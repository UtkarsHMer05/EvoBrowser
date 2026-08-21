// M26 unit tests: distributed run loop over the in-memory transport + store.
//
// A fake worker thread consumes TaskEnvelopes from the task stream and
// publishes ResultEnvelopes to the result stream (mirroring the TS worker's
// durable-handoff behavior). No Redis, no Postgres — pure scheduler-core.
//
// Covers:
//   1. Diamond DAG end-to-end: dispatch -> execute -> persist -> unlock
//      successors -> terminal run, with durable audit assertions.
//   2. Duplicate result injection: successors unlocked at most once.
//   3. Unknown-node / wrong-run results ignored (identity validation).
//   4. Failure path: details persisted, downstream canceled, no unlock.
//   5. Malformed result payload quarantined without poisoning the run.
//   6. Cancellation mid-run: terminal canceled state, durable + events.
//   7. Normalized run events emitted in order.

#include <google/protobuf/util/time_util.h>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <map>
#include <string>
#include <thread>
#include <vector>

#include "evo/dag.hpp"
#include "evo/distributed_run_loop.hpp"
#include "evo/execution.pb.h"
#include "evo/run_store.hpp"
#include "evo/transport.hpp"

using evo::Dag;
using evo::DistributedRunConfig;
using evo::DistributedRunLoop;
using evo::Edge;
using evo::InMemoryRunStore;
using evo::InMemoryTransport;
using evo::NodeKind;
using evo::NodeId;
using evo::NodeSpec;
using namespace std::chrono_literals;

namespace {

int failures = 0;
void check(bool cond, const char* label) {
  if (cond) {
    printf("  ok   %s\n", label);
  } else {
    printf("  FAIL %s\n", label);
    ++failures;
  }
}

// Build the diamond: start -> {a, b} -> c
Dag make_diamond() {
  std::vector<NodeSpec> nodes = {
      {NodeId{"start"}, NodeKind::Trigger, "start"},
      {NodeId{"a"}, NodeKind::Action, "bench:echo"},
      {NodeId{"b"}, NodeKind::Action, "bench:echo"},
      {NodeId{"c"}, NodeKind::Action, "bench:echo"},
  };
  std::vector<Edge> edges = {
      {NodeId{"start"}, NodeId{"a"}},
      {NodeId{"start"}, NodeId{"b"}},
      {NodeId{"a"}, NodeId{"c"}},
      {NodeId{"b"}, NodeId{"c"}},
  };
  auto br = Dag::build(nodes, edges);
  return std::move(*br.dag);
}

// M34: a browser-affinity chain used to prove the browser resource-loss policy
// under worker crash. b1 is the TRIGGER (the only initially-ready node, so a
// killed worker deterministically claims it) and BOTH b1 and b2 are browser
// node types ("open-url"), so ExecutionPolicy gives them the SAME capacity-1
// affinity key (the run id). The test proves: when the owning worker dies, the
// single affinity slot is RELEASED on lease reap (not leaked), the node is
// re-dispatched as a new attempt on a fresh session, and the chain then
// continues to b2 on the same capacity-1 key. This is the documented policy:
// browser resource treated as lost + re-created, never transparent session
// continuation, never a permanently leaked slot.
Dag make_browser_chain() {
  std::vector<NodeSpec> nodes = {
      {NodeId{"b1"}, NodeKind::Trigger, "open-url"},
      {NodeId{"b2"}, NodeKind::Action, "open-url"},
  };
  std::vector<Edge> edges = {
      {NodeId{"b1"}, NodeId{"b2"}},
  };
  auto br = Dag::build(nodes, edges);
  return std::move(*br.dag);
}

std::string encode_success(const std::string& run_id, const std::string& node,
                           unsigned attempt, const std::string& output) {
  evo::execution::v1::ResultEnvelope env;
  env.set_run_id(run_id);
  env.set_node_id(node);
  env.set_attempt_number(attempt);
  env.set_completed(true);
  env.set_output(output);
  env.set_status(evo::execution::v1::ResultEnvelope::OK);
  *env.mutable_finished_at() =
      google::protobuf::util::TimeUtil::MillisecondsToTimestamp(
          evo::now_wall_ms());
  return env.SerializeAsString();
}

std::string encode_failure(const std::string& run_id, const std::string& node,
                           unsigned attempt, const std::string& error) {
  evo::execution::v1::ResultEnvelope env;
  env.set_run_id(run_id);
  env.set_node_id(node);
  env.set_attempt_number(attempt);
  env.set_completed(false);
  env.set_error(error);
  env.set_status(evo::execution::v1::ResultEnvelope::NODE_FAILED);
  env.set_error_class(evo::execution::v1::ERROR_PERMANENT);
  *env.mutable_finished_at() =
      google::protobuf::util::TimeUtil::MillisecondsToTimestamp(
          evo::now_wall_ms());
  return env.SerializeAsString();
}

// Fake worker: reads task envelopes, publishes a result per task. `fail_node`
// (if non-empty) makes that node fail instead of succeed.
class FakeWorker {
 public:
  FakeWorker(InMemoryTransport& t, std::string prefix, std::string run_id,
             std::string fail_node = "")
      : transport_(t),
        prefix_(std::move(prefix)),
        run_id_(std::move(run_id)),
        fail_node_(std::move(fail_node)) {}

  void start() {
    thread_ = std::jthread([this](std::stop_token st) { this->loop(st); });
  }
  void stop() {
    thread_.request_stop();
    if (thread_.joinable()) thread_.join();
  }
  std::size_t executed() const { return executed_.load(); }

 private:
  void loop(std::stop_token st) {
    const std::string tasks = evo::task_stream_key(prefix_);
    const std::string results = evo::result_stream_key(prefix_);
    transport_.ensure_group(tasks, "workers");
    while (!st.stop_requested()) {
      auto msg = transport_.read(tasks, "workers", "fake-worker", 50ms, st);
      if (!msg) continue;
      evo::execution::v1::TaskEnvelope task;
      if (!task.ParseFromString(msg->payload)) {
        transport_.ack(tasks, "workers", msg->id);
        continue;
      }
      executed_.fetch_add(1);
      std::string result;
      if (task.node_id() == fail_node_) {
        result = encode_failure(run_id_, task.node_id(), task.attempt_number(),
                                "synthetic failure");
      } else {
        result = encode_success(run_id_, task.node_id(), task.attempt_number(),
                                "{\"ok\":true,\"node\":\"" + task.node_id() +
                                    "\"}");
      }
      // Durable handoff: publish result, then ack the task.
      transport_.publish(results, result);
      transport_.ack(tasks, "workers", msg->id);
    }
  }

  InMemoryTransport& transport_;
  std::string prefix_;
  std::string run_id_;
  std::string fail_node_;
  std::jthread thread_;
  std::atomic<std::size_t> executed_{0};
};

// M31: lease-aware fake worker. On claim it ACQUIRES the attempt lease from the
// store (like the real TS worker). Two modes:
//   renew=true  -> renews the lease periodically while "working" (a slow but
//                  ALIVE worker; the scheduler must NOT reap it).
//   renew=false -> acquires once, then never renews (a KILLED worker; the
//                  scheduler must reap the attempt after the lease expires).
// `work_ms` simulates how long the task takes before publishing a result.
class LeaseWorker {
 public:
  LeaseWorker(InMemoryTransport& t, evo::RunStore& store, std::string prefix,
              std::string run_id, std::string worker_id, bool renew,
              std::chrono::milliseconds work_ms,
              std::chrono::milliseconds lease_duration,
              std::chrono::milliseconds renew_interval)
      : transport_(t),
        store_(store),
        prefix_(std::move(prefix)),
        run_id_(std::move(run_id)),
        worker_id_(std::move(worker_id)),
        renew_(renew),
        work_ms_(work_ms),
        lease_duration_(lease_duration),
        renew_interval_(renew_interval) {}

