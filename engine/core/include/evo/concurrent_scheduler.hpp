#pragma once

// Concurrent dependency-aware DAG scheduler (Milestone 10) with cooperative
// cancellation (Milestone 11), resource-aware dispatch (Milestone 12), and
// evidence-grade metrics instrumentation (Milestone 13).
//
// Combines the thread pool (M09), ready queue (M08), and thread-safe state
// machine (M07) into a scheduler that executes independent branches in parallel
// while respecting dependencies. Produces a RunLog with per-node
// ready/start/finish timestamps (steady_clock) for equivalence testing against
// the sequential reference scheduler and for concurrency overlap verification.
//
// Cancellation (M11): cancel() requests stop on an internal stop_source and
// commits the run to a terminal CANCELED state. The dispatcher stops issuing
// new work immediately; already-running tasks observe the stop_token (when
// registered via the ConcurrentTaskFn overload) and return cooperatively.
// Pending/blocked nodes are marked CANCELED by the state machine. Cancellation
// request and run-terminal timestamps are recorded for latency metrics.
//
// Resource gating (M12): dispatch is additionally gated on resource capacity
// independent of dependency readiness. The default ExecutionPolicy serializes
// all browser-class nodes in a run on one capacity-1 browser-affinity resource
// so they never run concurrently against the same browser session.
//
// Metrics (M13): run-level counters and high-water marks (dispatch/completion
// counts, max in-flight, max queue depth, retry placeholder) are collected on
// the hot path and exported as structured JSON for benchmark harnesses.

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
#include "evo/execution_policy.hpp"
#include "evo/metrics.hpp"
#include "evo/ready_queue.hpp"
#include "evo/scheduler.hpp"
#include "evo/state_machine.hpp"
#include "evo/thread_pool.hpp"

namespace evo {

// A task that can observe cooperative cancellation via a stop_token. Used by
// the concurrent scheduler's stop_token-aware execution path (M11). Synthetic
// bench tasks provide cooperative variants so cancellation tests can verify
// in-flight tasks abort promptly rather than running to completion.
using ConcurrentTaskFn =
    std::function<TaskResult(const NodeSpec&, std::stop_token)>;

// Extended run record that includes the moment a node became READY (in addition
// to the started_at/finished_at from the sequential scheduler's NodeRun).
struct ConcurrentNodeRun {
  NodeId id;
  std::string type;
  std::size_t sequence;  // order of logical completion (not dispatch order)
  std::chrono::steady_clock::time_point became_ready_at;  // deps satisfied
  std::chrono::steady_clock::time_point ready_at;         // popped for dispatch
  std::chrono::steady_clock::time_point started_at;       // worker began exec
  std::chrono::steady_clock::time_point finished_at;      // worker finished
  TaskResult result;

  bool ok() const { return result.completed; }
  std::chrono::nanoseconds duration() const { return finished_at - started_at; }
  std::chrono::nanoseconds queue_latency() const { return started_at - ready_at; }
  std::chrono::nanoseconds ready_to_dispatch_latency() const {
    return ready_at - became_ready_at;
  }
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
  // Produces the same field names as the sequential RunLog plus readiness
  // timestamps (M13).
  std::string to_json_string() const;
};

// Configuration for the concurrent scheduler.
struct ConcurrentConfig {
  // Number of worker threads (must be > 0).
  std::size_t num_workers = 4;

  // Maximum number of ready nodes to keep in the dispatch queue (0 = unbounded).
  std::size_t ready_queue_capacity = 0;

  // Run identifier used to derive the default browser-affinity key (M12). All
  // browser-class nodes in one run serialize on that capacity-1 resource.
  std::string run_id = "default-run";
};

class ConcurrentScheduler {
 public:
  // Takes ownership of the Dag and task registry. The scheduler is single-use:
  // call run() once. The provided tasks must be thread-safe (executed from
  // multiple pool threads concurrently).
  //
  // This overload accepts the plain TaskFn (no stop_token); tasks run
  // oblivious to cancellation and are only prevented from *starting* after a
  // cancel — already-running tasks run to completion.
  ConcurrentScheduler(Dag dag, std::map<std::string, TaskFn> tasks,
                      ConcurrentConfig config = {});

  // Overload accepting stop_token-aware tasks (M11). Tasks receive the run's
  // stop_token and may abort cooperatively when cancellation is requested.
  ConcurrentScheduler(Dag dag, std::map<std::string, ConcurrentTaskFn> tasks,
                      ConcurrentConfig config = {});

