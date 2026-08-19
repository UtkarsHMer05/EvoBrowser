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

  // 3. Dedupe by attempt id (M22): repeated results are ignored.
  if (!dedupe_.first_time(attempt_key_of(result))) return false;

  // 4. Late-result rule (M22): an older attempt never overwrites newer
  // logical state (attempt-aware; retries are M32).
  const unsigned current = current_attempt_.contains(node_id)
                               ? current_attempt_.at(node_id)
                               : 0;
  if (is_late_result(result, current, /*node_is_terminal=*/false)) {
    return false;
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

  // Failure: persist details (M26 step 6), then propagate cancellation to
  // downstream nodes. Retry policy is M32 — M26 records the hint's inputs
  // (error_class/retryable are already in the durable attempt/result stream)
  // but does not re-dispatch.
  const bool applied = store_.complete_node_run(
      config_.run_id, node_id.value, node_status::kFailed, "", result.error(),
      finished_ms);
  if (!applied) return false;
  store_.finish_attempt(config_.run_id, node_id.value, result.attempt_number(),
                        result.worker_id(), node_status::kFailed,
                        result.error(), finished_ms);
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
