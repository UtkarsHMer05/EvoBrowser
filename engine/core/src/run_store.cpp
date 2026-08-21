#include "evo/run_store.hpp"

#include <chrono>

namespace evo {

std::int64_t now_wall_ms() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
             std::chrono::system_clock::now().time_since_epoch())
      .count();
}

namespace node_status {
bool is_terminal(const std::string& status) {
  return status == kSucceeded || status == kFailed ||
         status == kCanceled || status == kDeadLettered;
}
}  // namespace node_status

bool InMemoryRunStore::ensure_workflow(const std::string& workflow_id,
                                       const std::string& org_id,
                                       const std::string& name) {
  (void)org_id;
  (void)name;
  std::lock_guard lock(mu_);
  workflows_.insert(workflow_id);
  return true;
}

bool InMemoryRunStore::create_run(const RunRecord& run,
                                  std::int64_t started_wall_ms) {
  (void)started_wall_ms;
  std::lock_guard lock(mu_);
  if (run.run_id.empty()) return false;
  auto it = runs_.find(run.run_id);
  if (it != runs_.end()) {
    // Idempotent, mirroring PgRunStore: a pre-existing 'queued' row (the app
    // pre-creates it) is promoted to the engine's status; anything else is
    // left untouched so state never regresses.
    if (it->second.status == run_status::kQueued) {
      it->second.status = run.status;
    }
    return true;
  }
  runs_[run.run_id] = run;
  return true;
}

bool InMemoryRunStore::create_node_run(const std::string& run_id,
                                       const std::string& node_id,
                                       const std::string& node_type) {
  std::lock_guard lock(mu_);
  if (!runs_.contains(run_id)) return false;
  NodeKey key{run_id, node_id};
  if (node_runs_.contains(key)) return true;  // idempotent
  NodeRunRecord rec;
  rec.node_id = node_id;
  rec.node_type = node_type;
  node_runs_[key] = rec;
  return true;
}

bool InMemoryRunStore::set_node_status(const std::string& run_id,
                                       const std::string& node_id,
                                       const std::string& status) {
  std::lock_guard lock(mu_);
  auto it = node_runs_.find({run_id, node_id});
  if (it == node_runs_.end()) return false;
  if (node_status::is_terminal(it->second.status)) return false;
  it->second.status = status;
  return true;
}

bool InMemoryRunStore::set_node_retry_wait(
    const std::string& run_id, const std::string& node_id,
    std::int64_t retry_wait_until_wall_ms, const std::string& retry_reason) {
  std::lock_guard lock(mu_);
  auto it = node_runs_.find({run_id, node_id});
  if (it == node_runs_.end()) return false;
  if (node_status::is_terminal(it->second.status)) return false;
  it->second.status = node_status::kRetryWait;
  it->second.retry_wait_until = retry_wait_until_wall_ms;
  it->second.retry_reason = retry_reason;
  return true;
}

bool InMemoryRunStore::record_attempt(const std::string& run_id,
                                      const std::string& node_id,
                                      unsigned attempt_number,
                                      const std::string& worker_id,
                                      std::int64_t started_wall_ms) {
  (void)started_wall_ms;
  std::lock_guard lock(mu_);
  NodeKey key{run_id, node_id};
  auto it = node_runs_.find(key);
  if (it == node_runs_.end()) return false;
  auto [att_it, inserted] = attempts_[key].insert(attempt_number);
  if (!inserted) return false;  // duplicate delivery: no second attempt row
  attempt_workers_[key][attempt_number] = worker_id;
  it->second.attempt_count =
      static_cast<int>(attempts_[key].size());
  return true;
}

bool InMemoryRunStore::finish_attempt(const std::string& run_id,
                                      const std::string& node_id,
                                      unsigned attempt_number,
                                      const std::string& worker_id,
                                      const std::string& status,
                                      const std::string& error,
                                      std::int64_t finished_wall_ms) {
  (void)error;
  (void)finished_wall_ms;
  std::lock_guard lock(mu_);
  NodeKey key{run_id, node_id};
  auto it = attempts_.find(key);
  if (it == attempts_.end() || !it->second.contains(attempt_number)) {
    return false;
  }
  attempt_status_[key][attempt_number] = status;
  if (!worker_id.empty()) {
    attempt_workers_[key][attempt_number] = worker_id;  // COALESCE semantics
  }
  // M31: a completed attempt's lease is terminal, so the expired-lease scan
  // can never reap it (lease expiry must not double-complete a node).
  auto lit = attempt_leases_.find({run_id, node_id, attempt_number});
  if (lit != attempt_leases_.end() &&
      lit->second.status == attempt_status::kRunning) {
    lit->second.status = (status == node_status::kSucceeded)
                             ? attempt_status::kSucceeded
                             : attempt_status::kFailed;
  }
  return true;
}

