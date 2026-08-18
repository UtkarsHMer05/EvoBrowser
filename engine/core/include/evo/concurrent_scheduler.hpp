#pragma once

// Concurrent dependency-aware DAG scheduler (Milestone 10).
//
// Combines the thread pool (M09), ready queue (M08), and thread-safe state
// machine (M07) into a scheduler that executes independent branches in parallel
// while respecting dependencies. Produces a RunLog with per-node
// ready/start/finish timestamps (steady_clock) for equivalence testing against
// the sequential reference scheduler and for concurrency overlap verification.

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <functional>
#include <map>
#include <mutex>
#include <optional>
#include <set>
#include <stop_token>
#include <string>
#include <vector>

#include "evo/dag.hpp"
#include "evo/ready_queue.hpp"
#include "evo/scheduler.hpp"
#include "evo/state_machine.hpp"
#include "evo/thread_pool.hpp"

namespace evo {

// Extended run record that includes the moment a node became READY (in addition
// to the started_at/finished_at from the sequential scheduler's NodeRun).
struct ConcurrentNodeRun {
  NodeId id;
  std::string type;
  std::size_t sequence;  // order of logical completion (not dispatch order)
  std::chrono::steady_clock::time_point ready_at;   // when deps satisfied
  std::chrono::steady_clock::time_point started_at; // when worker began exec
  std::chrono::steady_clock::time_point finished_at; // when worker finished
  TaskResult result;

  bool ok() const { return result.completed; }
  std::chrono::nanoseconds duration() const { return finished_at - started_at; }
  std::chrono::nanoseconds queue_latency() const { return started_at - ready_at; }
};

// Run log for the concurrent scheduler. The `runs` vector is sorted by
// `sequence` (logical completion order) so it is directly comparable to the
// sequential scheduler's RunLog (which uses topo order == completion order).
struct ConcurrentRunLog {
  std::vector<ConcurrentNodeRun> runs;

  bool all_ok() const {
    for (const auto& r : runs) if (!r.ok()) return false;
    return !runs.empty();
  }

  // Canonical serialization for deterministic test/benchmark assertions.
  // Produces the same field names as the sequential RunLog plus `ready_at`.
  std::string to_json_string() const;
};

// Configuration for the concurrent scheduler.
struct ConcurrentConfig {
  // Number of worker threads (must be > 0).
  std::size_t num_workers = 4;

  // Maximum number of ready nodes to keep in the dispatch queue (0 = unbounded).
  std::size_t ready_queue_capacity = 0;
};

class ConcurrentScheduler {
 public:
  // Takes ownership of the Dag and task registry. The scheduler is single-use:
  // call run() once. The provided tasks must be thread-safe (executed from
  // multiple pool threads concurrently).
  ConcurrentScheduler(Dag dag, std::map<std::string, TaskFn> tasks,
                      ConcurrentConfig config = {});

  ~ConcurrentScheduler();

  // Run the entire DAG concurrently. Blocks until all nodes reach a terminal
  // state (all succeeded, any failed, or run canceled). Returns the complete
  // run log with per-node timestamps.
  ConcurrentRunLog run();

  // Request cooperative cancellation. No new nodes will be dispatched; already
  // running nodes are allowed to finish (or observe stop_token if they support
  // it). Idempotent.
  void cancel();

  // Wait for the run to complete (if run() was started asynchronously) or
  // return immediately if already terminal. The ConcurrentScheduler in M10 is
  // synchronous — run() blocks — so this is a no-op for now but reserved for
  // future async execution modes.
  void wait();

  const Dag& dag() const { return state_.dag(); }
  std::size_t num_workers() const { return pool_.num_workers(); }

 private:
  void dispatch_ready_nodes();
  void dispatch_ready_nodes_locked();
  void worker_task(const NodeId& id,
                   std::chrono::steady_clock::time_point ready_at);
  void on_node_complete(const NodeId& id, const TaskResult& result);
  void finalize_and_collect();

  Dag dag_;
  std::map<std::string, TaskFn> tasks_;
  ConcurrentConfig config_;

  SchedulerState state_;
  ThreadPool pool_;
  ReadyQueue ready_queue_;

  // Dispatcher coordination. The main run() thread owns dispatching; worker
  // threads signal completion (and newly ready nodes) through these.
  std::mutex dispatch_mu_;
  std::condition_variable_any dispatch_cv_;
  std::atomic<std::size_t> in_flight_{0};

  std::mutex log_mu_;
  std::vector<ConcurrentNodeRun> log_;
  std::atomic<std::size_t> sequence_counter_{0};
  std::atomic<bool> run_started_{false};
  std::atomic<bool> run_finished_{false};
  std::atomic<bool> canceled_{false};

  std::chrono::steady_clock::time_point run_start_time_;
};

}  // namespace evo