  void start() {
    thread_ = std::jthread([this](std::stop_token st) { this->loop(st); });
  }
  void stop() {
    thread_.request_stop();
    if (thread_.joinable()) thread_.join();
  }
  std::size_t executed() const { return executed_.load(); }

 private:
  void loop(std::stop_token st) {
    const std::string tasks = evo::task_stream_key(prefix_);
    const std::string results = evo::result_stream_key(prefix_);
    transport_.ensure_group(tasks, "workers");
    while (!st.stop_requested()) {
      auto msg = transport_.read(tasks, "workers", worker_id_, 20ms, st);
      if (!msg) continue;
      evo::execution::v1::TaskEnvelope task;
      if (!task.ParseFromString(msg->payload)) {
        transport_.ack(tasks, "workers", msg->id);
        continue;
      }
      executed_.fetch_add(1);

      // Acquire the lease (take over the queue-wait lease the scheduler init'd).
      const std::int64_t now = evo::now_wall_ms();
      store_.acquire_attempt_lease(run_id_, task.node_id(),
                                   task.attempt_number(), worker_id_, now,
                                   now + lease_duration_.count());

      // "Work" for work_ms, renewing the lease every renew_interval if alive.
      const auto deadline = std::chrono::steady_clock::now() + work_ms_;
      auto next_renew = std::chrono::steady_clock::now() + renew_interval_;
      while (std::chrono::steady_clock::now() < deadline &&
             !st.stop_requested()) {
        if (renew_ && std::chrono::steady_clock::now() >= next_renew) {
          const std::int64_t rnow = evo::now_wall_ms();
          store_.renew_attempt_lease(run_id_, task.node_id(),
                                     task.attempt_number(), worker_id_, rnow,
                                     rnow + lease_duration_.count());
          next_renew = std::chrono::steady_clock::now() + renew_interval_;
        }
        std::this_thread::sleep_for(5ms);
      }
      if (st.stop_requested()) return;  // killed mid-work: no result, no renew

      const std::string result = encode_success(
          run_id_, task.node_id(), task.attempt_number(),
          "{\"ok\":true,\"node\":\"" + task.node_id() + "\"}");
      transport_.publish(results, result);
      transport_.ack(tasks, "workers", msg->id);
    }
  }

  InMemoryTransport& transport_;
  evo::RunStore& store_;
  std::string prefix_;
  std::string run_id_;
  std::string worker_id_;
  bool renew_;
  std::chrono::milliseconds work_ms_;
  std::chrono::milliseconds lease_duration_;
  std::chrono::milliseconds renew_interval_;
  std::jthread thread_;
  std::atomic<std::size_t> executed_{0};
};

// Encode a TRANSIENT failure result (retryable by default). M32 tests use this
// to drive the retry path.
std::string encode_transient_failure(const std::string& run_id,
                                     const std::string& node, unsigned attempt,
                                     const std::string& error) {
  evo::execution::v1::ResultEnvelope env;
  env.set_run_id(run_id);
  env.set_node_id(node);
  env.set_attempt_number(attempt);
  env.set_completed(false);
  env.set_error(error);
  env.set_status(evo::execution::v1::ResultEnvelope::NODE_FAILED);
  env.set_error_class(evo::execution::v1::ERROR_TRANSIENT);
  *env.mutable_finished_at() =
      google::protobuf::util::TimeUtil::MillisecondsToTimestamp(
          evo::now_wall_ms());
  return env.SerializeAsString();
}

// Encode a PERMANENT failure result (never retried).
std::string encode_permanent_failure(const std::string& run_id,
                                     const std::string& node, unsigned attempt,
                                     const std::string& error) {
  evo::execution::v1::ResultEnvelope env;
  env.set_run_id(run_id);
  env.set_node_id(node);
  env.set_attempt_number(attempt);
  env.set_completed(false);
  env.set_error(error);
  env.set_status(evo::execution::v1::ResultEnvelope::NODE_FAILED);
  env.set_error_class(evo::execution::v1::ERROR_PERMANENT);
  *env.mutable_finished_at() =
      google::protobuf::util::TimeUtil::MillisecondsToTimestamp(
          evo::now_wall_ms());
  return env.SerializeAsString();
}

// M32: flaky worker. For a configured node, fails TRANSIENTLY for the first
// `fail_times` attempts, then succeeds. Every other node succeeds immediately.
// This drives the transient-then-success retry path end to end.
class FlakyWorker {
 public:
  FlakyWorker(InMemoryTransport& t, std::string prefix, std::string run_id,
              std::string flaky_node, int fail_times)
      : transport_(t),
        prefix_(std::move(prefix)),
        run_id_(std::move(run_id)),
        flaky_node_(std::move(flaky_node)),
        fail_times_(fail_times) {}

  void start() {
    thread_ = std::jthread([this](std::stop_token st) { this->loop(st); });
  }
  void stop() {
    thread_.request_stop();
    if (thread_.joinable()) thread_.join();
  }
  std::size_t executed() const { return executed_.load(); }
  int attempts_for(const std::string& node) const {
    return attempts_.count(node) ? attempts_.at(node) : 0;
  }

 private:
  void loop(std::stop_token st) {
    const std::string tasks = evo::task_stream_key(prefix_);
    const std::string results = evo::result_stream_key(prefix_);
    transport_.ensure_group(tasks, "workers");
    while (!st.stop_requested()) {
      auto msg = transport_.read(tasks, "workers", "flaky-worker", 20ms, st);
      if (!msg) continue;
      evo::execution::v1::TaskEnvelope task;
      if (!task.ParseFromString(msg->payload)) {
        transport_.ack(tasks, "workers", msg->id);
        continue;
      }
      executed_.fetch_add(1);
      attempts_[task.node_id()]++;

      std::string result;
      if (task.node_id() == flaky_node_ &&
          attempts_[task.node_id()] <= fail_times_) {
        result = encode_transient_failure(
            run_id_, task.node_id(), task.attempt_number(),
            "synthetic transient failure");
      } else {
        result = encode_success(run_id_, task.node_id(), task.attempt_number(),
                                "{\"ok\":true,\"node\":\"" + task.node_id() +
                                    "\"}");
      }
      transport_.publish(results, result);
      transport_.ack(tasks, "workers", msg->id);
    }
  }

  InMemoryTransport& transport_;
  std::string prefix_;
  std::string run_id_;
  std::string flaky_node_;
  int fail_times_;
  std::jthread thread_;
  std::atomic<std::size_t> executed_{0};
  std::map<std::string, int> attempts_;
};

}  // namespace