bool InMemoryRunStore::complete_node_run(const std::string& run_id,
                                         const std::string& node_id,
                                         const std::string& status,
                                         const std::string& output_json,
                                         const std::string& failure_reason,
                                         std::int64_t finished_wall_ms) {
  (void)finished_wall_ms;
  std::lock_guard lock(mu_);
  auto it = node_runs_.find({run_id, node_id});
  if (it == node_runs_.end()) return false;
  if (node_status::is_terminal(it->second.status)) return false;  // at-most-once
  if (!node_status::is_terminal(status)) return false;
  it->second.status = status;
  if (status == node_status::kSucceeded) {
    it->second.output_json = output_json;
  } else {
    it->second.failure_reason = failure_reason;
  }
  return true;
}

bool InMemoryRunStore::finish_run(const std::string& run_id,
                                  const std::string& status,
                                  const std::string& outcome,
                                  std::int64_t finished_wall_ms) {
  (void)finished_wall_ms;
  std::lock_guard lock(mu_);
  auto it = runs_.find(run_id);
  if (it == runs_.end()) return false;
  it->second.status = status;
  it->second.outcome = outcome;
  return true;
}

bool InMemoryRunStore::mark_cancel_requested(const std::string& run_id,
                                             const std::string& reason,
                                             std::int64_t requested_wall_ms) {
  std::lock_guard lock(mu_);
  auto it = runs_.find(run_id);
  if (it == runs_.end()) return false;
  // First request wins (mirrors PgRunStore's WHERE cancel_requested_at IS
  // NULL): a repeated request is a no-op that still reports success.
  if (it->second.cancel_requested_at == 0) {
    it->second.cancel_requested_at = requested_wall_ms;
    it->second.cancel_reason = reason;
  }
  return true;
}

// --- Milestone 31: worker registry + task leases ----------------------------

bool InMemoryRunStore::worker_heartbeat(const std::string& worker_id,
                                        const std::string& env_prefix,
                                        std::int64_t now_wall_ms) {
  std::lock_guard lock(mu_);
  if (worker_id.empty()) return false;
  auto& w = workers_[worker_id];  // upsert
  w.worker_id = worker_id;
  w.env_prefix = env_prefix;
  w.status = "alive";
  w.last_heartbeat_ms = now_wall_ms;
  return true;
}

bool InMemoryRunStore::init_attempt_lease(const std::string& run_id,
                                          const std::string& node_id,
                                          unsigned attempt_number,
                                          std::int64_t expires_wall_ms) {
  std::lock_guard lock(mu_);
  if (!attempts_[{run_id, node_id}].contains(attempt_number)) return false;
  AttemptKey key{run_id, node_id, attempt_number};
  if (attempt_leases_.contains(key)) return true;  // idempotent: already init
  AttemptLeaseRecord rec;
  rec.node_id = node_id;
  rec.attempt_number = attempt_number;
  rec.worker_id = "";  // no worker yet (queued/dispatched)
  rec.status = attempt_status::kRunning;
  rec.acquired_ms = 0;
  rec.renewed_ms = 0;
  rec.expires_ms = expires_wall_ms;
  attempt_leases_[key] = rec;
  return true;
}

bool InMemoryRunStore::acquire_attempt_lease(const std::string& run_id,
                                             const std::string& node_id,
                                             unsigned attempt_number,
                                             const std::string& worker_id,
                                             std::int64_t acquired_wall_ms,
                                             std::int64_t expires_wall_ms) {
  std::lock_guard lock(mu_);
  if (!attempts_[{run_id, node_id}].contains(attempt_number)) return false;
  AttemptKey key{run_id, node_id, attempt_number};
  auto it = attempt_leases_.find(key);
  if (it != attempt_leases_.end()) {
    const AttemptLeaseRecord& l = it->second;
    const bool terminal = l.status == attempt_status::kSucceeded ||
                          l.status == attempt_status::kFailed ||
                          l.status == attempt_status::kLeaseExpired;
    if (terminal) return false;
    // Steal guard: a DIFFERENT worker may only take over once the current
    // lease has expired. An empty worker_id (initialized at dispatch, not yet
    // claimed) is acquirable by anyone.
    if (!l.worker_id.empty() && l.worker_id != worker_id &&
        l.expires_ms > acquired_wall_ms) {
      return false;
    }
  }
  AttemptLeaseRecord rec;
  rec.node_id = node_id;
  rec.attempt_number = attempt_number;
  rec.worker_id = worker_id;
  rec.status = attempt_status::kRunning;
  rec.acquired_ms = acquired_wall_ms;
  rec.renewed_ms = acquired_wall_ms;
  rec.expires_ms = expires_wall_ms;
  attempt_leases_[key] = rec;
  attempt_workers_[{run_id, node_id}][attempt_number] = worker_id;
  return true;
}

