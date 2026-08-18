#include "evo/state_machine.hpp"

#include <algorithm>
#include <map>

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

NodeState SchedulerState::node_state(const NodeId& id) const {
  auto it = node_states_.find(id);
  return it == node_states_.end() ? NodeState::Blocked : it->second;
}

int SchedulerState::remaining_deps(const NodeId& id) const {
  auto it = dep_counts_.find(id);
  return it == dep_counts_.end() ? 0 : it->second;
}

NodeStatus SchedulerState::status(const NodeId& id) const {
  return NodeStatus{node_state(id), remaining_deps(id)};
}

std::vector<NodeId> SchedulerState::ready_nodes() const {
  std::vector<NodeId> out;
  for (const auto& id : dag_.node_ids()) {
    if (node_states_.at(id) == NodeState::Ready) {
      out.push_back(id);
    }
  }
  return out;
}

void SchedulerState::start_run() {
  if (run_state_ != RunState::Queued) return;
  run_state_ = RunState::Running;
  for (const auto& id : dag_.node_ids()) {
    if (dep_counts_[id] == 0) {
      node_states_[id] = NodeState::Ready;
    }
  }
}

void SchedulerState::dispatch_node(const NodeId& id) {
  auto it = node_states_.find(id);
  if (it == node_states_.end() || it->second != NodeState::Ready) return;
  it->second = NodeState::Dispatched;
}

void SchedulerState::start_node(const NodeId& id) {
  auto it = node_states_.find(id);
  if (it == node_states_.end() || it->second != NodeState::Dispatched) return;
  it->second = NodeState::Running;
}

std::vector<NodeId> SchedulerState::complete_node(const NodeId& id,
                                                     const TaskResult& result) {
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

void SchedulerState::mark_canceled_transitive(const NodeId& id,
                                               std::vector<NodeId>& canceled) {
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
  std::vector<NodeId> canceled;
  for (const auto& [id, state] : node_states_) {
    if (state == NodeState::Succeeded) continue;
    if (state == NodeState::Failed || state == NodeState::DeadLettered ||
        state == NodeState::Canceled) {
      continue;
    }
    node_states_[id] = NodeState::Canceled;
    if (state == NodeState::Blocked || state == NodeState::Ready ||
        state == NodeState::Dispatched || state == NodeState::Running) {
      failure_reasons_[id] = "run canceled";
    }
    canceled.push_back(id);
  }
  run_state_ = RunState::Canceled;
  return canceled;
}

bool SchedulerState::is_terminal() const {
  return run_state_ == RunState::Succeeded ||
         run_state_ == RunState::Failed ||
         run_state_ == RunState::Canceled;
}

bool SchedulerState::all_nodes_terminal() const {
  for (const auto& [_, state] : node_states_) {
    if (state != NodeState::Succeeded && state != NodeState::Failed &&
        state != NodeState::DeadLettered && state != NodeState::Canceled) {
      return false;
    }
  }
  return true;
}

const std::string& SchedulerState::failure_reason(const NodeId& id) const {
  static const std::string kEmpty;
  auto it = failure_reasons_.find(id);
  return it == failure_reasons_.end() ? kEmpty : it->second;
}

}  // namespace evo
