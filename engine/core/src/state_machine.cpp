#include "evo/state_machine.hpp"

#include <algorithm>
#include <map>
#include <mutex>

namespace evo {

const char* to_string(RunState s) {
  switch (s) {
    case RunState::Queued: return "QUEUED";
    case RunState::Running: return "RUNNING";
    case RunState::Succeeded: return "SUCCEEDED";
    case RunState::Failed: return "FAILED";
    case RunState::Canceled: return "CANCELED";
  }
  return "UNKNOWN";
}

const char* to_string(NodeState s) {
  switch (s) {
    case NodeState::Blocked: return "BLOCKED";
    case NodeState::Ready: return "READY";
    case NodeState::Dispatched: return "DISPATCHED";
    case NodeState::Running: return "RUNNING";
    case NodeState::Succeeded: return "SUCCEEDED";
    case NodeState::RetryWait: return "RETRY_WAIT";
    case NodeState::Failed: return "FAILED";
    case NodeState::DeadLettered: return "DEAD_LETTERED";
    case NodeState::Canceled: return "CANCELED";
  }
  return "UNKNOWN";
}

SchedulerState::SchedulerState(Dag dag) : dag_(std::move(dag)) {
  for (const auto& id : dag_.node_ids()) {
    node_states_[id] = NodeState::Blocked;
    dep_counts_[id] = static_cast<int>(dag_.predecessors(id).size());
  }
}

RunState SchedulerState::run_state() const {
  std::lock_guard lock(mu_);
  return run_state_;
}

NodeState SchedulerState::node_state(const NodeId& id) const {
  std::lock_guard lock(mu_);
  auto it = node_states_.find(id);
  return it == node_states_.end() ? NodeState::Blocked : it->second;
}

int SchedulerState::remaining_deps(const NodeId& id) const {
  std::lock_guard lock(mu_);
  auto it = dep_counts_.find(id);
  return it == dep_counts_.end() ? 0 : it->second;
}

NodeStatus SchedulerState::status(const NodeId& id) const {
  std::lock_guard lock(mu_);
  auto sit = node_states_.find(id);
  auto dit = dep_counts_.find(id);
  const NodeState state =
      sit == node_states_.end() ? NodeState::Blocked : sit->second;
  const int deps = dit == dep_counts_.end() ? 0 : dit->second;
  return NodeStatus{state, deps};
}

std::vector<NodeId> SchedulerState::ready_nodes() const {
  std::lock_guard lock(mu_);
  std::vector<NodeId> out;
  for (const auto& id : dag_.node_ids()) {
    if (node_states_.at(id) == NodeState::Ready) {
      out.push_back(id);
    }
  }
  return out;
}

void SchedulerState::start_run() {
  std::lock_guard lock(mu_);
  if (run_state_ != RunState::Queued) return;
  run_state_ = RunState::Running;
  for (const auto& id : dag_.node_ids()) {
    if (dep_counts_[id] == 0) {
      node_states_[id] = NodeState::Ready;
    }
  }
}

std::size_t SchedulerState::reconstruct(const std::vector<NodeRunRecord>& rows) {
  std::lock_guard lock(mu_);
  // A reconstructed run was already started before the restart.
  run_state_ = RunState::Running;

  // Map each persisted node-run row to a logical state.
  std::map<NodeId, NodeState> restored;
  std::map<NodeId, std::string> restored_output;
  std::map<NodeId, std::string> restored_reason;
  for (const auto& r : rows) {
    NodeId id{r.node_id};
    NodeState st;
    if (r.status == node_status::kSucceeded) {
      st = NodeState::Succeeded;
    } else if (r.status == node_status::kFailed) {
      st = NodeState::Failed;
    } else if (r.status == node_status::kDeadLettered) {
      st = NodeState::DeadLettered;
    } else if (r.status == node_status::kCanceled) {
      st = NodeState::Canceled;
    } else if (r.status == node_status::kRetryWait) {
      st = NodeState::RetryWait;
    } else if (r.status == node_status::kReady) {
      st = NodeState::Ready;
    } else if (r.status == node_status::kDispatched ||
               r.status == node_status::kRunning) {
      // In-flight at crash time: restore to RUNNING. The attempt either
      // completes (its result arrives) or its lease expires and the ordinary
      // lease-reap path re-dispatches it. The loop does NOT dispatch a
      // duplicate replacement while a valid lease still exists (M35 step 4).
      st = NodeState::Running;
    } else {
      st = NodeState::Blocked;  // blocked or unrecognized
    }
    restored[id] = st;
    restored_output[id] = r.output_json;
    restored_reason[id] = r.failure_reason;
  }

  // Apply restored states; DAG nodes absent from `rows` start BLOCKED.
  for (const auto& id : dag_.node_ids()) {
    auto it = restored.find(id);
    const NodeState st =
        (it == restored.end()) ? NodeState::Blocked : it->second;
    node_states_[id] = st;

    // Rebuild the terminal bookkeeping the live path maintains incrementally.
    switch (st) {
      case NodeState::Succeeded:
        completed_.insert(id);
        results_[id] = TaskResult{true, restored_output[id]};
        break;
      case NodeState::Failed:
      case NodeState::DeadLettered:
        completed_.insert(id);
        failure_reasons_[id] = restored_reason[id];
        results_[id] = TaskResult{false, restored_reason[id]};
        break;
      case NodeState::Canceled:
        failure_reasons_[id] = restored_reason[id];
        break;
      default:
        break;  // non-terminal: no completion/result recorded
    }
  }

  // Re-derive dependency counters from restored predecessor states: a node's
  // remaining deps = predecessors NOT yet logically succeeded.
  for (const auto& id : dag_.node_ids()) {
    int remaining = 0;
    for (const auto& pred : dag_.predecessors(id)) {
      if (node_states_[pred] != NodeState::Succeeded) ++remaining;
    }
    dep_counts_[id] = remaining;
  }

  // Resume dependency scheduling (M35 step 5): a non-terminal BLOCKED node
  // whose predecessors are all satisfied becomes READY. Nodes already READY /
  // RUNNING (in-flight) / RETRY_WAIT keep their restored state.
  std::size_t nonterminal = 0;
  for (const auto& id : dag_.node_ids()) {
    const NodeState st = node_states_[id];
    const bool terminal = st == NodeState::Succeeded ||
                          st == NodeState::Failed ||
                          st == NodeState::DeadLettered ||
                          st == NodeState::Canceled;
    if (!terminal) ++nonterminal;
    if (st == NodeState::Blocked && dep_counts_[id] == 0) {
      node_states_[id] = NodeState::Ready;
    }
  }
  return nonterminal;
}

void SchedulerState::dispatch_node(const NodeId& id) {
  std::lock_guard lock(mu_);
  auto it = node_states_.find(id);
  if (it == node_states_.end() || it->second != NodeState::Ready) return;
  it->second = NodeState::Dispatched;
}

void SchedulerState::start_node(const NodeId& id) {
  std::lock_guard lock(mu_);
  auto it = node_states_.find(id);
  if (it == node_states_.end() || it->second != NodeState::Dispatched) return;
  it->second = NodeState::Running;
}

std::vector<NodeId> SchedulerState::complete_node(const NodeId& id,
                                                  const TaskResult& result) {
  std::lock_guard lock(mu_);

  // Idempotent: a completion for an already-completed node is a no-op.
  if (completed_.contains(id)) return {};

  auto it = node_states_.find(id);
  if (it == node_states_.end() || it->second != NodeState::Running) return {};

  completed_.insert(id);
  results_[id] = result;

  if (result.completed) {
    it->second = NodeState::Succeeded;
    std::vector<NodeId> newly_ready;
    for (const auto& succ : dag_.successors(id)) {
      if (dep_counts_[succ] > 0) {
        --dep_counts_[succ];
      }
      if (dep_counts_[succ] == 0 &&
          node_states_[succ] == NodeState::Blocked) {
        node_states_[succ] = NodeState::Ready;
        newly_ready.push_back(succ);
      }
    }
    return newly_ready;
  }

  // Failure: mark this node FAILED and propagate CANCELED to reachable
  // non-terminal successors (explicit policy — no silent downstream execution).
  it->second = NodeState::Failed;
  failure_reasons_[id] = result.output;

  std::vector<NodeId> canceled;
  mark_canceled_transitive(id, canceled);
  return canceled;
}

std::vector<NodeId> SchedulerState::fail_node(const NodeId& id,
                                              const std::string& error) {
  std::lock_guard lock(mu_);
  auto it = node_states_.find(id);
  if (it == node_states_.end()) return {};
  if (it->second == NodeState::Succeeded || completed_.contains(id)) {
    return {};  // already completed; don't clobber a logical success
  }
  it->second = NodeState::Failed;
  failure_reasons_[id] = error;
  completed_.insert(id);

  std::vector<NodeId> canceled;
  mark_canceled_transitive(id, canceled);
  return canceled;
}

bool SchedulerState::abandon_node(const NodeId& id) {
  std::lock_guard lock(mu_);
  auto it = node_states_.find(id);
  if (it == node_states_.end()) return false;
  // Only an in-flight node can be abandoned; a terminal or not-yet-dispatched
  // node is untouched. Recovery, not failure: no successor propagation, no
  // dependency-counter change, no completion recorded.
  if (it->second != NodeState::Running && it->second != NodeState::Dispatched) {
    return false;
  }
  it->second = NodeState::Ready;
  return true;
}

bool SchedulerState::retry_wait_node(const NodeId& id) {
  std::lock_guard lock(mu_);
  auto it = node_states_.find(id);
  if (it == node_states_.end()) return false;
  // Only a Running node (a failed attempt just applied) can park for retry.
  // Not terminal: no successor touched, no dependency-counter change, no
  // completion recorded. The run loop owns the backoff due-time.
  if (it->second != NodeState::Running) return false;
  it->second = NodeState::RetryWait;
  return true;
}

bool SchedulerState::ready_from_retry(const NodeId& id) {
  std::lock_guard lock(mu_);
  auto it = node_states_.find(id);
  if (it == node_states_.end()) return false;
  // Only a parked RETRY_WAIT node becomes ready again; dispatch_ready() then
  // re-dispatches it as a NEW attempt.
  if (it->second != NodeState::RetryWait) return false;
  it->second = NodeState::Ready;
  return true;
}

std::vector<NodeId> SchedulerState::dead_letter_node(const NodeId& id,
                                                     const std::string& error) {
  std::lock_guard lock(mu_);
  auto it = node_states_.find(id);
  if (it == node_states_.end()) return {};
  if (it->second == NodeState::Succeeded || completed_.contains(id)) {
    return {};  // never clobber a logical success
  }
  // Terminal failure (retries exhausted): same downstream semantics as a
  // failed node — propagate CANCELED to reachable non-terminal successors.
  it->second = NodeState::DeadLettered;
  failure_reasons_[id] = error;
  completed_.insert(id);

  std::vector<NodeId> canceled;
  mark_canceled_transitive(id, canceled);
  return canceled;
}

void SchedulerState::mark_canceled_transitive(const NodeId& id,
                                              std::vector<NodeId>& canceled) {
  // Caller holds mu_.
  for (const auto& succ : dag_.successors(id)) {
    auto sit = node_states_.find(succ);
    if (sit == node_states_.end()) continue;
    if (sit->second == NodeState::Succeeded) continue;
    if (sit->second == NodeState::Failed ||
        sit->second == NodeState::DeadLettered ||
        sit->second == NodeState::Canceled) {
      continue;  // already terminal
    }
    sit->second = NodeState::Canceled;
    failure_reasons_[succ] = "canceled: upstream failure";
    if (std::find(canceled.begin(), canceled.end(), succ) == canceled.end()) {
      canceled.push_back(succ);
    }
    mark_canceled_transitive(succ, canceled);
  }
}

std::vector<NodeId> SchedulerState::cancel_run() {
  std::lock_guard lock(mu_);
  std::vector<NodeId> canceled;
  for (const auto& [id, state] : node_states_) {
    if (state == NodeState::Succeeded) continue;
    if (state == NodeState::Failed || state == NodeState::DeadLettered ||
        state == NodeState::Canceled) {
      continue;
    }
    node_states_[id] = NodeState::Canceled;
    failure_reasons_[id] = "run canceled";
    canceled.push_back(id);
  }
  run_state_ = RunState::Canceled;
  return canceled;
}

bool SchedulerState::is_terminal() const {
  std::lock_guard lock(mu_);
  return run_state_ == RunState::Succeeded ||
         run_state_ == RunState::Failed ||
         run_state_ == RunState::Canceled;
}

bool SchedulerState::all_nodes_terminal() const {
  std::lock_guard lock(mu_);
  for (const auto& [_, state] : node_states_) {
    if (state != NodeState::Succeeded && state != NodeState::Failed &&
        state != NodeState::DeadLettered && state != NodeState::Canceled) {
      return false;
    }
  }
  return true;
}

std::map<NodeId, TaskResult> SchedulerState::results() const {
  std::lock_guard lock(mu_);
  return results_;
}

std::string SchedulerState::failure_reason(const NodeId& id) const {
  std::lock_guard lock(mu_);
  auto it = failure_reasons_.find(id);
  return it == failure_reasons_.end() ? std::string{} : it->second;
}

void SchedulerState::finalize_run() {
  std::lock_guard lock(mu_);
  if (run_state_ == RunState::Succeeded || run_state_ == RunState::Failed ||
      run_state_ == RunState::Canceled) {
    return;  // already terminal
  }
  bool any_failed = false;
  bool any_canceled = false;
  bool any_nonterminal = false;
  for (const auto& [_, state] : node_states_) {
    switch (state) {
      case NodeState::Failed:
      case NodeState::DeadLettered:  // M32: retries exhausted is a failure
        any_failed = true;
        break;
      case NodeState::Canceled:
        any_canceled = true;
        break;
      default:
        if (state != NodeState::Succeeded) any_nonterminal = true;
        break;
    }
  }
  if (any_failed) {
    run_state_ = RunState::Failed;
  } else if (any_canceled || any_nonterminal) {
    run_state_ = RunState::Canceled;
  } else {
    run_state_ = RunState::Succeeded;
  }
}

}  // namespace evo
