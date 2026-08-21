#include "evo/distributed_run_loop.hpp"

#include <google/protobuf/util/time_util.h>

#include <algorithm>

#include "evo/execution.pb.h"
#include "evo/json.hpp"

namespace evo {

namespace {

using google::protobuf::util::TimeUtil;

execution::v1::ResourceClass to_proto_resource(ResourceClass rc) {
  switch (rc) {
    case ResourceClass::Browser:
      return execution::v1::BROWSER;
    case ResourceClass::ExternalIo:
      return execution::v1::EXTERNAL_IO;
    case ResourceClass::Internal:
    default:
      return execution::v1::INTERNAL;
  }
}

std::string truncate_detail(const std::string& s, std::size_t max = 500) {
  return s.size() <= max ? s : s.substr(0, max);
}

}  // namespace

std::string RunEvent::to_json_string() const {
  json::Object obj;
  obj["run_id"] = json::Value(run_id);
  obj["node_id"] = json::Value(node_id);
  obj["kind"] = json::Value(kind);
  obj["detail"] = json::Value(detail);
  obj["wall_ms"] = json::Value(static_cast<double>(wall_ms));
  return json::serialize(json::Value(std::move(obj)));
}

DistributedRunLoop::DistributedRunLoop(Dag dag, TaskTransport& transport,
                                       RunStore& store,
                                       DistributedRunConfig config,
                                       std::function<void(const RunEvent&)>
                                           on_event)
    : dag_(std::move(dag)),
      transport_(transport),
      store_(store),
      config_(std::move(config)),
      on_event_(std::move(on_event)),
      state_(dag_),
      policy_(config_.run_id) {}

DistributedRunLoop::~DistributedRunLoop() = default;

void DistributedRunLoop::stop() {
  stop_requested_.store(true, std::memory_order_relaxed);
  stop_source_.request_stop();
}

void DistributedRunLoop::cancel(const std::string& reason) {
  std::lock_guard lock(cancel_mu_);
  // Idempotent + terminal no-op (M30 step 8): only the FIRST request before
  // finalization takes effect. Repeats and Stop-after-terminal do nothing —
  // the original reason/timestamp/control message are preserved.
  if (finalized_.load(std::memory_order_relaxed)) return;
  if (cancel_requested_.load(std::memory_order_relaxed)) return;

  cancel_reason_ = reason;
  cancel_requested_at_ = now_wall_ms();
  cancel_requested_.store(true, std::memory_order_relaxed);

  // Durable first-write-wins stamp (M30 step 1). If the run row does not
  // exist yet (cancel raced run() startup), run() retries the stamp right
  // after creating the row.
  cancel_stamped_ =
      store_.mark_cancel_requested(config_.run_id, cancel_reason_,
                                   cancel_requested_at_);

  // Propagate to workers (M30 step 3): best-effort fan-out on the control
  // stream. The durable store + late-result rule are the backstop for any
  // worker that misses the message.
  publish_cancel_control();
}

void DistributedRunLoop::publish_cancel_control() {
  // Caller holds cancel_mu_. Publishes exactly once per loop instance.
  if (cancel_published_) return;
  cancel_published_ = true;

  execution::v1::ControlEnvelope env;
  env.set_kind(execution::v1::ControlEnvelope::CANCEL_RUN);
  env.set_run_id(config_.run_id);
  env.set_reason(cancel_reason_);
  *env.mutable_requested_at() =
      TimeUtil::MillisecondsToTimestamp(cancel_requested_at_);
  // Non-fatal on transport failure: workers that miss the message are still
  // bounded by the late-result rule once the run is terminal canceled.
  transport_.publish(control_stream_key(config_.env_prefix),
                     env.SerializeAsString());
}

void DistributedRunLoop::scan_expired_leases() {
  // Milestone 31 step 4: periodically scan for attempts whose lease expired
  // (worker lost / never claimed). Gated by lease_scan_interval; disabled when
  // lease_duration is 0. Uses wall-clock UTC for the durable comparison (the
  // store compares lease_expires_at against now_wall_ms), and steady_clock
  // only to pace the scan cadence.
  if (config_.lease_duration.count() <= 0) return;
  if (config_.lease_scan_interval.count() <= 0) return;

  const auto now_steady = std::chrono::steady_clock::now();
  if (last_lease_scan_.time_since_epoch().count() != 0 &&
      now_steady - last_lease_scan_ < config_.lease_scan_interval) {
    return;
  }
  last_lease_scan_ = now_steady;

  const auto expired =
      store_.scan_expired_attempt_leases(config_.run_id, now_wall_ms());
  for (const auto& lease : expired) {
    const NodeId node_id{lease.node_id};
    // At-most-once reap: only applied if the attempt is still running and held
    // by the recorded worker. A racing completion is never double-completed.
    const bool reaped = store_.mark_attempt_lease_expired(
        config_.run_id, lease.node_id, lease.attempt_number, lease.worker_id,
        now_wall_ms());
    if (!reaped) continue;

    // Recovery, NOT failure (M31 step 5): abandon the in-flight node back to
    // READY so dispatch_ready() re-dispatches it as a NEW attempt. No
    // successor is canceled and no dependency counter changes.
    const bool abandoned = state_.abandon_node(node_id);
    if (abandoned) {
      store_.set_node_status(config_.run_id, lease.node_id,
                             node_status::kReady);
      // Release the resource slot the abandoned attempt held (no result will
      // arrive to free it; otherwise the affinity capacity would leak).
      const ResourcePolicy pol = policy_for_node(node_id);
      if (!pol.affinity_key.empty()) {
        auto it = resource_usage_.find(pol.affinity_key);
        if (it != resource_usage_.end() && it->second > 0) it->second--;
      }
    }
    emit("node_lease_expired", &node_id,
         "worker=" + lease.worker_id + " attempt=" +
             std::to_string(lease.attempt_number));
  }
}

namespace {
// Deterministic per-(node, attempt) jitter seed: FNV-1a 64 of the node id,
// mixed with the attempt number and the run-level seed. Same inputs => same
// backoff jitter, so retry timing is reproducible in tests (M32 step 4).
std::uint64_t jitter_seed_for(const std::string& node_id, unsigned attempt,
                              std::uint64_t base_seed) {
  std::uint64_t h = 1469598103934665603ULL;  // FNV offset basis
  for (const char c : node_id) {
    h ^= static_cast<std::uint64_t>(static_cast<unsigned char>(c));
    h *= 1099511628211ULL;  // FNV prime
  }
  h ^= static_cast<std::uint64_t>(attempt) * 0x9E3779B97F4A7C15ULL;
  h ^= base_seed;
  return h == 0 ? 0xC0FFEEULL : h;
}
}  // namespace

bool DistributedRunLoop::handle_retryable_failure(
    const execution::v1::ResultEnvelope& result, std::int64_t finished_ms) {
  if (!config_.retries_enabled) return false;  // plain fail path

  const NodeId node_id{result.node_id()};
  const ResourcePolicy pol = policy_for_node(node_id);
  const RetryPolicy& policy = config_.retry_policies.for_class(pol.klass);

  const unsigned failed_attempt = result.attempt_number();
  const RetryDecision decision = decide_retry(
      policy, failed_attempt, result.error_class(), result.has_retryable(),
      result.retryable(),
      jitter_seed_for(node_id.value, failed_attempt, config_.retry_jitter_seed));

  if (decision.fail) {
    return false;  // caller runs the plain fail path (M26)
  }

  if (decision.dead_letter) {
    // Retries exhausted: terminal DEAD_LETTERED (M32 step 7). Persist the
    // terminal node state FIRST, then cancel downstream (same semantics as a
    // failed node). The resource slot is released by run() (applied==true).
    const bool applied = store_.complete_node_run(
        config_.run_id, node_id.value, node_status::kDeadLettered, "",
        decision.reason, finished_ms);
    if (!applied) return false;
    auto canceled = state_.dead_letter_node(node_id, decision.reason);
    emit("node_dead_lettered", &node_id, decision.reason);
    persist_canceled(canceled);
    return true;
  }

  // Retry: park the node in RETRY_WAIT with its backoff due-time (M32 step 5).
  // The wait is realized as a state + due-time, NOT by blocking any thread
  // (M32 step 6); process_retry_waits() re-readies it when the time elapses.
  const std::int64_t due_ms = finished_ms + decision.backoff.count();
  const bool parked = state_.retry_wait_node(node_id);
  if (!parked) return false;  // not Running anymore (e.g. raced cancel)
  store_.set_node_retry_wait(config_.run_id, node_id.value, due_ms,
                             decision.reason);
  retry_due_[node_id] = due_ms;
  emit("node_retry_scheduled", &node_id,
       decision.reason + " attempt=" + std::to_string(failed_attempt));
  return true;
}

void DistributedRunLoop::process_retry_waits() {
  if (retry_due_.empty()) return;
  // No new dispatch after a cancellation request (M30): leave parked nodes for
  // the cancel path to transition.
  if (cancel_requested_.load(std::memory_order_relaxed)) return;

  const std::int64_t now = now_wall_ms();
  std::vector<NodeId> due;
  for (const auto& [id, due_ms] : retry_due_) {
    if (due_ms <= now) due.push_back(id);
  }
  for (const auto& id : due) {
    retry_due_.erase(id);
    // RETRY_WAIT -> READY only if still parked (a racing cancel may have moved
    // it). dispatch_ready() then re-dispatches it as a NEW attempt.
    if (state_.ready_from_retry(id)) {
      store_.set_node_status(config_.run_id, id.value, node_status::kReady);
    }
  }
}

std::vector<RunEvent> DistributedRunLoop::events() const {
  std::lock_guard lock(events_mu_);
  return events_;
}

void DistributedRunLoop::emit(const std::string& kind, const NodeId* node,
                              const std::string& detail) {
  RunEvent ev;
  ev.run_id = config_.run_id;
  ev.node_id = node ? node->value : "";
  ev.kind = kind;
  ev.detail = truncate_detail(detail);
  ev.wall_ms = now_wall_ms();
  {
    std::lock_guard lock(events_mu_);
    events_.push_back(ev);
  }
  // Publish to the event stream for UI consumers (M26 step 7). Transport
  // failures here are non-fatal: the in-process callback + durable store are
  // the authoritative record.
  transport_.publish(event_stream_key(config_.env_prefix),
                     ev.to_json_string());
  if (on_event_) on_event_(ev);
}

ResourcePolicy DistributedRunLoop::policy_for_node(const NodeId& id) const {
  const NodeSpec* spec = dag_.node(id);
  if (!spec) return {};
  return policy_.policy_for(*spec);
}

void DistributedRunLoop::dispatch_ready() {
  // M30 no-go: no new task dispatches after a cancellation request. The run
  // loop also checks the flag before calling this, but dispatch can be
  // reached again within the same iteration after a result applied — the
  // guard here makes "no dispatch after cancel" unconditional.
  if (cancel_requested_.load(std::memory_order_relaxed)) return;

  for (const auto& id : state_.ready_nodes()) {
    // The ready snapshot can go stale mid-iteration (e.g. a same-loop
    // failure canceled downstream nodes); only Ready nodes are dispatched.
    if (state_.node_state(id) != NodeState::Ready) continue;

    const ResourcePolicy pol = policy_for_node(id);
    bool acquired = false;
    if (!pol.affinity_key.empty()) {
      const int in_use = resource_usage_[pol.affinity_key];
      if (in_use >= pol.capacity) {
        if (std::find(resource_blocked_.begin(), resource_blocked_.end(),
                      id) == resource_blocked_.end()) {
          resource_blocked_.push_back(id);
        }
        continue;
      }
      resource_usage_[pol.affinity_key]++;
      acquired = true;
    }

    const NodeSpec* spec = dag_.node(id);
    const unsigned attempt = ++current_attempt_[id];

    // Durable-before-dispatch: the attempt row exists before the envelope
    // hits the wire, so a crash after publish still has an audit record.
    state_.dispatch_node(id);
    state_.start_node(id);
    store_.set_node_status(config_.run_id, id.value, node_status::kDispatched);
    store_.record_attempt(config_.run_id, id.value, attempt,
                          /*worker_id=*/"", now_wall_ms());
    store_.set_node_status(config_.run_id, id.value, node_status::kRunning);
    // M31: stamp the initial lease deadline (covers queue-wait before a
    // worker claims). The worker takes over the lease via acquire/renew.
    // lease_initial_duration (deliberately generous) bounds the
    // dispatch->claim window; lease_duration bounds a claimed attempt.
    if (config_.lease_duration.count() > 0) {
      const std::chrono::milliseconds initial =
          config_.lease_initial_duration.count() > 0
              ? config_.lease_initial_duration
              : config_.lease_duration;
      store_.init_attempt_lease(config_.run_id, id.value, attempt,
                                now_wall_ms() + initial.count());
    }

    execution::v1::TaskEnvelope env;
    env.set_run_id(config_.run_id);
    env.set_workflow_version_id(config_.workflow_version_id);
    env.set_org_id(config_.org_id);
    env.set_node_id(id.value);
    env.set_attempt_number(attempt);
    env.set_resource_class(to_proto_resource(pol.klass));
    env.set_affinity_key(pol.affinity_key);
    env.set_node_type(spec ? spec->type : "");
    auto payload = config_.node_payloads.find(id.value);
    env.set_node_payload_json(payload != config_.node_payloads.end()
                                  ? payload->second
                                  : "{}");
    *env.mutable_became_ready_at() =
        TimeUtil::MillisecondsToTimestamp(now_wall_ms());

    const auto problems = validate_task_envelope(env);
    if (!problems.empty()) {
      // A locally-built envelope that fails validation is a bug; fail the
      // node rather than publishing an invalid task.
      if (acquired) resource_usage_[pol.affinity_key]--;
      store_.complete_node_run(config_.run_id, id.value, node_status::kFailed,
                               "", "invalid task envelope: " + problems.front(),
                               now_wall_ms());
      auto canceled = state_.fail_node(id, "invalid task envelope");
      persist_canceled(canceled);
      emit("node_failed", &id, "invalid task envelope: " + problems.front());
      continue;
    }

    const auto published = transport_.publish(
        task_stream_key(config_.env_prefix), env.SerializeAsString());
    if (!published) {
      // Transport rejected the dispatch. Fail the node (retry policy is M32;
      // M26 does not silently re-publish) and release the resource slot.
      if (acquired) resource_usage_[pol.affinity_key]--;
      store_.complete_node_run(config_.run_id, id.value, node_status::kFailed,
                               "", "task publish failed", now_wall_ms());
      auto canceled = state_.fail_node(id, "task publish failed");
      persist_canceled(canceled);
      emit("node_failed", &id, "task publish failed");
      continue;
    }
    emit("node_dispatched", &id, spec ? spec->type : "");
  }
}

void DistributedRunLoop::drain_resource_blocked() {
  if (resource_blocked_.empty()) return;
  std::vector<NodeId> still_blocked;
  for (const auto& id : resource_blocked_) {
    if (state_.node_state(id) != NodeState::Ready) continue;  // canceled etc.
    still_blocked.push_back(id);
  }
  resource_blocked_ = std::move(still_blocked);
  // Re-dispatch walks ready_nodes() again, which includes the blocked set
  // once capacity frees; nothing else to do here.
}

void DistributedRunLoop::persist_canceled(const std::vector<NodeId>& canceled) {
  for (const auto& id : canceled) {
    store_.complete_node_run(config_.run_id, id.value, node_status::kCanceled,
                             "", state_.failure_reason(id), now_wall_ms());
    emit("node_canceled", &id, state_.failure_reason(id));
  }
}

bool DistributedRunLoop::apply_result(
    const execution::v1::ResultEnvelope& result) {
  // 1. Validate identity before mutating durable state (M26 step 2).
  const auto problems = validate_result_envelope(result);
  if (!problems.empty()) return false;  // quarantine at the call site
  if (result.run_id() != config_.run_id) return false;  // not this run
  const NodeId node_id{result.node_id()};
  if (!dag_.node(node_id)) return false;  // unknown node

  // 2. Applicability: a result is only valid for a node that is currently
  // RUNNING (dispatched and awaiting its result). A result for a node that
  // has not been dispatched yet is premature; one for a terminal node is
  // late. Both are ignored — and crucially the dedupe key is NOT consumed,
  // so the legitimate result can still be applied later.
  const NodeState ns = state_.node_state(node_id);
  if (ns != NodeState::Running) return false;

  // 3. Dedupe by attempt id (M22): repeated results are ignored. This is the
  //    fast path for same-process redelivery (no durable round-trip).
  if (!dedupe_.first_time(attempt_key_of(result))) return false;

  // 4. Late-result rule (M22): an older attempt never overwrites newer
  // logical state (attempt-aware; retries are M32). Checked BEFORE the durable
  // claim so a forged/late result can never pollute the idempotency ledger.
  const unsigned current = current_attempt_.contains(node_id)
                               ? current_attempt_.at(node_id)
                               : 0;
  if (is_late_result(result, current, /*node_is_terminal=*/false)) {
    return false;
  }

  // 4b. Durable idempotency claim (M33 step 3): the in-memory dedupe above is
  //    lost on a scheduler restart, so the authoritative duplicate-suppression
  //    gate is the durable ledger. The logical operation key is derived from
  //    the attempt identity; the ledger's unique constraint makes a second
  //    claim a no-op. A duplicate delivery (e.g. redelivered after a restart)
  //    therefore never re-applies the result. The claim records the committed
  //    output for successes so a duplicate can reuse it (M33 step 4).
  if (!store_.claim_idempotency_key(result_idempotency_key(result),
                                    config_.run_id,
                                    result.completed() ? result.output() : "")) {
    return false;  // already applied (duplicate suppressed)
  }

  const std::int64_t finished_ms =
      result.finished_at().seconds() * 1000 +
      result.finished_at().nanos() / 1000000;

  if (result.completed()) {
    // 4. Persist durable success FIRST; unlock successors only when the
    //    store applied it (at-most-once). A duplicate success can therefore
    //    never decrement dependency counters twice (M26 steps 4-5).
    const bool applied = store_.complete_node_run(
        config_.run_id, node_id.value, node_status::kSucceeded,
        result.output(), "", finished_ms);
    if (!applied) return false;
    store_.finish_attempt(config_.run_id, node_id.value,
                          result.attempt_number(), result.worker_id(),
                          node_status::kSucceeded, "", finished_ms);
    auto newly_ready =
        state_.complete_node(node_id, TaskResult{true, result.output()});
    (void)newly_ready;  // dispatch_ready() picks them up next iteration
    emit("node_succeeded", &node_id, truncate_detail(result.output(), 200));
    return true;
  }

  // Failure path (M32): persist the failed attempt, then consult the retry
  // policy. A retryable failure parks the node in RETRY_WAIT (re-dispatched
  // after its backoff) or dead-letters it (attempt budget exhausted); a
  // non-retryable failure fails the node and cancels downstream (M26).
  store_.finish_attempt(config_.run_id, node_id.value, result.attempt_number(),
                        result.worker_id(), node_status::kFailed,
                        result.error(), finished_ms);

  if (handle_retryable_failure(result, finished_ms)) {
    return true;  // retried (RETRY_WAIT) or dead-lettered
  }

  // Non-retryable: fail the node + cancel downstream (M26 step 6).
  const bool applied = store_.complete_node_run(
      config_.run_id, node_id.value, node_status::kFailed, "", result.error(),
      finished_ms);
  if (!applied) return false;
  auto canceled = state_.complete_node(node_id, TaskResult{false, result.error()});
  emit("node_failed", &node_id, result.error());
  persist_canceled(canceled);
  return true;
}

void DistributedRunLoop::finalize_run(const std::string& status,
                                      const std::string& outcome) {
  store_.finish_run(config_.run_id, status, outcome, now_wall_ms());
  emit("run_finished", nullptr, outcome);
}

std::string DistributedRunLoop::run() {
  const std::string task_stream = task_stream_key(config_.env_prefix);
  const std::string result_stream = result_stream_key(config_.env_prefix);
  transport_.ensure_group(result_stream, config_.result_group);
  (void)task_stream;  // workers ensure their own task-stream group

  // Durable initial state (M26 step 3): run + one node_run per DAG node.
  store_.ensure_workflow(config_.workflow_id, config_.org_id,
                         "workflow " + config_.workflow_id);
  RunRecord run;
  run.run_id = config_.run_id;
  run.org_id = config_.org_id;
  run.workflow_id = config_.workflow_id;
  run.workflow_version_id = config_.workflow_version_id;
  run.engine = "evo";
  run.status = run_status::kRunning;
  store_.create_run(run, now_wall_ms());
  for (const auto& id : dag_.node_ids()) {
    const NodeSpec* spec = dag_.node(id);
    store_.create_node_run(config_.run_id, id.value, spec ? spec->type : "");
  }

  // If cancel() raced run() startup, the run row did not exist when the
  // request was stamped; retry the durable stamp now that it does.
  if (cancel_requested_.load(std::memory_order_relaxed)) {
    std::lock_guard lock(cancel_mu_);
    if (!cancel_stamped_) {
      cancel_stamped_ = store_.mark_cancel_requested(
          config_.run_id, cancel_reason_, cancel_requested_at_);
    }
  }

  state_.start_run();
  emit("run_started", nullptr, "");

  const auto deadline =
      config_.run_timeout.count() > 0
          ? std::optional<std::chrono::steady_clock::time_point>(
                std::chrono::steady_clock::now() + config_.run_timeout)
          : std::nullopt;

  while (!stop_requested_.load(std::memory_order_relaxed)) {
    // Cancellation (M30): apply once. Pending/blocked/ready nodes become
    // CANCELED; in-flight results that arrive later are ignored by the
    // late-result rule (node already terminal). No dispatch happens after
    // this point — the check precedes dispatch_ready() and dispatch_ready()
    // itself re-checks the flag.
    if (cancel_requested_.load(std::memory_order_relaxed)) {
      auto canceled = state_.cancel_run();
      persist_canceled(canceled);
      finalize_run(run_status::kCanceled, "canceled");
      finalized_.store(true, std::memory_order_relaxed);
      return run_status::kCanceled;
    }

    dispatch_ready();
    drain_resource_blocked();

    // Milestone 31: periodically reap expired attempt leases (lost workers).
    // Paced internally by lease_scan_interval; a no-op most iterations.
    scan_expired_leases();

    // Milestone 32: re-ready RETRY_WAIT nodes whose backoff has elapsed.
    // Cheap map scan; the backoff wait is a state + due-time, never a blocked
    // thread (M32 step 6).
    process_retry_waits();

    if (state_.all_nodes_terminal()) {
      state_.finalize_run();
      const RunState rs = state_.run_state();
      finalized_.store(true, std::memory_order_relaxed);
      if (rs == RunState::Succeeded) {
        finalize_run(run_status::kSucceeded, "succeeded");
        return run_status::kSucceeded;
      }
      if (rs == RunState::Canceled) {
        finalize_run(run_status::kCanceled, "canceled");
        return run_status::kCanceled;
      }
      finalize_run(run_status::kFailed, "failed");
      return run_status::kFailed;
    }

    if (deadline && std::chrono::steady_clock::now() >= *deadline) {
      // Bounded wait exceeded: cancel the run rather than hang.
      auto canceled = state_.cancel_run();
      persist_canceled(canceled);
      finalize_run(run_status::kCanceled, "timeout");
      finalized_.store(true, std::memory_order_relaxed);
      return run_status::kCanceled;
    }

    // Consume one result (blocking slice honors stop via stop_token).
    auto msg = transport_.read(result_stream, config_.result_group,
                               config_.consumer_id, config_.read_block_ms,
                               stop_source_.get_token());
    if (!msg) continue;

    execution::v1::ResultEnvelope result;
    if (!result.ParseFromString(msg->payload)) {
      // Malformed payload: quarantine + ack so it cannot poison the group.
      transport_.ack(result_stream, config_.result_group, msg->id);
      continue;
    }
    const bool applied = apply_result(result);
    // Ack applied AND ignored results alike: duplicates/late results are
    // consumed, never reprocessed.
    transport_.ack(result_stream, config_.result_group, msg->id);

    // Free the node's resource slot only when this result actually drove the
    // node to a terminal state (apply_result returns true exactly once per
    // node). Releasing on duplicates/late results would double-free capacity.
    if (applied) {
      const ResourcePolicy pol = policy_for_node(NodeId{result.node_id()});
      if (!pol.affinity_key.empty()) {
        auto it = resource_usage_.find(pol.affinity_key);
        if (it != resource_usage_.end() && it->second > 0) it->second--;
      }
    }
  }

  // Stop requested mid-run: leave in-flight work for redelivery; mark the
  // run canceled so durable state is terminal and consistent.
  auto canceled = state_.cancel_run();
  persist_canceled(canceled);
  finalize_run(run_status::kCanceled, "stopped");
  finalized_.store(true, std::memory_order_relaxed);
  return run_status::kCanceled;
}

}  // namespace evo
