#pragma once

// Scheduler state machines and dependency accounting (Milestone 07).
//
// Defines the run-level, node-level, and attempt-level state machines from
// docs/phase2/ARCHITECTURE.md §6, plus the dependency counter that implements
// the fan-in invariant (§6.5): a node becomes READY only when every
// predecessor has logically SUCCEEDED, and duplicate completion messages never
// decrement the counter twice.
//
// Ownership and thread-safety (Milestone 10): a SchedulerState owns the
// immutable Dag and all per-node state. Every public method takes the internal
// mutex, so the concurrent scheduler can drive the same transitions from
// multiple pool threads. The Dag itself is immutable and is returned by const
// reference without locking. Value-returning accessors (results, failure
// reasons) return snapshots so callers never observe unlocked internal state.

#include <chrono>
#include <cstddef>
#include <map>
#include <mutex>
#include <set>
#include <string>
#include <vector>

#include "evo/dag.hpp"
#include "evo/scheduler.hpp"

namespace evo {

// --- Run-level state (ARCHITECTURE.md §6.1) ---
enum class RunState {
  Queued,
  Running,
  Succeeded,
  Failed,
  Canceled,
};

// --- Node-level logical state (ARCHITECTURE.md §6.2) ---
enum class NodeState {
  Blocked,
  Ready,
  Dispatched,
  Running,
  Succeeded,
  RetryWait,
  Failed,
  DeadLettered,
  Canceled,
};

const char* to_string(RunState s);
const char* to_string(NodeState s);

// A node's logical status within a scheduler run.
struct NodeStatus {
  NodeState state;
  int remaining_deps;  // unsatisfied predecessor count (0 => dependency-free)
};

class SchedulerState {
 public:
  explicit SchedulerState(Dag dag);

  // --- Thread-safe accessors (all lock the internal mutex) ---
  RunState run_state() const;
  NodeState node_state(const NodeId& id) const;
  int remaining_deps(const NodeId& id) const;
  NodeStatus status(const NodeId& id) const;
  // The Dag is immutable after construction; safe to read without locking.
  const Dag& dag() const { return dag_; }

  // Nodes currently in the READY state (dispatch candidates).
  std::vector<NodeId> ready_nodes() const;

  // QUEUED → RUNNING; roots (zero-predecessor) become READY.
  void start_run();

  // READY → DISPATCHED.
  void dispatch_node(const NodeId& id);
  // DISPATCHED → RUNNING.
  void start_node(const NodeId& id);

  // RUNNING → SUCCEEDED | FAILED. On success, decrements predecessor
  // counters for each successor (idempotent: duplicate completions are
  // no-ops) and returns successors that just became READY. On failure,
  // propagates CANCELED to reachable successors and returns them.
  std::vector<NodeId> complete_node(const NodeId& id, const TaskResult& result);

  // FAIL any non-terminal node → FAILED, propagating CANCELED to all
  // transitively reachable not-yet-terminal successors. Returns canceled set.
  std::vector<NodeId> fail_node(const NodeId& id, const std::string& error);

  // Milestone 31: RUNNING/DISPATCHED → READY after the attempt's lease
  // expired (worker lost). The node becomes eligible for re-dispatch as a NEW
  // attempt; this is a recovery transition, NOT a failure — no successor is
  // touched and no dependency counter changes. Returns true only when the
  // transition applied (the node was in flight).
  bool abandon_node(const NodeId& id);

  // Cancel the entire run: every non-terminal node → CANCELED.
  std::vector<NodeId> cancel_run();

  bool is_terminal() const;
  bool all_nodes_terminal() const;

  // Snapshot of per-node results (thread-safe copy).
  std::map<NodeId, TaskResult> results() const;
  // Failure reason for a node, or empty string (thread-safe copy).
  std::string failure_reason(const NodeId& id) const;

  // Derives the run's terminal state from node outcomes (Milestone 10):
  // any FAILED node => Failed; else any CANCELED (or non-terminal leftover)
  // => Canceled; else Succeeded. No-op if the run is already terminal.
  void finalize_run();

 private:
  // Helper called with the lock held (from complete_node / fail_node).
  void mark_canceled_transitive(const NodeId& start,
                                std::vector<NodeId>& canceled);

  Dag dag_;
  mutable std::mutex mu_;
  RunState run_state_ = RunState::Queued;
  std::map<NodeId, NodeState> node_states_;
  std::map<NodeId, int> dep_counts_;
  std::set<NodeId> completed_;        // idempotency guard for completions
  std::map<NodeId, TaskResult> results_;
  std::map<NodeId, std::string> failure_reasons_;
};

}  // namespace evo