int main() {
  // --- 1. Diamond end-to-end + audit assertions ----------------------------
  {
    InMemoryTransport transport;
    InMemoryRunStore store;
    DistributedRunConfig cfg;
    cfg.run_id = "run-diamond";
    cfg.org_id = "org-1";
    cfg.workflow_id = "wf-1";
    cfg.env_prefix = "evo:m26unit";
    cfg.read_block_ms = 20ms;
    cfg.run_timeout = 10s;

    std::vector<evo::RunEvent> events;
    DistributedRunLoop loop(make_diamond(), transport, store, cfg,
                            [&](const evo::RunEvent& ev) { events.push_back(ev); });
    FakeWorker worker(transport, cfg.env_prefix, cfg.run_id);
    worker.start();

    const std::string status = loop.run();
    worker.stop();

    check(status == evo::run_status::kSucceeded, "diamond run succeeds");
    check(worker.executed() == 4, "worker executed all 4 nodes");

    // Durable audit: run + node rows.
    auto run = store.get_run("run-diamond");
    check(run.has_value() && run->status == evo::run_status::kSucceeded &&
              run->outcome == "succeeded" && run->engine == "evo",
          "run row terminal succeeded (engine=evo)");
    for (const char* n : {"start", "a", "b", "c"}) {
      auto nr = store.get_node_run("run-diamond", n);
      check(nr.has_value() && nr->status == evo::node_status::kSucceeded,
            std::string("node_run succeeded: " + std::string(n)).c_str());
      check(nr.has_value() && !nr->output_json.empty(),
            std::string("node output persisted: " + std::string(n)).c_str());
      check(store.attempt_row_count("run-diamond", n) == 1,
            std::string("exactly one attempt row: " + std::string(n)).c_str());
    }

    // Events: run_started first, run_finished last, 4 dispatches, 4 successes.
    check(!events.empty() && events.front().kind == "run_started",
          "first event is run_started");
    check(events.back().kind == "run_finished", "last event is run_finished");
    std::size_t dispatched = 0, succeeded = 0;
    for (const auto& ev : events) {
      if (ev.kind == "node_dispatched") dispatched++;
      if (ev.kind == "node_succeeded") succeeded++;
    }
    check(dispatched == 4 && succeeded == 4,
          "events: 4 dispatched + 4 succeeded");
    // Event stream carries the same normalized events for UI consumers.
    check(transport.stream_length(evo::event_stream_key(cfg.env_prefix)) >= 10,
          "event stream carries normalized run events");
  }

  // --- 2. Duplicate result injection: at-most-once unlock -------------------
  {
    InMemoryTransport transport;
    InMemoryRunStore store;
    DistributedRunConfig cfg;
    cfg.run_id = "run-dup";
    cfg.org_id = "org-1";
    cfg.workflow_id = "wf-1";
    cfg.env_prefix = "evo:m26dup";
    cfg.read_block_ms = 20ms;
    cfg.run_timeout = 10s;

    DistributedRunLoop loop(make_diamond(), transport, store, cfg);
    FakeWorker worker(transport, cfg.env_prefix, cfg.run_id);
    worker.start();

    // Inject a duplicate success for node "a" (same attempt id) repeatedly;
    // the loop must apply it at most once and never double-unlock "c".
    const std::string dup = encode_success(cfg.run_id, "a", 1, "{\"dup\":1}");
    std::jthread injector([&](std::stop_token st) {
      const std::string results = evo::result_stream_key("evo:m26dup");
      while (!st.stop_requested()) {
        transport.publish(results, dup);
        std::this_thread::sleep_for(5ms);
      }
    });

    const std::string status = loop.run();
    injector.request_stop();
    injector.join();
    worker.stop();

    check(status == evo::run_status::kSucceeded, "run succeeds under dup storm");
    check(store.attempt_row_count("run-dup", "c") == 1,
          "successor c dispatched exactly once (no double unlock)");
    check(store.attempt_row_count("run-dup", "a") == 1,
          "node a has exactly one attempt row despite duplicates");
    auto nr = store.get_node_run("run-dup", "a");
    check(nr.has_value() && nr->status == evo::node_status::kSucceeded,
          "node a terminal succeeded exactly once");

    // M33: the durable idempotency ledger holds EXACTLY ONE claim for node a's
    // applied result, and the committed output is reusable. The dup storm
    // published the same result many times; the ledger's unique key collapsed
    // them all to the first application. Whichever result won the race (the
    // injected dup or the worker's legitimate one), the ledger must store
    // exactly the output the node persisted — the first committed output.
    const std::string akey =
        evo::result_idempotency_key([] {
          evo::execution::v1::ResultEnvelope e;
          e.set_run_id("run-dup");
          e.set_node_id("a");
          e.set_attempt_number(1);
          return e;
        }());
    auto aresp = store.get_idempotency_response(akey);
    check(aresp.has_value(),
          "m33: applied result claimed a durable idempotency key");
    check(aresp.has_value() && nr.has_value() &&
              *aresp == nr->output_json,
          "m33: ledger stores the first committed output (reusable)");
  }

  // --- 3. Identity validation: unknown node / wrong run ignored -------------
  {
    InMemoryTransport transport;
    InMemoryRunStore store;
    DistributedRunConfig cfg;
    cfg.run_id = "run-id";
    cfg.org_id = "org-1";
    cfg.workflow_id = "wf-1";
    cfg.env_prefix = "evo:m26id";
    cfg.read_block_ms = 20ms;
    cfg.run_timeout = 10s;

    DistributedRunLoop loop(make_diamond(), transport, store, cfg);
    FakeWorker worker(transport, cfg.env_prefix, cfg.run_id);
    worker.start();

    // Bogus results: unknown node, wrong run id.
    const std::string results = evo::result_stream_key(cfg.env_prefix);
    transport.publish(results, encode_success(cfg.run_id, "ghost", 1, "{}"));
    transport.publish(results, encode_success("other-run", "a", 1, "{}"));

    const std::string status = loop.run();
    worker.stop();
    check(status == evo::run_status::kSucceeded,
          "bogus results ignored; run still succeeds");
    check(!store.get_node_run("run-id", "ghost").has_value(),
          "unknown node never persisted");
  }

  // --- 4. Failure path: persist details, cancel downstream, no unlock -------
  {
    InMemoryTransport transport;
    InMemoryRunStore store;
    DistributedRunConfig cfg;
    cfg.run_id = "run-fail";
    cfg.org_id = "org-1";
    cfg.workflow_id = "wf-1";
    cfg.env_prefix = "evo:m26fail";
    cfg.read_block_ms = 20ms;
    cfg.run_timeout = 10s;

    DistributedRunLoop loop(make_diamond(), transport, store, cfg);
    FakeWorker worker(transport, cfg.env_prefix, cfg.run_id,
                      /*fail_node=*/"a");
    worker.start();

    const std::string status = loop.run();
    worker.stop();

    check(status == evo::run_status::kFailed, "run fails when a node fails");
    auto na = store.get_node_run("run-fail", "a");
    check(na.has_value() && na->status == evo::node_status::kFailed &&
              na->failure_reason == "synthetic failure",
          "failure details persisted for a");
    // c depends on a: it must be canceled, never executed/unlocked.
    auto nc = store.get_node_run("run-fail", "c");
    check(nc.has_value() && nc->status == evo::node_status::kCanceled,
          "downstream c canceled (not unlocked) after a failed");
    check(store.attempt_row_count("run-fail", "c") == 0,
          "c never dispatched (no attempt row)");
  }

  // --- 5. Malformed result payload quarantined ------------------------------
  {
    InMemoryTransport transport;
    InMemoryRunStore store;
    DistributedRunConfig cfg;
    cfg.run_id = "run-malformed";
    cfg.org_id = "org-1";
    cfg.workflow_id = "wf-1";
    cfg.env_prefix = "evo:m26mal";
    cfg.read_block_ms = 20ms;
    cfg.run_timeout = 10s;

    DistributedRunLoop loop(make_diamond(), transport, store, cfg);
    FakeWorker worker(transport, cfg.env_prefix, cfg.run_id);
    worker.start();

    const std::string results = evo::result_stream_key(cfg.env_prefix);
    transport.publish(results, "this is not a protobuf");

    const std::string status = loop.run();
    worker.stop();
    check(status == evo::run_status::kSucceeded,
          "malformed payload quarantined; run unaffected");
    check(transport.pending_count(results, cfg.result_group) == 0,
          "malformed message acked (not left pending forever)");
  }

  // --- 6. Cancellation mid-run ----------------------------------------------
  {
    InMemoryTransport transport;
    InMemoryRunStore store;
    DistributedRunConfig cfg;
    cfg.run_id = "run-cancel";
    cfg.org_id = "org-1";
    cfg.workflow_id = "wf-1";
    cfg.env_prefix = "evo:m26cancel";
    cfg.read_block_ms = 20ms;
    cfg.run_timeout = 10s;

    DistributedRunLoop loop(make_diamond(), transport, store, cfg);
    // No worker: nothing completes, so cancel() must terminate the run.
    std::jthread canceller([&](std::stop_token st) {
      while (!st.stop_requested() && loop.events().size() < 2) {
        std::this_thread::sleep_for(2ms);
      }
      loop.cancel("user requested stop");
    });

    const std::string status = loop.run();
    canceller.request_stop();
    canceller.join();

    check(status == evo::run_status::kCanceled, "cancel() terminates the run");
    auto run = store.get_run("run-cancel");
    check(run.has_value() && run->status == evo::run_status::kCanceled &&
              run->outcome == "canceled",
          "run row durably canceled");
    bool all_terminal = true;
    for (const char* n : {"start", "a", "b", "c"}) {
      auto nr = store.get_node_run("run-cancel", n);
      if (!nr.has_value() || !evo::node_status::is_terminal(nr->status)) {
        all_terminal = false;
      }
    }
    check(all_terminal, "every node row durably terminal after cancel");
  }

  // --- 7. stop() mid-run leaves a consistent terminal state -----------------
  {
    InMemoryTransport transport;
    InMemoryRunStore store;
    DistributedRunConfig cfg;
    cfg.run_id = "run-stop";
    cfg.org_id = "org-1";
    cfg.workflow_id = "wf-1";
    cfg.env_prefix = "evo:m26stop";
    cfg.read_block_ms = 20ms;
    cfg.run_timeout = 10s;

    DistributedRunLoop loop(make_diamond(), transport, store, cfg);
    std::jthread stopper([&](std::stop_token st) {
      while (!st.stop_requested() && loop.events().empty()) {
        std::this_thread::sleep_for(1ms);
      }
      std::this_thread::sleep_for(5ms);
      loop.stop();
    });
    const std::string status = loop.run();
    stopper.request_stop();
    stopper.join();
    check(status == evo::run_status::kCanceled,
          "stop() yields terminal canceled state (no hang)");
  }

  // --- 8. M30: cancel BEFORE dispatch (cancel races run() startup) ----------
  {
    InMemoryTransport transport;
    InMemoryRunStore store;
    DistributedRunConfig cfg;
    cfg.run_id = "run-m30-pre";
    cfg.org_id = "org-1";
    cfg.workflow_id = "wf-1";
    cfg.env_prefix = "evo:m30pre";
    cfg.read_block_ms = 20ms;
    cfg.run_timeout = 10s;

    DistributedRunLoop loop(make_diamond(), transport, store, cfg);
    // Cancel BEFORE run(): the run row does not exist yet, so the durable
    // stamp must be retried by run() right after it creates the row.
    loop.cancel("user requested stop");
    const auto cancel_at = std::chrono::steady_clock::now();
    const std::string status = loop.run();
    // Diagnostic (NOT benchmark-grade; single sample, no methodology):
    // scheduler-side cancellation latency = cancel() -> run() returns.
    const auto scheduler_cancel_us =
        std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now() - cancel_at)
            .count();
    printf("  info m30 scheduler cancel latency (diagnostic, 1 sample): "
           "%lldus\n",
           static_cast<long long>(scheduler_cancel_us));

    check(status == evo::run_status::kCanceled,
          "m30: cancel-before-dispatch terminates canceled");
    bool all_canceled = true;
    bool any_attempt = false;
    for (const char* n : {"start", "a", "b", "c"}) {
      auto nr = store.get_node_run("run-m30-pre", n);
      if (!nr.has_value() || nr->status != evo::node_status::kCanceled) {
        all_canceled = false;
      }
      if (store.attempt_row_count("run-m30-pre", n) != 0) any_attempt = true;
    }
    check(all_canceled, "m30: every node canceled (none dispatched)");
    check(!any_attempt, "m30: no attempt rows created (no dispatch after cancel)");
    auto run = store.get_run("run-m30-pre");
    check(run.has_value() && run->cancel_requested_at > 0 &&
              run->cancel_reason == "user requested stop",
          "m30: cancel timestamp + reason stamped despite startup race");

    // Exactly one CANCEL_RUN control message, well-formed, for this run.
    const std::string control = evo::control_stream_key(cfg.env_prefix);
    transport.ensure_group(control, "m30-readers", "0");
    std::stop_source ss;
    int cancel_msgs = 0;
    bool well_formed = true;
    while (true) {
      auto m = transport.read(control, "m30-readers", "r", 100ms,
                              ss.get_token());
      if (!m) break;
      evo::execution::v1::ControlEnvelope env;
      if (!env.ParseFromString(m->payload) ||
          env.kind() != evo::execution::v1::ControlEnvelope::CANCEL_RUN ||
          env.run_id() != cfg.run_id) {
        well_formed = false;
      }
      cancel_msgs++;
      transport.ack(control, "m30-readers", m->id);
    }
    check(cancel_msgs == 1 && well_formed,
          "m30: exactly one well-formed CANCEL_RUN control message");
  }

  // --- 9. M30: repeated Stop is idempotent (first request wins) -------------
  {
    InMemoryTransport transport;
    InMemoryRunStore store;
    DistributedRunConfig cfg;
    cfg.run_id = "run-m30-rep";
    cfg.org_id = "org-1";
    cfg.workflow_id = "wf-1";
    cfg.env_prefix = "evo:m30rep";
    cfg.read_block_ms = 20ms;
    cfg.run_timeout = 10s;

    DistributedRunLoop loop(make_diamond(), transport, store, cfg);
    loop.cancel("first request");
    loop.cancel("second request");  // must be a no-op
    loop.cancel("third request");   // must be a no-op
    const std::string status = loop.run();
    check(status == evo::run_status::kCanceled, "m30: repeated cancel -> canceled");
    auto run = store.get_run("run-m30-rep");
    check(run.has_value() && run->cancel_reason == "first request",
          "m30: first cancel reason wins (repeats do not overwrite)");

    const std::string control = evo::control_stream_key(cfg.env_prefix);
    transport.ensure_group(control, "m30-readers", "0");
    std::stop_source ss;
    int cancel_msgs = 0;
    while (true) {
      auto m = transport.read(control, "m30-readers", "r", 100ms,
                              ss.get_token());
      if (!m) break;
      cancel_msgs++;
      transport.ack(control, "m30-readers", m->id);
    }
    check(cancel_msgs == 1,
          "m30: repeated cancel publishes exactly one control message");
  }

  // --- 10. M30: Stop-after-terminal is a no-op ------------------------------
  {
    InMemoryTransport transport;
    InMemoryRunStore store;
    DistributedRunConfig cfg;
    cfg.run_id = "run-m30-term";
    cfg.org_id = "org-1";
    cfg.workflow_id = "wf-1";
    cfg.env_prefix = "evo:m30term";
    cfg.read_block_ms = 20ms;
    cfg.run_timeout = 10s;

    DistributedRunLoop loop(make_diamond(), transport, store, cfg);
    FakeWorker worker(transport, cfg.env_prefix, cfg.run_id);
    worker.start();
    const std::string status = loop.run();
    worker.stop();
    check(status == evo::run_status::kSucceeded, "m30: run completes first");

    // Cancel after the run is terminal: must not republish, restamp, or
    // regress the durable state.
    loop.cancel("too late");
    auto run = store.get_run("run-m30-term");
    check(run.has_value() && run->status == evo::run_status::kSucceeded &&
              run->cancel_requested_at == 0 && run->cancel_reason.empty(),
          "m30: Stop-after-terminal leaves succeeded run untouched");

    const std::string control = evo::control_stream_key(cfg.env_prefix);
    transport.ensure_group(control, "m30-readers", "0");
    std::stop_source ss;
    auto m = transport.read(control, "m30-readers", "r", 100ms, ss.get_token());
    check(!m.has_value(),
          "m30: no control message published after terminal state");
  }

  // --- 11. M30: late success after terminal canceled is rejected ------------
  {
    InMemoryTransport transport;
    InMemoryRunStore store;
    DistributedRunConfig cfg;
    cfg.run_id = "run-m30-late";
    cfg.org_id = "org-1";
    cfg.workflow_id = "wf-1";
    cfg.env_prefix = "evo:m30late";
    cfg.read_block_ms = 20ms;
    cfg.run_timeout = 10s;

    DistributedRunLoop loop(make_diamond(), transport, store, cfg);
    // No worker. Cancel once "start" has been dispatched (in flight).
    std::jthread canceller([&](std::stop_token st) {
      while (!st.stop_requested()) {
        bool dispatched = false;
        for (const auto& ev : loop.events()) {
          if (ev.kind == "node_dispatched" && ev.node_id == "start") {
            dispatched = true;
          }
        }
        if (dispatched) break;
        std::this_thread::sleep_for(1ms);
      }
      loop.cancel("user requested stop");
    });
    // Inject a forged success for the in-flight node shortly after cancel;
    // the run loop checks cancellation before consuming results, so this
    // result must never be applied.
    std::jthread injector([&](std::stop_token st) {
      while (!st.stop_requested() &&
             !store.get_run("run-m30-late").has_value()) {
        std::this_thread::sleep_for(1ms);
      }
      std::this_thread::sleep_for(30ms);
      transport.publish(evo::result_stream_key(cfg.env_prefix),
                        encode_success(cfg.run_id, "start", 1, "{\"late\":1}"));
    });

    const std::string status = loop.run();
    canceller.request_stop();
    canceller.join();
    injector.request_stop();
    injector.join();

    check(status == evo::run_status::kCanceled, "m30: run terminal canceled");
    auto ns = store.get_node_run("run-m30-late", "start");
    check(ns.has_value() && ns->status == evo::node_status::kCanceled &&
              ns->output_json.find("late") == std::string::npos,
          "m30: late success did not overwrite terminal canceled node");
    bool all_terminal = true;
    for (const char* n : {"start", "a", "b", "c"}) {
      auto nr = store.get_node_run("run-m30-late", n);
      if (!nr.has_value() || !evo::node_status::is_terminal(nr->status)) {
        all_terminal = false;
      }
    }
    check(all_terminal, "m30: all nodes terminal after late-result injection");
  }

  // --- 12. M31: slow-but-renewing worker is NOT reaped ----------------------
  // A worker whose task takes longer than the lease duration, but that RENEWS
  // its lease while working, must never be reaped: the run succeeds on the
  // first attempt and no lease_expired event fires.
  {
    InMemoryTransport transport;
    InMemoryRunStore store;
    DistributedRunConfig cfg;
    cfg.run_id = "run-m31-slow";
    cfg.org_id = "org-1";
    cfg.workflow_id = "wf-1";
    cfg.env_prefix = "evo:m31slow";
    cfg.read_block_ms = 20ms;
    cfg.run_timeout = 15s;
    // Lease shorter than the task, but the worker renews well inside it.
    cfg.lease_duration = 200ms;
    cfg.lease_scan_interval = 50ms;
    // Generous queue-wait lease: with ONE worker, nodes a/b/c queue behind
    // the busy worker; their dispatch-time lease must not expire before the
    // worker claims them (queue-wait is bounded separately from the claimed
    // attempt lease, per DistributedRunConfig::lease_initial_duration).
    cfg.lease_initial_duration = 10s;

    std::vector<evo::RunEvent> events;
    DistributedRunLoop loop(make_diamond(), transport, store, cfg,
                            [&](const evo::RunEvent& ev) {
                              events.push_back(ev);
                            });
    // Task takes 600ms (> 3x the 200ms lease) but renews every 50ms.
    LeaseWorker worker(transport, store, cfg.env_prefix, cfg.run_id,
                       "m31-slow-worker", /*renew=*/true, /*work_ms=*/600ms,
                       /*lease_duration=*/200ms, /*renew_interval=*/50ms);
    worker.start();

    const std::string status = loop.run();
    worker.stop();

    check(status == evo::run_status::kSucceeded,
          "m31: slow-but-renewing worker completes the run");
    bool any_lease_expired = false;
    for (const auto& ev : events) {
      if (ev.kind == "node_lease_expired") any_lease_expired = true;
    }
    check(!any_lease_expired,
          "m31: renewing worker never reaped (no lease_expired event)");
    // Every node succeeded on exactly one attempt (no re-dispatch).
    bool one_attempt_each = true;
    for (const char* n : {"start", "a", "b", "c"}) {
      if (store.attempt_row_count("run-m31-slow", n) != 1) one_attempt_each = false;
      auto nr = store.get_node_run("run-m31-slow", n);
      if (!nr.has_value() || nr->status != evo::node_status::kSucceeded) {
        one_attempt_each = false;
      }
    }
    check(one_attempt_each,
          "m31: every node succeeded on exactly one attempt (no reap)");
  }

  // --- 13. M31: killed worker's lease expires -> node re-dispatched ---------
  // A worker that acquires the lease then dies (never renews, never publishes
  // a result) must be reaped: the attempt transitions to lease_expired and the
  // node is re-dispatched as a NEW attempt, which a healthy worker completes.
  {
    InMemoryTransport transport;
    InMemoryRunStore store;
    DistributedRunConfig cfg;
    cfg.run_id = "run-m31-kill";
    cfg.org_id = "org-1";
    cfg.workflow_id = "wf-1";
    cfg.env_prefix = "evo:m31kill";
    cfg.read_block_ms = 20ms;
    cfg.run_timeout = 15s;
    cfg.lease_duration = 150ms;
    cfg.lease_scan_interval = 30ms;

    std::vector<evo::RunEvent> events;
    DistributedRunLoop loop(make_diamond(), transport, store, cfg,
                            [&](const evo::RunEvent& ev) {
                              events.push_back(ev);
                            });

    // Phase 1: a "killed" worker claims the first dispatched task (start) but
    // never renews and never publishes a result. The run loop must be RUNNING
    // (on its own thread) for anything to dispatch; we stop the killer right
    // after it acquires the lease, simulating a crash.
    std::jthread loop_thread([&] { loop.run(); });

    LeaseWorker killer(transport, store, cfg.env_prefix, cfg.run_id,
                       "m31-killed-worker", /*renew=*/false,
                       /*work_ms=*/60000ms,  // would never finish on its own
                       /*lease_duration=*/150ms, /*renew_interval=*/1000ms);
    killer.start();
    // Wait until the killed worker has actually ACQUIRED the lease for
    // (start, attempt 1) — not merely claimed the message — so the reap below
    // has a real lease to expire.
    {
      const auto wait_deadline = std::chrono::steady_clock::now() + 5s;
      bool acquired = false;
      while (std::chrono::steady_clock::now() < wait_deadline && !acquired) {
        auto l = store.get_attempt_lease("run-m31-kill", "start", 1);
        acquired = l.has_value() && l->worker_id == "m31-killed-worker";
        if (!acquired) std::this_thread::sleep_for(2ms);
      }
      check(acquired, "m31: killed worker acquired the lease before crash");
    }
    killer.stop();  // simulate crash: no result, no further renewals

    // Phase 2: a healthy worker takes over after the lease expires.
    LeaseWorker healthy(transport, store, cfg.env_prefix, cfg.run_id,
                        "m31-healthy-worker", /*renew=*/true,
                        /*work_ms=*/20ms, /*lease_duration=*/2000ms,
                        /*renew_interval=*/500ms);
    healthy.start();

    loop_thread.join();
    const std::string status = store.get_run("run-m31-kill")->status;
    healthy.stop();

    check(status == evo::run_status::kSucceeded,
          "m31: run recovers after a killed worker (re-dispatch succeeds)");

    // The killed worker's attempt (start, attempt 1) is lease_expired.
    auto lease1 = store.get_attempt_lease("run-m31-kill", "start", 1);
    check(lease1.has_value() &&
              lease1->status == evo::attempt_status::kLeaseExpired &&
              lease1->worker_id == "m31-killed-worker" &&
              lease1->expired_ms > 0,
          "m31: killed worker's attempt reaped to lease_expired (evidence)");
    // Lease evidence timestamps recorded (acquired before expired).
    check(lease1.has_value() && lease1->acquired_ms > 0 &&
              lease1->expires_ms > 0,
          "m31: lease acquired/expires timestamps recorded");

    // The node was re-dispatched as attempt 2 and completed by the healthy
    // worker — recovery, not permanent failure.
    auto lease2 = store.get_attempt_lease("run-m31-kill", "start", 2);
    check(lease2.has_value() &&
              lease2->worker_id == "m31-healthy-worker",
          "m31: node re-dispatched as a new attempt to a healthy worker");
    auto nstart = store.get_node_run("run-m31-kill", "start");
    check(nstart.has_value() && nstart->status == evo::node_status::kSucceeded,
          "m31: node eventually succeeded (not permanently failed)");
    check(store.attempt_row_count("run-m31-kill", "start") == 2,
          "m31: exactly two attempts for the reaped node");

    // A lease_expired event was emitted for the reaped attempt.
    bool saw_lease_expired = false;
    for (const auto& ev : events) {
      if (ev.kind == "node_lease_expired" && ev.node_id == "start") {
        saw_lease_expired = true;
      }
    }
    check(saw_lease_expired, "m31: node_lease_expired event emitted");
  }

  // --- 14. M32: transient-then-success retry -------------------------------
  // A node that fails transiently once, then succeeds on retry, must end
  // SUCCEEDED with exactly 2 attempts and a node_retry_scheduled event.
  {
    InMemoryTransport transport;
    InMemoryRunStore store;
    DistributedRunConfig cfg;
    cfg.run_id = "run-m32-retry";
    cfg.org_id = "org-1";
    cfg.workflow_id = "wf-1";
    cfg.env_prefix = "evo:m32retry";
    cfg.read_block_ms = 20ms;
    cfg.run_timeout = 15s;
    // Short backoff so the test is fast; deterministic jitter seed.
    cfg.retry_policies = evo::RetryPolicySet::defaults();
    cfg.retry_policies.internal.base_backoff = 30ms;
    cfg.retry_policies.internal.max_attempts = 3;
    cfg.retry_jitter_seed = 42;

    std::vector<evo::RunEvent> events;
    DistributedRunLoop loop(make_diamond(), transport, store, cfg,
                            [&](const evo::RunEvent& ev) { events.push_back(ev); });
    // Node "a" fails transiently once, then succeeds.
    FlakyWorker worker(transport, cfg.env_prefix, cfg.run_id, "a", 1);
    worker.start();

    const std::string status = loop.run();
    worker.stop();

    check(status == evo::run_status::kSucceeded,
          "m32: transient-then-success run succeeds");
    check(worker.attempts_for("a") == 2,
          "m32: flaky node executed exactly 2 attempts");
    auto na = store.get_node_run("run-m32-retry", "a");
    check(na.has_value() && na->status == evo::node_status::kSucceeded &&
              na->attempt_count == 2,
          "m32: node succeeded on attempt 2 (attempt_count=2)");
    // A retry was scheduled for node a.
    bool saw_retry = false;
    for (const auto& ev : events) {
      if (ev.kind == "node_retry_scheduled" && ev.node_id == "a") saw_retry = true;
    }
    check(saw_retry, "m32: node_retry_scheduled event emitted");
    // Downstream c still succeeded (retry did not break the fan-in).
    auto nc = store.get_node_run("run-m32-retry", "c");
    check(nc.has_value() && nc->status == evo::node_status::kSucceeded,
          "m32: downstream node succeeded after retry");
  }

  // --- 15. M32: permanent failure fails fast (no retry) ---------------------
  // A permanent failure must fail the node immediately (1 attempt), cancel
  // downstream, and NOT schedule a retry.
  {
    InMemoryTransport transport;
    InMemoryRunStore store;
    DistributedRunConfig cfg;
    cfg.run_id = "run-m32-perm";
    cfg.org_id = "org-1";
    cfg.workflow_id = "wf-1";
    cfg.env_prefix = "evo:m32perm";
    cfg.read_block_ms = 20ms;
    cfg.run_timeout = 15s;
    cfg.retry_policies = evo::RetryPolicySet::defaults();

    std::vector<evo::RunEvent> events;
    DistributedRunLoop loop(make_diamond(), transport, store, cfg,
                            [&](const evo::RunEvent& ev) { events.push_back(ev); });
    // Worker that fails node "a" PERMANENTLY on every attempt.
    class PermanentFailWorker {
     public:
      PermanentFailWorker(InMemoryTransport& t, std::string prefix,
                          std::string run_id, std::string node)
          : transport_(t),
            prefix_(std::move(prefix)),
            run_id_(std::move(run_id)),
            node_(std::move(node)) {}
      void start() {
        thread_ = std::jthread([this](std::stop_token st) { this->loop(st); });
      }
      void stop() {
        thread_.request_stop();
        if (thread_.joinable()) thread_.join();
      }
      int attempts() const { return attempts_.load(); }

     private:
      void loop(std::stop_token st) {
        const std::string tasks = evo::task_stream_key(prefix_);
        const std::string results = evo::result_stream_key(prefix_);
        transport_.ensure_group(tasks, "workers");
        while (!st.stop_requested()) {
          auto msg = transport_.read(tasks, "workers", "perm-worker", 20ms, st);
          if (!msg) continue;
          evo::execution::v1::TaskEnvelope task;
          if (!task.ParseFromString(msg->payload)) {
            transport_.ack(tasks, "workers", msg->id);
            continue;
          }
          std::string result;
          if (task.node_id() == node_) {
            attempts_.fetch_add(1);
            result = encode_permanent_failure(run_id_, task.node_id(),
                                              task.attempt_number(),
                                              "synthetic permanent failure");
          } else {
            result = encode_success(run_id_, task.node_id(),
                                    task.attempt_number(), "{\"ok\":true}");
          }
          transport_.publish(results, result);
          transport_.ack(tasks, "workers", msg->id);
        }
      }
      InMemoryTransport& transport_;
      std::string prefix_;
      std::string run_id_;
      std::string node_;
      std::jthread thread_;
      std::atomic<int> attempts_{0};
    };
    PermanentFailWorker worker(transport, cfg.env_prefix, cfg.run_id, "a");
    worker.start();

    const std::string status = loop.run();
    worker.stop();

    check(status == evo::run_status::kFailed,
          "m32: permanent failure -> run failed");
    check(worker.attempts() == 1,
          "m32: permanent failure executed exactly once (no retry)");
    auto na = store.get_node_run("run-m32-perm", "a");
    check(na.has_value() && na->status == evo::node_status::kFailed &&
              na->attempt_count == 1,
          "m32: node failed on attempt 1 (no retry)");
    // No retry scheduled for a.
    bool saw_retry = false;
    for (const auto& ev : events) {
      if (ev.kind == "node_retry_scheduled" && ev.node_id == "a") saw_retry = true;
    }
    check(!saw_retry, "m32: no retry scheduled for permanent failure");
    // Downstream c canceled (upstream failed).
    auto nc = store.get_node_run("run-m32-perm", "c");
    check(nc.has_value() && nc->status == evo::node_status::kCanceled,
          "m32: downstream canceled after permanent failure");
  }

  // --- 16. M32: repeated transient failure -> dead-letter -------------------
  // A node that fails transiently on EVERY attempt must exhaust its budget and
  // be dead-lettered (terminal), canceling downstream.
  {
    InMemoryTransport transport;
    InMemoryRunStore store;
    DistributedRunConfig cfg;
    cfg.run_id = "run-m32-dl";
    cfg.org_id = "org-1";
    cfg.workflow_id = "wf-1";
    cfg.env_prefix = "evo:m32dl";
    cfg.read_block_ms = 20ms;
    cfg.run_timeout = 15s;
    cfg.retry_policies = evo::RetryPolicySet::defaults();
    cfg.retry_policies.internal.base_backoff = 20ms;
    cfg.retry_policies.internal.max_attempts = 3;

    std::vector<evo::RunEvent> events;
    DistributedRunLoop loop(make_diamond(), transport, store, cfg,
                            [&](const evo::RunEvent& ev) { events.push_back(ev); });
    // Node "a" fails transiently forever (fail_times huge).
    FlakyWorker worker(transport, cfg.env_prefix, cfg.run_id, "a", 1000);
    worker.start();

    const std::string status = loop.run();
    worker.stop();

    check(status == evo::run_status::kFailed,
          "m32: exhausted retries -> run failed");
    check(worker.attempts_for("a") == 3,
          "m32: node executed exactly max_attempts (3) attempts");
    auto na = store.get_node_run("run-m32-dl", "a");
    check(na.has_value() && na->status == evo::node_status::kDeadLettered &&
              na->attempt_count == 3,
          "m32: node dead-lettered after 3 attempts");
    bool saw_dl = false;
    for (const auto& ev : events) {
      if (ev.kind == "node_dead_lettered" && ev.node_id == "a") saw_dl = true;
    }
    check(saw_dl, "m32: node_dead_lettered event emitted");
    auto nc = store.get_node_run("run-m32-dl", "c");
    check(nc.has_value() && nc->status == evo::node_status::kCanceled,
          "m32: downstream canceled after dead-letter");
  }

  // --- 17. M32: cancellation during backoff ---------------------------------
  // A node parked in RETRY_WAIT must be canceled (not re-dispatched) when the
  // run is canceled during its backoff.
  {
    InMemoryTransport transport;
    InMemoryRunStore store;
    DistributedRunConfig cfg;
    cfg.run_id = "run-m32-cancel";
    cfg.org_id = "org-1";
    cfg.workflow_id = "wf-1";
    cfg.env_prefix = "evo:m32cancel";
    cfg.read_block_ms = 20ms;
    cfg.run_timeout = 15s;
    cfg.retry_policies = evo::RetryPolicySet::defaults();
    // Long backoff so the node is still parked when we cancel.
    cfg.retry_policies.internal.base_backoff = 5000ms;
    cfg.retry_policies.internal.max_attempts = 3;

    std::vector<evo::RunEvent> events;
    DistributedRunLoop loop(make_diamond(), transport, store, cfg,
                            [&](const evo::RunEvent& ev) { events.push_back(ev); });
    // Node "a" fails transiently forever -> parks in RETRY_WAIT on the long
    // backoff after attempt 1.
    FlakyWorker worker(transport, cfg.env_prefix, cfg.run_id, "a", 1000);
    worker.start();

    // Run on a thread so we can cancel mid-backoff.
    std::jthread loop_thread([&] { loop.run(); });
    // Wait until node a is parked in RETRY_WAIT.
    {
      const auto deadline = std::chrono::steady_clock::now() + 5s;
      bool parked = false;
      while (std::chrono::steady_clock::now() < deadline && !parked) {
        auto na = store.get_node_run("run-m32-cancel", "a");
        parked = na.has_value() && na->status == evo::node_status::kRetryWait;
        if (!parked) std::this_thread::sleep_for(5ms);
      }
      check(parked, "m32: node parked in retry_wait during backoff");
    }
    loop.cancel("user stop during backoff");
    loop_thread.join();
    worker.stop();

    auto na = store.get_node_run("run-m32-cancel", "a");
    check(na.has_value() && na->status == evo::node_status::kCanceled,
          "m32: retry-waiting node canceled (not re-dispatched)");
    check(worker.attempts_for("a") == 1,
          "m32: node not re-dispatched after cancel during backoff");
    auto run = store.get_run("run-m32-cancel");
    check(run.has_value() && run->status == evo::run_status::kCanceled,
          "m32: run terminal canceled");
  }

  // --- 18. M32: retry after worker lease expiry ------------------------------
  // A killed worker's attempt is reaped (M31 recovery) and the node is
  // re-dispatched; when the NEXT attempt fails transiently, the retry policy
  // must still apply normally: lease-expiry recovery consumes no retry budget,
  // and the node parks in RETRY_WAIT, then succeeds on the following attempt.
  {
    InMemoryTransport transport;
    InMemoryRunStore store;
    DistributedRunConfig cfg;
    cfg.run_id = "run-m32-lease-retry";
    cfg.org_id = "org-1";
    cfg.workflow_id = "wf-1";
    cfg.env_prefix = "evo:m32leaseretry";
    cfg.read_block_ms = 20ms;
    cfg.run_timeout = 15s;
    cfg.lease_duration = 150ms;
    cfg.lease_scan_interval = 30ms;
    // Generous queue-wait lease: the flaky worker does not acquire/renew
    // leases itself, so its attempts must not be reaped while in flight.
    cfg.lease_initial_duration = 10s;
    cfg.retry_policies = evo::RetryPolicySet::defaults();
    cfg.retry_policies.internal.base_backoff = 50ms;  // keep the test fast

    std::vector<evo::RunEvent> events;
    DistributedRunLoop loop(make_diamond(), transport, store, cfg,
                            [&](const evo::RunEvent& ev) {
                              events.push_back(ev);
                            });
    std::jthread loop_thread([&] { loop.run(); });

    // Phase 1: a killed worker claims (start, attempt 1) then dies.
    LeaseWorker killer(transport, store, cfg.env_prefix, cfg.run_id,
                       "m32-killed-worker", /*renew=*/false,
                       /*work_ms=*/60000ms, /*lease_duration=*/150ms,
                       /*renew_interval=*/1000ms);
    killer.start();
    {
      const auto wait_deadline = std::chrono::steady_clock::now() + 5s;
      bool acquired = false;
      while (std::chrono::steady_clock::now() < wait_deadline && !acquired) {
        auto l = store.get_attempt_lease("run-m32-lease-retry", "start", 1);
        acquired = l.has_value() && l->worker_id == "m32-killed-worker";
        if (!acquired) std::this_thread::sleep_for(2ms);
      }
      check(acquired, "m32: killed worker acquired the lease before crash");
    }
    killer.stop();  // crash: no result, no further renewals

    // Phase 2: the reaped node is re-dispatched as attempt 2 to a flaky
    // worker, which fails it TRANSIENTLY once (-> RETRY_WAIT) and then
    // succeeds on attempt 3.
    FlakyWorker flaky(transport, cfg.env_prefix, cfg.run_id, "start", 1);
    flaky.start();

    loop_thread.join();
    const std::string status = store.get_run("run-m32-lease-retry")->status;
    flaky.stop();

    check(status == evo::run_status::kSucceeded,
          "m32: run succeeds after lease-expiry recovery + retry");

    // The killed attempt was reaped, and recovery consumed NO retry budget:
    // the node needed 3 attempts total (reaped, transient-fail, success).
    auto lease1 = store.get_attempt_lease("run-m32-lease-retry", "start", 1);
    check(lease1.has_value() &&
              lease1->status == evo::attempt_status::kLeaseExpired,
          "m32: killed attempt reaped to lease_expired before retry");
    check(store.attempt_row_count("run-m32-lease-retry", "start") == 3,
          "m32: exactly 3 attempts (reap does not consume retry budget)");
    auto nstart = store.get_node_run("run-m32-lease-retry", "start");
    check(nstart.has_value() &&
              nstart->status == evo::node_status::kSucceeded &&
              nstart->attempt_count == 3,
          "m32: node succeeded on attempt 3 after reap + transient retry");

    // Both recovery and retry evidence events were emitted.
    bool saw_lease_expired = false;
    bool saw_retry = false;
    for (const auto& ev : events) {
      if (ev.kind == "node_lease_expired" && ev.node_id == "start") {
        saw_lease_expired = true;
      }
      if (ev.kind == "node_retry_scheduled" && ev.node_id == "start") {
        saw_retry = true;
      }
    }
    check(saw_lease_expired, "m32: node_lease_expired emitted before retry");
    check(saw_retry, "m32: node_retry_scheduled emitted after reap");

    // Downstream completed normally.
    for (const char* n : {"a", "b", "c"}) {
      auto nr = store.get_node_run("run-m32-lease-retry", n);
      check(nr.has_value() && nr->status == evo::node_status::kSucceeded,
            "m32: downstream node succeeded after lease-retry recovery");
    }
  }

  // --- 19. M34: browser-affinity worker crash -> slot released, re-dispatch --
  // The documented browser resource-loss policy (FAILURE_MODEL.md §4): when the
  // owning worker dies, the browser resource is treated as LOST — the single
  // capacity-1 affinity slot must be released on lease reap (not leaked), the
  // node re-dispatched as a new attempt on a FRESH session, and the chain then
  // continues. No transparent session continuation is claimed. This test proves
  // the slot is released: if it leaked, b1's re-dispatch and b2 would be
  // blocked forever on the capacity-1 key and the run would time out.
  {
    InMemoryTransport transport;
    InMemoryRunStore store;
    DistributedRunConfig cfg;
    cfg.run_id = "run-m34-browser";
    cfg.org_id = "org-1";
    cfg.workflow_id = "wf-1";
    cfg.env_prefix = "evo:m34browser";
    cfg.read_block_ms = 20ms;
    cfg.run_timeout = 15s;
    cfg.lease_duration = 150ms;
    cfg.lease_scan_interval = 30ms;
    // Generous queue-wait so the killed worker can claim before the reap.
    cfg.lease_initial_duration = 10s;

    std::vector<evo::RunEvent> events;
    DistributedRunLoop loop(make_browser_chain(), transport, store, cfg,
                            [&](const evo::RunEvent& ev) {
                              events.push_back(ev);
                            });
    std::jthread loop_thread([&] { loop.run(); });

    // Phase 1: a killed worker claims the browser node b1 (attempt 1) then dies.
    LeaseWorker killer(transport, store, cfg.env_prefix, cfg.run_id,
                       "m34-killed-worker", /*renew=*/false,
                       /*work_ms=*/60000ms, /*lease_duration=*/150ms,
                       /*renew_interval=*/1000ms);
    killer.start();
    {
      const auto wait_deadline = std::chrono::steady_clock::now() + 5s;
      bool acquired = false;
      while (std::chrono::steady_clock::now() < wait_deadline && !acquired) {
        auto l = store.get_attempt_lease("run-m34-browser", "b1", 1);
        acquired = l.has_value() && l->worker_id == "m34-killed-worker";
        if (!acquired) std::this_thread::sleep_for(2ms);
      }
      check(acquired, "m34: killed worker acquired the browser lease pre-crash");
    }
    killer.stop();  // crash: no result, no further renewals

    // Phase 2: a healthy worker takes over after the lease expires. It renews,
    // so its own attempts are never reaped.
    LeaseWorker healthy(transport, store, cfg.env_prefix, cfg.run_id,
                        "m34-healthy-worker", /*renew=*/true,
                        /*work_ms=*/20ms, /*lease_duration=*/2000ms,
                        /*renew_interval=*/500ms);
    healthy.start();

    loop_thread.join();
    const std::string status = store.get_run("run-m34-browser")->status;
    healthy.stop();

    check(status == evo::run_status::kSucceeded,
          "m34: browser chain recovers after owning-worker crash (run succeeds)");

    // The killed attempt was reaped; the node re-dispatched as attempt 2 on the
    // healthy worker and succeeded. Exactly 2 attempts for b1.
    auto lease1 = store.get_attempt_lease("run-m34-browser", "b1", 1);
    check(lease1.has_value() &&
              lease1->status == evo::attempt_status::kLeaseExpired &&
              lease1->worker_id == "m34-killed-worker",
          "m34: killed browser attempt reaped to lease_expired");
    check(store.attempt_row_count("run-m34-browser", "b1") == 2,
          "m34: browser node re-dispatched as a new attempt after crash");
    auto lease2 = store.get_attempt_lease("run-m34-browser", "b1", 2);
    check(lease2.has_value() && lease2->worker_id == "m34-healthy-worker",
          "m34: replacement attempt ran on a different (healthy) worker");
    auto nb1 = store.get_node_run("run-m34-browser", "b1");
    check(nb1.has_value() && nb1->status == evo::node_status::kSucceeded,
          "m34: browser node eventually succeeded on a fresh session");

    // The affinity slot was RELEASED on reap: b2 (same capacity-1 key) was able
    // to dispatch + complete after b1. If the slot had leaked, b2 would never
    // have run and the run would have timed out.
    auto nb2 = store.get_node_run("run-m34-browser", "b2");
    check(nb2.has_value() && nb2->status == evo::node_status::kSucceeded &&
              store.attempt_row_count("run-m34-browser", "b2") == 1,
          "m34: downstream browser node ran on the freed capacity-1 slot");

    bool saw_lease_expired = false;
    for (const auto& ev : events) {
      if (ev.kind == "node_lease_expired" && ev.node_id == "b1") {
        saw_lease_expired = true;
      }
    }
    check(saw_lease_expired, "m34: node_lease_expired emitted for browser node");
  }

  if (failures == 0) {
    printf("\nALL M26 DISTRIBUTED RUN LOOP TESTS PASSED!\n");
    return 0;
  }
  printf("\n%d FAILURE(S)\n", failures);
  return 1;
}
