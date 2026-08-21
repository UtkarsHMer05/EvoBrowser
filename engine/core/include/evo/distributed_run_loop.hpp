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
#include "evo/retry_policy.hpp"
#include "evo/run_store.hpp"
#include "evo/state_machine.hpp"
#include "evo/transport.hpp"

namespace evo {

// Normalized run event for UI consumers (M26 step 7). `kind` is one of:
//   run_started, node_dispatched, node_succeeded, node_failed,
//   node_canceled, run_finished, node_lease_expired (M31),
//   node_retry_scheduled, node_dead_lettered (M32)
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

  // --- Task leases (Milestone 31) ---
  // Lease duration a WORKER is expected to hold/renew while an attempt runs.
  // The worker acquires with this duration on claim and renews it while work
  // legitimately runs. 0 => lease monitoring disabled.
  std::chrono::milliseconds lease_duration{30000};
  // Queue-wait lease stamped at dispatch, BEFORE any worker claims. Covers the
  // dispatch->claim window; deliberately generous so a live-but-slow-to-claim
  // worker is not reaped before it acquires. Once a worker acquires, it resets
  // the expiry to lease_duration. Defaults to lease_duration.
  std::chrono::milliseconds lease_initial_duration{0};  // 0 => use lease_duration
  // How often the loop scans for expired leases (steady clock). 0 disables
  // scanning even when lease_duration > 0.
  std::chrono::milliseconds lease_scan_interval{5000};

  // --- Node-level retries (Milestone 32) ---
  // Retry policies by resource class. Defaults: internal 3 attempts, browser
  // 2 attempts, external_io 1 attempt (no retry — side effects need an
  // idempotency strategy first). Set `enabled=false` to disable retries
  // entirely (every retryable failure then fails the node immediately).
  RetryPolicySet retry_policies = RetryPolicySet::defaults();
  bool retries_enabled = true;
  // Seed for the deterministic backoff jitter (M32 step 4). The per-node seed
  // is derived from this + the node id, so tests are reproducible.
  std::uint64_t retry_jitter_seed = 0xC0FFEE;

  // --- Scheduler restart recovery (Milestone 35) ---
  // When true, run() RECONSTRUCTS the run's logical state from the durable
  // store (node statuses, dependency counters, attempt numbers, retry waits)
  // instead of starting fresh, and drains this consumer's pending result
  // messages so no result is lost across the restart. The caller must supply
  // the same Dag (parsed from the persisted dag_json) and the same
  // result_group/consumer_id the pre-crash loop used, so the pending-entry
  // list (PEL) is reclaimed by the same consumer. Default false = fresh run.
  bool resume = false;
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
  // Milestone 31: scan for expired attempt leases and transition them to
  // lease_expired (a recoverable state — the node is re-dispatched as a new
  // attempt, NOT failed). Called periodically from run().
  void scan_expired_leases();
  // Milestone 32: move RETRY_WAIT nodes whose backoff has elapsed back to
  // READY so dispatch_ready() re-dispatches them as a new attempt. Called
  // every loop iteration (cheap map scan; no blocking, no worker-thread wait).
  void process_retry_waits();
  // Milestone 32: apply the retry policy to a failed attempt. Returns true if
  // the node was parked for retry (RETRY_WAIT) or dead-lettered, false if the
  // caller should run the plain fail path.
  bool handle_retryable_failure(const execution::v1::ResultEnvelope& result,
                                std::int64_t finished_ms);

  // Milestone 35 (scheduler restart recovery): reconstruct the run's logical
  // state from the durable store. Returns true when the run had durable state
  // and was reconstructed (the caller then resumes the main loop); false when
  // there was nothing to reconstruct (run row or node rows absent), in which
  // case the caller falls back to fresh-run initialization. On success this
  // also restores attempt numbers, retry due-times, in-flight resource slots,
  // any durable cancellation request, and drains this consumer's pending
  // result messages so no result is lost across the restart.
  bool reconstruct_from_store();
  // Drain this consumer's pending (delivered, unacked) result messages by
  // read_pending -> apply_result -> ack. Called once on resume before the main
  // loop, so results the pre-crash loop read but did not ack are still applied.
  void drain_pending_results();

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

  // Milestone 31: steady-clock instant of the last expired-lease scan
  // (loop-thread only). Zero => never scanned.
  std::chrono::steady_clock::time_point last_lease_scan_{};

  // Milestone 32: RETRY_WAIT bookkeeping (loop-thread only). node -> wall-clock
  // UTC ms when its backoff elapses. The failed attempt number comes from the
  // result envelope; the per-node jitter seed is derived from
  // config_.retry_jitter_seed + node id + attempt number. A node in RETRY_WAIT
  // is not terminal, so all_nodes_terminal() keeps the run alive until its
  // backoff elapses and it is re-dispatched.
  std::map<NodeId, std::int64_t> retry_due_;

  mutable std::mutex events_mu_;
  std::vector<RunEvent> events_;
};

}  // namespace evo