bool InMemoryRunStore::renew_attempt_lease(const std::string& run_id,
                                           const std::string& node_id,
                                           unsigned attempt_number,
                                           const std::string& worker_id,
                                           std::int64_t renewed_wall_ms,
                                           std::int64_t expires_wall_ms) {
  std::lock_guard lock(mu_);
  auto it = attempt_leases_.find({run_id, node_id, attempt_number});
  if (it == attempt_leases_.end()) return false;
  AttemptLeaseRecord& l = it->second;
  if (l.status != attempt_status::kRunning) return false;  // terminal
  if (l.worker_id != worker_id) return false;  // only the holder renews
  l.renewed_ms = renewed_wall_ms;
  l.expires_ms = expires_wall_ms;
  return true;
}

std::vector<AttemptLeaseRecord> InMemoryRunStore::scan_expired_attempt_leases(
    const std::string& run_id, std::int64_t now_wall_ms) {
  std::lock_guard lock(mu_);
  std::vector<AttemptLeaseRecord> out;
  for (const auto& [key, l] : attempt_leases_) {
    if (std::get<0>(key) != run_id) continue;
    if (l.status != attempt_status::kRunning) continue;  // already reaped/done
    if (l.expires_ms > 0 && l.expires_ms <= now_wall_ms) out.push_back(l);
  }
  return out;
}

bool InMemoryRunStore::mark_attempt_lease_expired(const std::string& run_id,
                                                  const std::string& node_id,
                                                  unsigned attempt_number,
                                                  const std::string& worker_id,
                                                  std::int64_t expired_wall_ms) {
  std::lock_guard lock(mu_);
  auto it = attempt_leases_.find({run_id, node_id, attempt_number});
  if (it == attempt_leases_.end()) return false;
  AttemptLeaseRecord& l = it->second;
  // At-most-once reap: only a still-running lease held by this worker can be
  // expired. A racing completion (status already terminal) is never
  // double-completed.
  if (l.status != attempt_status::kRunning) return false;
  if (l.worker_id != worker_id) return false;
  l.status = attempt_status::kLeaseExpired;
  l.expired_ms = expired_wall_ms;
  attempt_status_[{run_id, node_id}][attempt_number] =
      attempt_status::kLeaseExpired;
  return true;
}

std::optional<AttemptLeaseRecord> InMemoryRunStore::get_attempt_lease(
    const std::string& run_id, const std::string& node_id,
    unsigned attempt_number) {
  std::lock_guard lock(mu_);
  auto it = attempt_leases_.find({run_id, node_id, attempt_number});
  if (it == attempt_leases_.end()) return std::nullopt;
  return it->second;
}

std::optional<WorkerRecord> InMemoryRunStore::get_worker(
    const std::string& worker_id) {
  std::lock_guard lock(mu_);
  auto it = workers_.find(worker_id);
  if (it == workers_.end()) return std::nullopt;
  return it->second;
}

// --- Milestone 33: idempotency ledger ---------------------------------------

bool InMemoryRunStore::claim_idempotency_key(const std::string& key,
                                             const std::string& run_id,
                                             const std::string& response_json) {
  (void)run_id;  // audit-only in the durable store
  std::lock_guard lock(mu_);
  if (key.empty()) return false;
  // First claim wins (mirrors INSERT ... ON CONFLICT DO NOTHING on the key's
  // PRIMARY KEY); a duplicate claim is a no-op that reports not-applied.
  return idempotency_.emplace(key, response_json).second;
}

std::optional<std::string> InMemoryRunStore::get_idempotency_response(
    const std::string& key) {
  std::lock_guard lock(mu_);
  auto it = idempotency_.find(key);
  if (it == idempotency_.end()) return std::nullopt;
  return it->second;
}

std::optional<RunRecord> InMemoryRunStore::get_run(
    const std::string& run_id) {
  std::lock_guard lock(mu_);
  auto it = runs_.find(run_id);
  if (it == runs_.end()) return std::nullopt;
  return it->second;
}

std::optional<NodeRunRecord> InMemoryRunStore::get_node_run(
    const std::string& run_id, const std::string& node_id) {
  std::lock_guard lock(mu_);
  auto it = node_runs_.find({run_id, node_id});
  if (it == node_runs_.end()) return std::nullopt;
  return it->second;
}

std::size_t InMemoryRunStore::attempt_row_count(const std::string& run_id,
                                                const std::string& node_id) {
  std::lock_guard lock(mu_);
  auto it = attempts_.find({run_id, node_id});
  if (it == attempts_.end()) return 0;
  return it->second.size();
}

std::vector<std::string> InMemoryRunStore::attempt_worker_ids(
    const std::string& run_id, const std::string& node_id) {
  std::lock_guard lock(mu_);
  std::vector<std::string> out;
  auto it = attempt_workers_.find({run_id, node_id});
  if (it == attempt_workers_.end()) return out;
  for (const auto& [attempt, worker] : it->second) out.push_back(worker);
  return out;
}

}  // namespace evo
