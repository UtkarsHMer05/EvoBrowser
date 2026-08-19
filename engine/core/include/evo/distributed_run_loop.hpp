#pragma once

// Distributed run loop (Milestone 26): completes the
// scheduler -> worker -> result -> successor cycle.
//
// One loop instance drives ONE run against a durable transport (Redis Streams
// in production, InMemoryTransport in tests) and a durable RunStore
// (PgRunStore in production, InMemoryRunStore in tests):
//
//   1. Persist the run + one node_run row per DAG node (durable initial state).
//   2. Dispatch ready nodes as validated TaskEnvelopes on the task stream,
//      recording each attempt durably before dispatch.
//   3. Consume ResultEnvelopes from the result stream:
//        - validate identity (run/node membership, attempt) before applying,
//        - dedupe by attempt id (ResultDedupe, M22),
//        - ignore late results (is_late_result, M22),
//        - persist the terminal node state FIRST; only when the store applied
//          it (at-most-once) does the loop unlock successor dependency
//          counters. A duplicate successful result therefore can never
//          decrement successors twice.
//        - failures persist their details and cancel downstream nodes; the
//          retry policy itself is M32 (the loop hands the hint through but
//          does not re-dispatch in M26).
//   4. Publish normalized run events (JSON) on the event stream for UI
//      consumers (M28 wires the frontend fan-out) and to an optional
//      in-process callback.
//   5. When every node is terminal, persist the run's terminal status +
//      outcome and return.
//
// Threading/ownership: the loop is single-threaded (run() drives dispatch and
// result consumption on the calling thread). All mutable scheduling state is
// owned by that thread; the transport and RunStore are thread-safe objects
// shared by reference. cancel()/stop() are the only cross-thread entry points
// (atomics + stop_source). Shutdown while waiting on the transport is honored
// via std::stop_token on the blocking read — no busy-spin.
//
// Timestamps: wall-clock UTC milliseconds (system_clock) for everything
// persisted or emitted in events; steady_clock is used only for internal
// deadline bookkeeping and is never persisted.

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <stop_token>
#include <string>
#include <vector>

#include "evo/dag.hpp"
#include "evo/envelope.hpp"
#include "evo/execution_policy.hpp"
#include "evo/run_store.hpp"
#include "evo/state_machine.hpp"
#include "evo/transport.hpp"

namespace evo {

// Normalized run event for UI consumers (M26 step 7). `kind` is one of:
//   run_started, node_dispatched, node_succeeded, node_failed,
//   node_canceled, run_finished
struct RunEvent {
  std::string run_id;
  std::string node_id;  // empty for run-level events
  std::string kind;
  std::string detail;   // opaque (error message / truncated output)
  std::int64_t wall_ms = 0;

  std::string to_json_string() const;
};

struct DistributedRunConfig {
  std::string run_id;
  std::string org_id;
  std::string workflow_id;
  std::string workflow_version_id;  // empty => NULL in the store

  // Stream namespace prefix (e.g. "evo:dev").
  std::string env_prefix = "evo:dev";
  // Consumer group the scheduler uses to read the RESULT stream. Workers use
  // their own group on the task stream; the scheduler owns the result group.
  std::string result_group = "scheduler";
  std::string consumer_id = "scheduler-1";

  // Per-node payload JSON carried in the TaskEnvelope (synthetic specs in
  // tests; node `values` in production). Empty => "{}".
  std::map<std::string, std::string> node_payloads;

  // Blocking-read slice for the result stream; also the stop-latency
  // granularity.
  std::chrono::milliseconds read_block_ms{100};

  // Overall run deadline (steady clock) to bound tests; 0 => no deadline.
  std::chrono::milliseconds run_timeout{0};
};

// Wall-clock UTC milliseconds since the Unix epoch — see run_store.hpp.

class DistributedRunLoop {
 public:
  // `transport` and `store` are borrowed (caller-owned) and must outlive the
  // loop. `on_event` (optional) receives every normalized event in order.
  DistributedRunLoop(Dag dag, TaskTransport& transport, RunStore& store,
                     DistributedRunConfig config,
                     std::function<void(const RunEvent&)> on_event = nullptr);
  ~DistributedRunLoop();

