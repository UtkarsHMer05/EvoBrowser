#include "evo/quota.hpp"

#include "evo/json.hpp"

namespace evo {

namespace {
const char* class_key(ResourceClass rc) { return to_string(rc); }
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
  if (cap != cfg_.global_class_capacity.end() && cap->second > 0 &&
      inflight_class_[klass] >= cap->second) {
    ++counters_.deferred_tasks;
    return false;
  }
  ++inflight_tasks_[org_id];
  ++inflight_class_[klass];
  ++counters_.acquired_tasks;
  return true;
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
    orgs.emplace(org, json::Value(std::move(one)));
  }
  o.emplace("orgs", json::Value(std::move(orgs)));

  json::Object classes;
  for (const auto& [klass, n] : inflight_class_) {
    classes.emplace(class_key(klass), json::Value(static_cast<double>(n)));
  }
  o.emplace("inflight_by_class", json::Value(std::move(classes)));

  json::Object doc;
  doc.emplace("quota", json::Value(std::move(o)));
  return json::serialize(json::Value(std::move(doc)));
}

}  // namespace evo
