#pragma once

// Deterministic sequential reference scheduler (Milestone 06).
//
// Executes an immutable Dag in topological order against a caller-supplied
// task registry. This is the correctness oracle and the performance baseline
// for the Phase-2 Evo engine — it is NOT a replacement for Phase-1's
// Trigger.dev path (see phase2(ARCHITECTURE.md) §3.1).
//
// Thread-safety: a Scheduler owns its Dag by value and is single-threaded.
// TaskFn invocations happen sequentially on the calling thread, so the task
// registry and any task-captured state need no synchronization here.

#include <chrono>
#include <cstddef>
#include <functional>
#include <map>
#include <string>
#include <vector>

#include "evo/dag.hpp"

namespace evo {

// Outcome of executing one node. `completed == false` marks a logical failure;
// `output` is an opaque task-produced payload (text or JSON) preserved for
// diagnosis. In sequential mode a failure halts the run, but the partial log
// is still returned.
struct TaskResult {
  bool completed = true;
  std::string output;
};

// A unit of executable work for a node type. Receives the node's spec
// (id/kind/type). Per-node parameters (e.g. benchmark durations) are captured
// by closure in the task factory — the core scheduler stays parameter-free so
// it can be reused by the concurrent engine (M07–M10).
using TaskFn = std::function<TaskResult(const NodeSpec&)>;

// Record of one node's execution within a run.
struct NodeRun {
  NodeId id;
  std::string type;
  std::size_t sequence;  // position in execution order (0-based)
  std::chrono::steady_clock::time_point started_at;
  std::chrono::steady_clock::time_point finished_at;
  TaskResult result;

  bool ok() const { return result.completed; }
  std::chrono::nanoseconds duration() const { return finished_at - started_at; }
};

// Ordered, deterministic record of a sequential run.
struct RunLog {
  std::vector<NodeRun> runs;

  bool all_ok() const;
  // Canonical serialization for deterministic test/benchmark assertions.
  std::string to_json_string() const;
};

class Scheduler {
 public:
  Scheduler(Dag dag, std::map<std::string, TaskFn> tasks);

  // Runs every node in the Dag's deterministic topological order (dependencies
  // are satisfied by construction). Stops at the first failing node.
  RunLog run();

  const Dag& dag() const { return dag_; }
  std::size_t registered_task_count() const { return tasks_.size(); }

 private:
  Dag dag_;
  std::map<std::string, TaskFn> tasks_;
};

}  // namespace evo
