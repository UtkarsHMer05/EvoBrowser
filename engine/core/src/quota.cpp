#include "evo/quota.hpp"

#include "evo/json.hpp"

namespace evo {

namespace {
const char* class_key(ResourceClass rc) { return to_string(rc); }

// Default demand-freshness window (wall-clock ms). A waiting org refreshes its
// demand every time its run loop re-polls a deferred node (each main-loop
// iteration, paced by read_block_ms), so a live waiter refreshes far faster
// than this; entries that go stale belong to a run that stopped polling
// (canceled/finished) and must not keep holding a class slot hostage.
constexpr std::int64_t kDefaultDemandTimeoutMs = 5000;
}  // namespace

TenantQuotaGate::TenantQuotaGate(QuotaConfig cfg) : cfg_(std::move(cfg)) {}

bool TenantQuotaGate::admit_run(const std::string& org_id) {
  std::lock_guard lock(mu_);
  if (cfg_.max_active_runs_per_org > 0 &&
      active_runs_[org_id] >= cfg_.max_active_runs_per_org) {
    ++counters_.rejected_runs;
    return false;
  }
  if (cfg_.max_active_runs_global > 0 &&
      total_active_runs_ >= cfg_.max_active_runs_global) {
    ++counters_.rejected_runs;
    return false;
  }
  ++active_runs_[org_id];
  ++total_active_runs_;
  ++counters_.admitted_runs;
  if (total_active_runs_ > counters_.max_active_runs) {
    counters_.max_active_runs = total_active_runs_;
  }
  return true;
}

void TenantQuotaGate::readmit_run(const std::string& org_id) {
  std::lock_guard lock(mu_);
  ++active_runs_[org_id];
  ++total_active_runs_;
  if (total_active_runs_ > counters_.max_active_runs) {
    counters_.max_active_runs = total_active_runs_;
  }
}

void TenantQuotaGate::release_run(const std::string& org_id) {
  std::lock_guard lock(mu_);
  auto it = active_runs_.find(org_id);
  if (it != active_runs_.end() && it->second > 0) {
    --it->second;
    if (total_active_runs_ > 0) --total_active_runs_;
  }
}

bool TenantQuotaGate::acquire_task(const std::string& org_id,
                                   ResourceClass klass) {
  std::lock_guard lock(mu_);
  if (cfg_.max_inflight_tasks_per_org > 0 &&
      inflight_tasks_[org_id] >= cfg_.max_inflight_tasks_per_org) {
    ++counters_.deferred_tasks;
    return false;
  }
  auto cap = cfg_.global_class_capacity.find(klass);
  const bool class_capped =
      cap != cfg_.global_class_capacity.end() && cap->second > 0;

  // M37: fair tenant selection. Applies only when enabled AND the class has a
  // finite global capacity (an uncapped class cannot starve anyone). The caller
  // ALWAYS registers/refreshes its demand first, so the gate tracks every
  // waiting org even while the class is full; when a slot is free it goes to
  // the LEAST-SERVED org with fresh demand (weighted by org_weights). A caller
  // that is not the recipient is deferred — its node stays READY and its run
  // loop re-polls, refreshing demand. Because a waiting org's served count
  // stays constant while recipients' counts grow, every live waiting org
  // becomes the minimum within bounded time: no starvation (M37 step 5).
  if (cfg_.fair_scheduling && class_capped) {
    const std::int64_t now = now_wall_ms();
    demand_[klass][org_id] = now;  // register/refresh this org's demand

    if (inflight_class_[klass] >= cap->second) {
      // Class full: defer, but demand is registered so this org is tracked as
      // waiting. When a slot frees, recipient selection sees it.
      ++counters_.deferred_tasks;
      return false;
    }

    // A class slot is free: grant it to the least-served org with fresh demand.
    const std::string recipient = fair_recipient_locked(klass, now);
    if (!recipient.empty() && recipient != org_id) {
      // A less-served org is waiting: defer this caller (backpressure). The
      // slot stays free for the recipient's next poll.
      ++counters_.deferred_tasks;
      ++counters_.fair_order_deferrals;
      return false;
    }
    // Caller is the recipient (or the only fresh demand): fall through to grant.
  } else if (class_capped && inflight_class_[klass] >= cap->second) {
    // M36 path (fairness off): first-come-first-served capacity check.
    ++counters_.deferred_tasks;
    return false;
  }

  ++inflight_tasks_[org_id];
  ++inflight_class_[klass];
  ++counters_.acquired_tasks;
  if (cfg_.fair_scheduling) {
    ++served_[klass][org_id];  // fairness accounting for the next decision
    // The org got what it asked for: it is no longer WAITING. Clear its demand
    // so an org that finishes and stops polling cannot keep being picked as the
    // recipient (which would starve live waiters until the demand timeout). It
    // re-registers demand on its next poll if it has more tasks.
    demand_[klass].erase(org_id);
  }
  return true;
}

std::string TenantQuotaGate::fair_recipient_locked(ResourceClass klass,
                                                   std::int64_t now_ms) {
  const std::int64_t timeout = cfg_.fair_demand_timeout_ms > 0
                                   ? cfg_.fair_demand_timeout_ms
                                   : kDefaultDemandTimeoutMs;
  auto& demand = demand_[klass];
  // Drop stale demand entries first (an org that stopped polling is gone).
  for (auto it = demand.begin(); it != demand.end();) {
    if (now_ms - it->second > timeout) {
      it = demand.erase(it);
    } else {
      ++it;
    }
  }
  // Pick the org with the smallest served/weight. Cross-multiply to compare
  // served_a/weight_a < served_b/weight_b without floating point:
  //   served_a * weight_b < served_b * weight_a.
  // Ties break on org id (std::map iteration order) for determinism.
  std::string best;
  std::int64_t best_served = 0;
  int best_weight = 1;
  for (const auto& [org, last_ms] : demand) {
    (void)last_ms;  // freshness already filtered above
    const std::int64_t served = served_[klass][org];
    const int weight = weight_for(org);
    if (best.empty()) {
      best = org;
      best_served = served;
      best_weight = weight;
      continue;
    }
    // served/weight < best_served/best_weight  <=>  served*best_weight < best_served*weight
    if (served * best_weight < best_served * weight) {
      best = org;
      best_served = served;
      best_weight = weight;
    }
  }
  return best;
}

void TenantQuotaGate::release_task(const std::string& org_id,
                                   ResourceClass klass) {
  std::lock_guard lock(mu_);
  auto oit = inflight_tasks_.find(org_id);
  if (oit != inflight_tasks_.end() && oit->second > 0) --oit->second;
  auto cit = inflight_class_.find(klass);
  if (cit != inflight_class_.end() && cit->second > 0) --cit->second;
  ++counters_.released_tasks;
}

void TenantQuotaGate::reacquire_task(const std::string& org_id,
                                     ResourceClass klass) {
  std::lock_guard lock(mu_);
  ++inflight_tasks_[org_id];
  ++inflight_class_[klass];
}

QuotaCounters TenantQuotaGate::counters() const {
  std::lock_guard lock(mu_);
  QuotaCounters out = counters_;
  out.active_runs_now = total_active_runs_;
  return out;
}

int TenantQuotaGate::active_runs(const std::string& org_id) const {
  std::lock_guard lock(mu_);
  auto it = active_runs_.find(org_id);
  return it == active_runs_.end() ? 0 : it->second;
}

int TenantQuotaGate::inflight_tasks(const std::string& org_id) const {
  std::lock_guard lock(mu_);
  auto it = inflight_tasks_.find(org_id);
  return it == inflight_tasks_.end() ? 0 : it->second;
}

int TenantQuotaGate::inflight_class(ResourceClass klass) const {
  std::lock_guard lock(mu_);
  auto it = inflight_class_.find(klass);
  return it == inflight_class_.end() ? 0 : it->second;
}

std::int64_t TenantQuotaGate::served_count(const std::string& org_id,
                                           ResourceClass klass) const {
  std::lock_guard lock(mu_);
  auto cit = served_.find(klass);
  if (cit == served_.end()) return 0;
  auto oit = cit->second.find(org_id);
  return oit == cit->second.end() ? 0 : oit->second;
}

int TenantQuotaGate::demand_count(ResourceClass klass) const {
  std::lock_guard lock(mu_);
  const std::int64_t now = now_wall_ms();
  const std::int64_t timeout = cfg_.fair_demand_timeout_ms > 0
                                   ? cfg_.fair_demand_timeout_ms
                                   : kDefaultDemandTimeoutMs;
  auto cit = demand_.find(klass);
  if (cit == demand_.end()) return 0;
  int n = 0;
  for (const auto& [org, last_ms] : cit->second) {
    (void)org;
    if (now - last_ms <= timeout) ++n;
  }
  return n;
}

int TenantQuotaGate::weight_for(const std::string& org_id) const {
  // No lock: reads only the immutable cfg_ (set at construction).
  auto it = cfg_.org_weights.find(org_id);
  return (it == cfg_.org_weights.end() || it->second < 1) ? 1 : it->second;
}

std::string TenantQuotaGate::to_json_string() const {
  std::lock_guard lock(mu_);
  json::Object o;
  o.emplace("admitted_runs",
            json::Value(static_cast<double>(counters_.admitted_runs)));
  o.emplace("rejected_runs",
            json::Value(static_cast<double>(counters_.rejected_runs)));
  o.emplace("acquired_tasks",
            json::Value(static_cast<double>(counters_.acquired_tasks)));
  o.emplace("deferred_tasks",
            json::Value(static_cast<double>(counters_.deferred_tasks)));
  o.emplace("released_tasks",
            json::Value(static_cast<double>(counters_.released_tasks)));
  o.emplace("fair_order_deferrals",
            json::Value(static_cast<double>(counters_.fair_order_deferrals)));
  o.emplace("fair_scheduling", json::Value(cfg_.fair_scheduling));
  o.emplace("active_runs_now",
            json::Value(static_cast<double>(total_active_runs_)));
  o.emplace("max_active_runs",
            json::Value(static_cast<double>(counters_.max_active_runs)));

  json::Object orgs;
  for (const auto& [org, n] : active_runs_) {
    json::Object one;
    one.emplace("active_runs", json::Value(static_cast<double>(n)));
    auto it = inflight_tasks_.find(org);
    one.emplace("inflight_tasks",
                json::Value(static_cast<double>(it == inflight_tasks_.end()
                                                    ? 0
                                                    : it->second)));
    one.emplace("weight", json::Value(static_cast<double>(weight_for(org))));
    orgs.emplace(org, json::Value(std::move(one)));
  }
  o.emplace("orgs", json::Value(std::move(orgs)));

  json::Object classes;
  for (const auto& [klass, n] : inflight_class_) {
    classes.emplace(class_key(klass), json::Value(static_cast<double>(n)));
  }
  o.emplace("inflight_by_class", json::Value(std::move(classes)));

  // M37: per-class served counts (the fairness accounting).
  json::Object served;
  for (const auto& [klass, per_org] : served_) {
    json::Object po;
    for (const auto& [org, n] : per_org) {
      po.emplace(org, json::Value(static_cast<double>(n)));
    }
    served.emplace(class_key(klass), json::Value(std::move(po)));
  }
  o.emplace("served_by_class", json::Value(std::move(served)));

  json::Object doc;
  doc.emplace("quota", json::Value(std::move(o)));
  return json::serialize(json::Value(std::move(doc)));
}

}  // namespace evo