  ~ConcurrentScheduler();

  // Run the entire DAG concurrently. Blocks until all nodes reach a terminal
  // state (all succeeded, any failed, or run canceled). Returns the complete
  // run log with per-node timestamps. If cancel() was called before run(),
  // returns immediately with an empty (canceled) log.
  ConcurrentRunLog run();

  // Request cooperative cancellation. No new nodes will be dispatched; already
  // running tasks observe the stop_token (when registered via the
  // ConcurrentTaskFn overload) and may abort early. Idempotent. Safe to call
  // before run() (the run then starts already-canceled) or from any thread.
  void cancel();

  // Wait for the run to complete (if run() was started asynchronously) or
  // return immediately if already terminal. The ConcurrentScheduler in M10/M11
  // is synchronous — run() blocks — so this is a no-op for now but reserved for
  // future async execution modes.
  void wait();

  const Dag& dag() const { return state_.dag(); }
  std::size_t num_workers() const { return pool_.num_workers(); }

  // Mutable access to the execution policy for setup before run() (e.g. tests
  // set per-node affinity overrides). Do not mutate during run().
  ExecutionPolicy& policy() { return policy_; }
  const ExecutionPolicy& policy() const { return policy_; }

  // True once cancel() has been invoked (run is committed to termination).
  bool is_canceled() const { return canceled_.load(std::memory_order_relaxed); }

  // Timestamps for cancellation-latency metrics (steady_clock). Zero if the
  // corresponding event has not occurred.
  std::chrono::steady_clock::time_point cancel_requested_at() const {
    return cancel_requested_at_;
  }
  std::chrono::steady_clock::time_point run_terminal_at() const {
    return run_terminal_at_;
  }

  // Aggregate run metrics (M13). Call after run() completes.
  const RunMetrics& metrics() const { return metrics_; }

 private:
  void init();
  void dispatch_ready_nodes_locked();
  // Re-attempt resource-blocked nodes once a capacity slot frees (M12).
  void drain_resource_blocked();
  void worker_task(const NodeId& id,
                   std::chrono::steady_clock::time_point ready_at);
  void on_node_complete(const NodeId& id, const TaskResult& result);
  void finalize_and_collect();

  // Resolve the resource policy for a node (see ExecutionPolicy).
  ResourcePolicy policy_for_node(const NodeId& id) const;

  // Update high-water marks (under dispatch_mu_).
  void update_high_water();

  Dag dag_;
  std::map<std::string, ConcurrentTaskFn> tasks_;
  ConcurrentConfig config_;
  ExecutionPolicy policy_;

  SchedulerState state_;
  ThreadPool pool_;
  ReadyQueue ready_queue_;
  std::stop_source stop_source_;  // shared cancellation signal for in-flight tasks

  // Dispatcher coordination. The main run() thread owns dispatching; worker
  // threads signal completion (and newly ready nodes) through these.
  std::mutex dispatch_mu_;
  std::condition_variable_any dispatch_cv_;
  std::atomic<std::size_t> in_flight_{0};

  // Resource accounting (M12), guarded by dispatch_mu_.
  std::map<std::string, int> resource_usage_;  // affinity_key -> in-use count
  std::vector<NodeId> resource_blocked_;        // ready but waiting for capacity

  // Per-node "became ready" timestamps (when deps were satisfied), guarded by
  // log_mu_ (written from workers via on_node_complete, read in finalize).
  std::map<NodeId, std::chrono::steady_clock::time_point> ready_times_;

  std::mutex log_mu_;
  std::vector<ConcurrentNodeRun> log_;
  std::atomic<std::size_t> sequence_counter_{0};
  std::atomic<bool> run_started_{false};
  std::atomic<bool> run_finished_{false};
  std::atomic<bool> canceled_{false};

  std::chrono::steady_clock::time_point run_start_time_;
  std::chrono::steady_clock::time_point cancel_requested_at_;
  std::chrono::steady_clock::time_point run_terminal_at_;

  // Run-level counters/metrics (M13). dispatch_count/completion_count are
  // atomic (updated from multiple threads); high-water marks are updated under
  // dispatch_mu_.
  std::atomic<std::size_t> dispatch_count_{0};
  std::atomic<std::size_t> completion_count_{0};
  std::size_t max_in_flight_ = 0;
  std::size_t max_queue_depth_ = 0;

  RunMetrics metrics_;
};

}  // namespace evo