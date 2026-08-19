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