  DistributedRunLoop(const DistributedRunLoop&) = delete;
  DistributedRunLoop& operator=(const DistributedRunLoop&) = delete;

  // Drive the run to a terminal state. Returns the final run status string
  // (run_status::*). Blocks until every node is terminal, the run is
  // canceled, or stop() is requested.
  std::string run();

  // Request shutdown: stop dispatching and consuming; in-flight worker tasks
  // are left for redelivery (their results, if any, arrive too late for this
  // loop instance). Idempotent; safe from any thread.
  void stop();

  // Request run cancellation (Milestone 30 end-to-end cancel):
  //   - idempotent: only the FIRST request takes effect (reason + timestamp
  //     are preserved; repeats are no-ops),
  //   - durable: stamps cancel_requested_at on the run row via the store
  //     (first-write-wins; if the row does not exist yet, run() stamps it
  //     right after creating it),
  //   - propagated: publishes a CANCEL_RUN ControlEnvelope on the control
  //     stream so workers abort in-flight attempts and short-circuit queued
  //     tasks for this run (best-effort fan-out; the durable store + the
  //     late-result rule are the backstop for workers that miss it),
  //   - terminal no-op: once the run has finalized, cancel() does nothing.
  // Safe from any thread.
  void cancel(const std::string& reason);

  const SchedulerState& state() const { return state_; }
  const DistributedRunConfig& config() const { return config_; }

  // Events emitted so far (test/observability helper; in emission order).
  std::vector<RunEvent> events() const;

 private:
  // Dispatch every READY node whose resource capacity permits.
  void dispatch_ready();
  // Drain resource-blocked nodes after a completion freed capacity.
  void drain_resource_blocked();
  ResourcePolicy policy_for_node(const NodeId& id) const;

  // Apply one decoded result. Returns true if it was applied (not a
  // duplicate/late/unknown result).
  bool apply_result(const execution::v1::ResultEnvelope& result);

  void persist_canceled(const std::vector<NodeId>& canceled);
  // Publish the CANCEL_RUN control message exactly once (M30). Best-effort:
  // a transport failure is logged via events but never blocks cancellation.
  void publish_cancel_control();
  void emit(const std::string& kind, const NodeId* node,
            const std::string& detail);
  void finalize_run(const std::string& status, const std::string& outcome);

  Dag dag_;
  TaskTransport& transport_;
  RunStore& store_;
  DistributedRunConfig config_;
  std::function<void(const RunEvent&)> on_event_;

  SchedulerState state_;
  ExecutionPolicy policy_;
  ResultDedupe dedupe_;

  std::stop_source stop_source_;
  std::atomic<bool> stop_requested_{false};

  // Cancellation state (M30). cancel() may be called from any thread; the
  // mutex guards the first-request-wins bookkeeping (reason + timestamp +
  // durable stamp + control publish, each exactly once). cancel_requested_ is
  // the hot flag the run loop polls.
  mutable std::mutex cancel_mu_;
  std::atomic<bool> cancel_requested_{false};
  std::string cancel_reason_;
  std::int64_t cancel_requested_at_ = 0;  // wall-clock UTC ms of first request
  bool cancel_published_ = false;         // control message published once
  bool cancel_stamped_ = false;           // durable timestamp stamped once
  std::atomic<bool> finalized_{false};    // run reached a terminal state

  // Resource accounting (M12 semantics in distributed mode), loop-thread only.
  std::map<std::string, int> resource_usage_;
  std::vector<NodeId> resource_blocked_;

  // Current attempt number per node (loop-thread only; M26 dispatches once
  // per node — retries are M32 — but the bookkeeping is attempt-aware).
  std::map<NodeId, unsigned> current_attempt_;

  mutable std::mutex events_mu_;
  std::vector<RunEvent> events_;
};

}  // namespace evo
