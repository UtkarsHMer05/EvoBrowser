// Milestone 17: C++ scheduler service over gRPC.
// Milestone 29: bridge product workflows to the distributed run loop.
// Milestone 35: restart recovery — on startup, resume active evo runs from
// the durable store instead of forgetting them.
//
// Wraps the scheduler core (M10–M13) behind the versioned ControlService
// (engine/proto/evo/execution.proto, generated stubs) and serves
// SubmitRun/GetRun/CancelRun/Health.
//
// Two execution modes, selected per-run by the DAG's node types:
//   - LOCAL (synthetic) mode: every node is a synthetic bench/start type. The
//     DAG runs in-process via the ConcurrentScheduler with a synthetic
//     bench-task registry (the M17 behavior; no external infra needed). This
//     keeps the M17 integration test and benchmarks self-contained.
//   - DISTRIBUTED mode (Milestone 29): the DAG contains at least one product
//     node type (open-url/act/extract/observe/agent/send-email). Those only
//     execute on TypeScript workers, so the run is driven by the
//     DistributedRunLoop (M26) over Redis Streams + the Postgres RunStore.
//     This is what lets a real workflow open a Browserbase session on a
//     worker and publish normalized run events for the UI. Requires the Redis
//     transport + Postgres store to be built (EVO_HAVE_DISTRIBUTED) and
//     reachable; otherwise SubmitRun fails closed with UNAVAILABLE.
//
// Correctness notes:
//  - Run entries are heap-stable (std::map of std::unique_ptr<ActiveRun>) so a
//    runner thread holding a raw pointer is never invalidated.
//  - The runner thread publishes its result under runs_mu_; GetRun/CancelRun
//    read under the same lock, so there is no data race. Terminal entries are
//    pruned oldest-first past a retention bound; a pruned entry's runner is
//    JOINED before its ActiveRun is destroyed (the lambdas touch raw->org_id
//    and metrics after publishing done=true), which keeps that pointer rule
//    intact for the whole lifetime of an entry.
//  - Distributed node state for GetRun is derived from the loop's normalized
//    run events, collected under a per-run mutex by the on_event callback —
//    the gRPC thread never touches the loop's internal scheduling state.
//  - Graceful shutdown: a dedicated sigwait thread calls server->Shutdown() on
//    SIGINT/SIGTERM (async-signal-safe; no busy-spin), then in-flight runs are
//    canceled/stopped and their runner threads joined before exit.
//  - Timestamps crossing the service boundary are wall-clock UTC (§7). Local
//    mode bridges steady->wall with a per-run anchor pair; distributed mode
//    events already carry wall-clock milliseconds.
//  - Structured logs carry run/org identifiers only; never secrets.

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <deque>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include <grpcpp/grpcpp.h>

#include "evo/bench.hpp"
#include "evo/auth_token.hpp"
#include "evo/concurrent_scheduler.hpp"
#include "evo/dag.hpp"
#include "evo/execution.grpc.pb.h"
#include "evo/input_limits.hpp"
#include "evo/log.hpp"
#include "evo/metrics_registry.hpp"
#include "evo/quota.hpp"
#include "evo/scheduler.hpp"

#include "metrics_http.hpp"

#ifdef EVO_HAVE_DISTRIBUTED
#include "evo/distributed_run_loop.hpp"
#include "evo/pg_run_store.hpp"
#include "evo/redis_transport.hpp"
#include "evo/run_store.hpp"
#include "evo/transport.hpp"
#endif

namespace evo::service {

namespace {

void log_line(const std::string& event, const std::string& run_id,
              const std::string& org_id, const std::string& detail = "") {
  // M38: structured JSON log line (one object per line, wall-clock UTC ms).
  // Identifiers only — never secrets (the logger also redacts secret-like
  // keys as defense in depth).
  evo::log::info(event, {
                            {"service", "evo-scheduler"},
                            {"run_id", run_id.empty() ? "-" : run_id},
                            {"org_id", org_id.empty() ? "-" : org_id},
                            {"detail", detail},
                        });
}

std::string env_or(const char* name, const std::string& fallback) {
  const char* v = std::getenv(name);
  return (v && *v) ? std::string(v) : fallback;
}

#ifdef EVO_HAVE_DISTRIBUTED
// Single source of truth for the distributed-infra endpoints this process
// talks to. Every call site used to re-read the same five/six variables with
// inline dev defaults; centralizing them keeps the loopback-only fallbacks
// documented once and makes an env-only production deployment a matter of
// setting the variables (no code edits, no divergent defaults).
evo::RedisTransportConfig phase2_redis_config_from_env() {
  evo::RedisTransportConfig cfg;
  cfg.host = env_or("EVO_PHASE2_REDIS_HOST", "127.0.0.1");
  cfg.port = std::atoi(env_or("EVO_PHASE2_REDIS_PORT", "6390").c_str());
  return cfg;
}

evo::PgRunStoreConfig phase2_pg_config_from_env() {
  evo::PgRunStoreConfig cfg;
  cfg.host = env_or("EVO_PHASE2_PG_HOST", "127.0.0.1");
  cfg.port = std::atoi(env_or("EVO_PHASE2_PG_PORT", "5433").c_str());
  cfg.user = env_or("EVO_PHASE2_PG_USER", "evo");
  cfg.password = env_or("EVO_PHASE2_PG_PASSWORD", "evo_dev_password");
  cfg.dbname = env_or("EVO_PHASE2_PG_DB", "evo_phase2");
  return cfg;
}
#endif

// Synthetic local executor registry. Only bench tasks run here; unknown real
// node types (act/send-email/...) are intentionally absent so they surface as
// clear per-node failures rather than silently no-op (they belong to TS workers).
std::map<std::string, evo::ConcurrentTaskFn> local_tasks(
    const std::map<evo::NodeId, int>& sleep_ms) {
  std::map<std::string, evo::ConcurrentTaskFn> tasks;
  tasks["start"] = [](const evo::NodeSpec&, std::stop_token) {
    return evo::TaskResult{true, "started"};
  };
  tasks["bench:sleep"] = evo::bench::sleep_task_cooperative(sleep_ms);
  return tasks;
}

// A node type the in-process synthetic registry can execute. Product node
// types are NOT in this set — they require the distributed worker path.
bool is_synthetic_type(const std::string& t) {
  return t == "start" || t.rfind("bench:", 0) == 0;
}

// True when every node in the DAG is a synthetic type (=> local mode).
bool is_synthetic_only(const evo::Dag& dag) {
  for (const auto& id : dag.node_ids()) {
    const evo::NodeSpec* spec = dag.node(id);
    if (!spec) return false;
    if (!is_synthetic_type(spec->type)) return false;
  }
  return true;
}

void set_timestamp(google::protobuf::Timestamp* ts,
                   std::chrono::system_clock::time_point tp) {
  // Manual conversion: protobuf 35 removed TimeUtil::TimePointToTimestamp.
  const auto since_epoch = tp.time_since_epoch();
  const auto secs =
      std::chrono::duration_cast<std::chrono::seconds>(since_epoch);
  const auto nanos =
      std::chrono::duration_cast<std::chrono::nanoseconds>(since_epoch - secs);
  ts->set_seconds(secs.count());
  ts->set_nanos(static_cast<std::int32_t>(nanos.count()));
}

void set_timestamp_ms(google::protobuf::Timestamp* ts, std::int64_t wall_ms) {
  set_timestamp(ts, std::chrono::system_clock::time_point(
                        std::chrono::milliseconds(wall_ms)));
}

}  // namespace

struct ActiveRun {
  // --- Local (synthetic) mode ---
  std::unique_ptr<evo::ConcurrentScheduler> scheduler;
  evo::ConcurrentRunLog log;

#ifdef EVO_HAVE_DISTRIBUTED
  // --- Distributed mode (M29). transport/store must outlive loop; declared
  // before it so destruction order (reverse) destroys loop first. ---
  bool distributed = false;
  std::unique_ptr<evo::RedisTransport> transport;
  std::unique_ptr<evo::PgRunStore> store;
  std::unique_ptr<evo::DistributedRunLoop> loop;
  // Normalized events collected by the loop's on_event callback, guarded by
  // events_mu. The gRPC thread derives GetRun node state from these only.
  mutable std::mutex events_mu;
  std::vector<evo::RunEvent> events;
  // The DAG's (node_id, type) pairs, captured at submit so GetRun can report
  // not-yet-dispatched nodes too.
  std::vector<std::pair<std::string, std::string>> node_id_type;
#endif

  std::thread runner;
  // Published under runs_mu_ exactly once at completion:
  bool done = false;
  evo::execution::v1::RunOutcome outcome =
      evo::execution::v1::RunOutcome::OUTCOME_UNSPECIFIED;
  // steady->wall anchors captured at submit (for local boundary timestamps).
  std::chrono::steady_clock::time_point steady_anchor;
  std::chrono::system_clock::time_point wall_anchor;
  std::string org_id;
  std::string workflow_version_id;
};

class ControlServiceImpl final
    : public evo::execution::v1::ControlService::Service {
 public:
  // M38: service-to-service engine-token auth. When EVO_ENGINE_TOKEN is set
  // (non-empty), every RPC must carry `authorization: Bearer <token>` metadata
  // matching it (constant-time compare), else UNAUTHENTICATED. When unset,
  // auth is disabled (backwards compatible; default binds loopback only). The
  // token is read once at construction and never logged.
  grpc::Status authorize(const grpc::ServerContext* ctx,
                         const char* rpc) const {
    if (engine_token_.empty()) return grpc::Status::OK;  // auth disabled
    std::string presented;
    const auto& metadata = ctx->client_metadata();
    auto it = metadata.find("authorization");
    if (it != metadata.end()) {
      presented = evo::auth::extract_bearer(std::string(it->second.data(),
                                                        it->second.size()));
    }
    if (!evo::auth::constant_time_equals(engine_token_, presented)) {
      evo::log::warn("rpc_unauthenticated",
                     {{"service", "evo-scheduler"}, {"rpc", rpc}});
      return grpc::Status(grpc::UNAUTHENTICATED,
                          "missing or invalid engine token");
    }
    return grpc::Status::OK;
  }

  grpc::Status SubmitRun(grpc::ServerContext* ctx,
                         const evo::execution::v1::SubmitRunRequest* req,
                         evo::execution::v1::SubmitRunResponse* resp) override {
    if (const grpc::Status st = authorize(ctx, "SubmitRun"); !st.ok()) {
      return st;
    }
    const std::string rid = req->run_id();
    if (rid.empty()) {
      return grpc::Status(grpc::INVALID_ARGUMENT, "run_id is required");
    }
    // M38: validate identifiers + payload size at the trust boundary, BEFORE
    // mutating any state. Identifiers must be bounded [A-Za-z0-9._-]; the DAG
    // payload must be within the size limit. Reject fail-closed.
    if (!evo::limits::is_valid_identifier(rid)) {
      return grpc::Status(grpc::INVALID_ARGUMENT,
                          "run_id contains invalid characters or is too long");
    }
    if (!req->org_id().empty() &&
        !evo::limits::is_valid_identifier(req->org_id())) {
      return grpc::Status(grpc::INVALID_ARGUMENT,
                          "org_id contains invalid characters or is too long");
    }
    if (!req->workflow_id().empty() &&
        !evo::limits::is_valid_identifier(req->workflow_id())) {
      return grpc::Status(
          grpc::INVALID_ARGUMENT,
          "workflow_id contains invalid characters or is too long");
    }
    if (!evo::limits::is_dag_size_ok(req->dag_json())) {
      return grpc::Status(grpc::INVALID_ARGUMENT,
                          "dag_json exceeds the maximum payload size");
    }
    // M38: count every submission attempt (before idempotency/quota checks).
    metrics_.counter_add("evo_scheduler_submissions_total",
                         "SubmitRun requests received by this scheduler.", {},
                         1.0);

    {
      std::lock_guard lock(runs_mu_);
      if (runs_.count(rid) != 0) {
        // Idempotent submission: re-submitting an existing run is a no-op that
        // reports acceptance without double-executing.
        resp->set_run_id(rid);
        resp->set_accepted(true);
        resp->set_message("run already submitted (idempotent)");
        log_line("submit_idempotent", rid, req->org_id());
        return grpc::Status::OK;
      }
    }

    // M36: multi-tenant admission. The org_id is the scheduling tenant key; it
    // originates from the authenticated server-side submission (the app resolves
    // it from Clerk), never from a browser client (M36 step 9). When the org's
    // active-run quota (or the global cap) is exhausted, REJECT with
    // RESOURCE_EXHAUSTED — fail closed, never queue without bound (M36 step 4).
    if (!quota_gate_.admit_run(req->org_id())) {
      log_line("submit_rejected", rid, req->org_id(),
               "quota exhausted (active-run limit)");
      metrics_.counter_add(
          "evo_scheduler_submissions_rejected_total",
          "SubmitRun requests rejected (quota or validation).", {}, 1.0);
      return grpc::Status(grpc::RESOURCE_EXHAUSTED,
                          "org active-run quota exhausted");
    }

    // Validate + parse the canonical DAG JSON into an engine Dag before
    // mutating any state (trust-boundary rule: validate before durable write).
    auto parse = evo::Dag::from_json_string(req->dag_json());
    if (!parse.ok() || !parse.dag.has_value()) {
      quota_gate_.release_run(req->org_id());  // give back the admitted slot
      std::string err = "malformed or invalid DAG";
      if (!parse.errors.empty()) err += ": " + parse.errors[0].message;
      log_line("submit_rejected", rid, req->org_id(), err);
      return grpc::Status(grpc::INVALID_ARGUMENT, err);
    }
    evo::Dag dag = std::move(*parse.dag);

    if (is_synthetic_only(dag)) {
      return submit_local(rid, req, std::move(dag), resp);
    }
    return submit_distributed(rid, req, std::move(dag), resp);
  }

  grpc::Status CancelRun(grpc::ServerContext* ctx,
                         const evo::execution::v1::CancelRunRequest* req,
                         evo::execution::v1::CancelRunResponse* resp) override {
    if (const grpc::Status st = authorize(ctx, "CancelRun"); !st.ok()) {
      return st;
    }
    std::lock_guard lock(runs_mu_);
    auto it = runs_.find(req->run_id());
    if (it == runs_.end()) {
      resp->set_ok(false);
      resp->set_outcome(evo::execution::v1::RunOutcome::OUTCOME_UNSPECIFIED);
      return grpc::Status(grpc::NOT_FOUND, "run not found");
    }
    ActiveRun& e = *it->second;

    // M30 idempotency: Stop-after-terminal is a no-op that reports the run's
    // ACTUAL terminal outcome (never re-cancels, never overwrites). Repeated
    // Stop on a still-running run is also a no-op after the first request —
    // the loop's cancel() itself is first-request-wins.
    if (e.done) {
      resp->set_ok(true);
      resp->set_outcome(e.outcome);
      log_line("cancel_after_terminal_noop", req->run_id(), e.org_id,
               req->reason());
      return grpc::Status::OK;
    }

#ifdef EVO_HAVE_DISTRIBUTED
    if (e.distributed && e.loop) {
      e.loop->cancel(req->reason());
    } else
#endif
        if (e.scheduler) {
      e.scheduler->cancel();
    }
    resp->set_ok(true);
    resp->set_outcome(evo::execution::v1::RunOutcome::CANCELED);
    log_line("cancel_requested", req->run_id(), e.org_id, req->reason());
    return grpc::Status::OK;
  }

  grpc::Status GetRun(grpc::ServerContext* ctx,
                      const evo::execution::v1::GetRunRequest* req,
                      evo::execution::v1::GetRunResponse* resp) override {
    if (const grpc::Status st = authorize(ctx, "GetRun"); !st.ok()) {
      return st;
    }
    std::lock_guard lock(runs_mu_);
    auto it = runs_.find(req->run_id());
    if (it == runs_.end()) {
      return grpc::Status(grpc::NOT_FOUND, "run not found");
    }
    const ActiveRun& e = *it->second;
    resp->set_run_id(req->run_id());
    resp->set_workflow_version_id(e.workflow_version_id);
    set_timestamp(resp->mutable_created_at(), e.wall_anchor);

#ifdef EVO_HAVE_DISTRIBUTED
    if (e.distributed) {
      return fill_run_distributed(e, resp);
    }
#endif
    return fill_run_local(e, resp);
  }

  grpc::Status Health(grpc::ServerContext* ctx,
                      const evo::execution::v1::HealthRequest*,
                      evo::execution::v1::HealthResponse* resp) override {
    if (const grpc::Status st = authorize(ctx, "Health"); !st.ok()) {
      return st;
    }
    resp->set_ok(true);
    // M36 step 6: expose queue depth + rejected/deferred counters. The detail
    // carries a structured JSON snapshot of the quota gate (admitted/rejected
    // runs, acquired/deferred/released tasks, per-org active-run depth, and
    // per-class in-flight counts) alongside the serving state.
    resp->set_detail("SERVING " + quota_gate_.to_json_string());
    return grpc::Status::OK;
  }

  // Graceful drain: cancel/stop every run, then join all runner threads.
  // Called after the gRPC server has stopped accepting new RPCs.
  void shutdown() {
    std::map<std::string, ActiveRun*> to_join;
    {
      std::lock_guard lock(runs_mu_);
      for (auto& [rid, e] : runs_) {
#ifdef EVO_HAVE_DISTRIBUTED
        if (e->distributed && e->loop) {
          e->loop->stop();
        } else
#endif
            if (e->scheduler) {
          e->scheduler->cancel();
        }
        to_join.emplace(rid, e.get());
      }
    }
    for (auto& [rid, e] : to_join) {
      if (e->runner.joinable()) e->runner.join();
    }
    log_line("shutdown_complete", "", "");
  }

 private:
  // --- Local (synthetic) submission: the M17 in-process path. ---
  grpc::Status submit_local(const std::string& rid,
                            const evo::execution::v1::SubmitRunRequest* req,
                            evo::Dag dag,
                            evo::execution::v1::SubmitRunResponse* resp) {
    // Default small sleep per node for the local synthetic executor.
    // EVO_LOCAL_SLEEP_MS is a test-only override (default 3ms, unchanged) that
    // lets the M36 admission test hold a run ACTIVE long enough to exercise the
    // per-org active-run cap.
    const int per_node_sleep =
        std::atoi(env_or("EVO_LOCAL_SLEEP_MS", "3").c_str());
    std::map<evo::NodeId, int> sleep_ms;
    for (const auto& id : dag.node_ids()) sleep_ms[id] = per_node_sleep;

    auto entry = std::make_unique<ActiveRun>();
    entry->org_id = req->org_id();
    entry->workflow_version_id = req->workflow_version_id();
    entry->steady_anchor = std::chrono::steady_clock::now();
    entry->wall_anchor = std::chrono::system_clock::now();
    entry->scheduler = std::make_unique<evo::ConcurrentScheduler>(
        std::move(dag), local_tasks(sleep_ms),
        evo::ConcurrentConfig{
            .num_workers = 4,
            .ready_queue_capacity = 0,
            .run_id = rid,  // seeds the browser-affinity key (M12)
        });

    ActiveRun* raw = entry.get();  // heap-stable (map of unique_ptr)
    {
      std::lock_guard lock(runs_mu_);
      register_run_locked(rid, std::move(entry));
    }

    // Execute asynchronously so the gRPC thread returns immediately.
    raw->runner = std::thread([this, raw, rid]() {
      auto log = raw->scheduler->run();  // blocks; outside the registry lock
      const bool all_ok = log.all_ok();
      const auto outcome =
          all_ok ? evo::execution::v1::RunOutcome::SUCCEEDED
          : raw->scheduler->is_canceled()
              ? evo::execution::v1::RunOutcome::CANCELED
              : evo::execution::v1::RunOutcome::FAILED;
      {
        std::lock_guard lock(runs_mu_);
        raw->log = std::move(log);
        raw->outcome = outcome;
        raw->done = true;
      }
      quota_gate_.release_run(raw->org_id);  // M36: run is terminal
      metrics_.counter_add(
          "evo_scheduler_runs_terminal_total",
          "Runs that reached a terminal state in this scheduler.",
          {{"outcome", evo::execution::v1::RunOutcome_Name(outcome)}}, 1.0);
      log_line("run_terminal", rid, raw->org_id,
               std::string("outcome=") +
                   evo::execution::v1::RunOutcome_Name(outcome));
    });

    resp->set_run_id(rid);
    resp->set_accepted(true);
    resp->set_message("run accepted");
    metrics_.counter_add("evo_scheduler_submissions_accepted_total",
                         "SubmitRun requests accepted for execution.",
                         {{"mode", "local"}}, 1.0);
    log_line("submit_accepted", rid, req->org_id(), "mode=local");
    return grpc::Status::OK;
  }

#ifndef EVO_HAVE_DISTRIBUTED
  // Product DAGs need the distributed run loop (Redis + Postgres + TS
  // workers). This build lacks it — fail closed rather than silently
  // mis-executing product nodes on the synthetic registry.
  grpc::Status submit_distributed(const std::string& rid,
                                  const evo::execution::v1::SubmitRunRequest* req,
                                  evo::Dag,
                                  evo::execution::v1::SubmitRunResponse*) {
    log_line("submit_rejected", rid, req->org_id(),
             "product DAG but distributed mode not built");
    return grpc::Status(grpc::UNAVAILABLE,
                        "distributed mode not available in this build");
  }
#endif  // !EVO_HAVE_DISTRIBUTED

#ifdef EVO_HAVE_DISTRIBUTED
  // --- Distributed submission (M29): drive the M26 run loop over Redis + PG.
  // Product node types execute on TypeScript workers; the loop persists
  // run/node state to Postgres and publishes normalized events for the UI. ---
  grpc::Status submit_distributed(const std::string& rid,
                                  const evo::execution::v1::SubmitRunRequest* req,
                                  evo::Dag dag,
                                  evo::execution::v1::SubmitRunResponse* resp) {
    auto entry = std::make_unique<ActiveRun>();
    entry->distributed = true;
    entry->org_id = req->org_id();
    entry->workflow_version_id = req->workflow_version_id();
    entry->steady_anchor = std::chrono::steady_clock::now();
    entry->wall_anchor = std::chrono::system_clock::now();

    // Capture the DAG's (node_id, type) pairs before moving it into the loop,
    // so GetRun can report not-yet-dispatched nodes.
    for (const auto& id : dag.node_ids()) {
      const evo::NodeSpec* spec = dag.node(id);
      entry->node_id_type.emplace_back(id.value, spec ? spec->type : "");
    }

    evo::RedisTransportConfig rcfg = phase2_redis_config_from_env();
    entry->transport = std::make_unique<evo::RedisTransport>(rcfg);
    if (!entry->transport->connect()) {
      quota_gate_.release_run(req->org_id());  // M36: give back admitted slot
      log_line("submit_rejected", rid, req->org_id(),
               "distributed Redis unreachable");
      return grpc::Status(grpc::UNAVAILABLE,
                          "distributed infra (Redis) unreachable");
    }

    entry->store =
        std::make_unique<evo::PgRunStore>(phase2_pg_config_from_env());
    if (!entry->store->connect()) {
      quota_gate_.release_run(req->org_id());  // M36: give back admitted slot
      log_line("submit_rejected", rid, req->org_id(),
               "distributed Postgres unreachable");
      return grpc::Status(grpc::UNAVAILABLE,
                          "distributed infra (Postgres) unreachable");
    }

    evo::DistributedRunConfig dcfg;
    dcfg.run_id = rid;
    dcfg.org_id = req->org_id();
    dcfg.workflow_id =
        req->workflow_id().empty() ? std::string("wf-") + rid : req->workflow_id();
    dcfg.workflow_version_id = req->workflow_version_id();
    dcfg.env_prefix = env_or("EVO_WORKER_ENV_PREFIX", "evo:dev");
    dcfg.result_group = "scheduler";
    dcfg.consumer_id = "scheduler-grpc";
    dcfg.read_block_ms = std::chrono::milliseconds(100);
    // M36: share the service-wide quota gate so the per-org in-flight task cap
    // and the global resource-class capacities are enforced ACROSS runs.
    dcfg.quota_gate = &quota_gate_;
    // M38: propagate the correlation id into every dispatched TaskEnvelope so
    // workers can tie logs/results back to this run. The app sets traceId to
    // the run id; fall back to the run id when the client omits it.
    dcfg.trace_id = req->trace_id().empty() ? rid : req->trace_id();

    // Pre-create the workers' task-stream group (mirrors the M26 E2E) so no
    // task is lost to a late "$" cursor. Idempotent (BUSYGROUP => ok).
    const std::string worker_group = env_or("EVO_WORKER_GROUP", "workers");
    entry->transport->ensure_group(evo::task_stream_key(dcfg.env_prefix),
                                   worker_group, "$");

    ActiveRun* raw = entry.get();  // heap-stable (map of unique_ptr)
    raw->loop = std::make_unique<evo::DistributedRunLoop>(
        std::move(dag), *raw->transport, *raw->store, dcfg,
        [raw](const evo::RunEvent& ev) {
          std::lock_guard lock(raw->events_mu);
          raw->events.push_back(ev);
        });

    {
      std::lock_guard lock(runs_mu_);
      register_run_locked(rid, std::move(entry));
    }

    raw->runner = std::thread([this, raw, rid]() {
      const std::string status = raw->loop->run();  // blocks
      evo::execution::v1::RunOutcome outcome =
          evo::execution::v1::RunOutcome::FAILED;
      if (status == evo::run_status::kSucceeded) {
        outcome = evo::execution::v1::RunOutcome::SUCCEEDED;
      } else if (status == evo::run_status::kCanceled) {
        outcome = evo::execution::v1::RunOutcome::CANCELED;
      }
      {
        std::lock_guard lock(runs_mu_);
        raw->outcome = outcome;
        raw->done = true;
      }
      quota_gate_.release_run(raw->org_id);  // M36: run is terminal
      metrics_.counter_add(
          "evo_scheduler_runs_terminal_total",
          "Runs that reached a terminal state in this scheduler.",
          {{"outcome", evo::execution::v1::RunOutcome_Name(outcome)}}, 1.0);
      log_line("run_terminal", rid, raw->org_id,
               std::string("outcome=") +
                   evo::execution::v1::RunOutcome_Name(outcome));
    });

    resp->set_run_id(rid);
    resp->set_accepted(true);
    resp->set_message("run accepted (distributed)");
    metrics_.counter_add("evo_scheduler_submissions_accepted_total",
                         "SubmitRun requests accepted for execution.",
                         {{"mode", "distributed"}}, 1.0);
    log_line("submit_accepted", rid, req->org_id(), "mode=distributed");
    return grpc::Status::OK;
  }

 public:
  // --- Startup reconciliation (M35): resume active runs after a restart. ---
  // On scheduler startup, identify every NON-TERMINAL run owned by the Evo
  // engine (Postgres is the durable source), reconstruct each run's DAG from
  // its persisted dag_json, and re-drive it with resume=true. The run loop
  // rebuilds logical state from durable node rows, drains the dead scheduler's
  // pending result messages, and resumes dependency scheduling — so no active
  // run is forgotten and no node is double-completed across the restart.
  //
  // Runs whose dag_json is missing/invalid (legacy / pre-M35 rows) are logged
  // and skipped: without durable topology the scheduler cannot reconstruct
  // them, and it must not guess. This is the documented small restart window
  // that is not recoverable (M35 no-go).
  void reconcile_active_runs() {
    evo::PgRunStoreConfig pcfg = phase2_pg_config_from_env();
    evo::PgRunStore lister(pcfg);
    if (!lister.connect()) {
      evo::log::warn("reconcile_pg_unreachable",
                     {{"service", "evo-scheduler"},
                      {"detail", "skipping active-run reconciliation"}});
      return;
    }
    const std::vector<std::string> active = lister.list_active_evo_run_ids();
    if (active.empty()) {
      evo::log::info("reconcile_no_active_runs",
                     {{"service", "evo-scheduler"}});
      return;
    }
    evo::log::info("reconcile_start",
                   {{"service", "evo-scheduler"},
                    {"active_runs", std::to_string(active.size())}});

    for (const std::string& rid : active) {
      auto run = lister.get_run(rid);
      if (!run.has_value()) continue;
      if (run->dag_json.empty()) {
        evo::log::warn("reconcile_skip_no_dag",
                       {{"service", "evo-scheduler"},
                        {"run_id", rid},
                        {"detail", "no durable dag_json (pre-M35)"}});
        continue;
      }
      auto parse = evo::Dag::from_json_string(run->dag_json);
      if (!parse.ok() || !parse.dag.has_value()) {
        evo::log::warn("reconcile_skip_invalid_dag",
                       {{"service", "evo-scheduler"}, {"run_id", rid}});
        continue;
      }
      evo::Dag dag = std::move(*parse.dag);

      {
        std::lock_guard lock(runs_mu_);
        if (runs_.count(rid) != 0) continue;  // already driving (idempotent)
      }

      auto entry = std::make_unique<ActiveRun>();
      entry->distributed = true;
      entry->org_id = run->org_id;
      entry->workflow_version_id = run->workflow_version_id;
      entry->steady_anchor = std::chrono::steady_clock::now();
      entry->wall_anchor = std::chrono::system_clock::now();
      for (const auto& id : dag.node_ids()) {
        const evo::NodeSpec* spec = dag.node(id);
        entry->node_id_type.emplace_back(id.value, spec ? spec->type : "");
      }

      evo::RedisTransportConfig rcfg = phase2_redis_config_from_env();
      entry->transport = std::make_unique<evo::RedisTransport>(rcfg);
      if (!entry->transport->connect()) {
        evo::log::warn("reconcile_skip_redis_unreachable",
                       {{"service", "evo-scheduler"}, {"run_id", rid}});
        continue;
      }

      entry->store = std::make_unique<evo::PgRunStore>(pcfg);
      if (!entry->store->connect()) {
        evo::log::warn("reconcile_skip_pg_unreachable",
                       {{"service", "evo-scheduler"}, {"run_id", rid}});
        continue;
      }

      evo::DistributedRunConfig dcfg;
      dcfg.run_id = rid;
      dcfg.org_id = run->org_id;
      dcfg.workflow_id = run->workflow_id;
      dcfg.workflow_version_id = run->workflow_version_id;
      dcfg.env_prefix = env_or("EVO_WORKER_ENV_PREFIX", "evo:dev");
      dcfg.result_group = "scheduler";
      // Same consumer id the pre-crash loop used, so the pending-entry list is
      // reclaimed by the same consumer on resume.
      dcfg.consumer_id = "scheduler-grpc";
      dcfg.read_block_ms = std::chrono::milliseconds(100);
      dcfg.resume = true;  // M35: reconstruct from durable state
      dcfg.quota_gate = &quota_gate_;  // M36: share the service-wide gate
      // M38: the trace id is not persisted; the app sets traceId == runId, so
      // the run id is the correct correlation id for a resumed run.
      dcfg.trace_id = rid;

      // M36: a resumed run is EXISTING durable work, not a new submission, so
      // it is RE-COUNTED (never rejected) against the org's active-run quota.
      quota_gate_.readmit_run(run->org_id);

      const std::string worker_group = env_or("EVO_WORKER_GROUP", "workers");
      entry->transport->ensure_group(evo::task_stream_key(dcfg.env_prefix),
                                     worker_group, "$");

      ActiveRun* raw = entry.get();
      raw->loop = std::make_unique<evo::DistributedRunLoop>(
          std::move(dag), *raw->transport, *raw->store, dcfg,
          [raw](const evo::RunEvent& ev) {
            std::lock_guard lock(raw->events_mu);
            raw->events.push_back(ev);
          });

      {
        std::lock_guard lock(runs_mu_);
        register_run_locked(rid, std::move(entry));
      }

      raw->runner = std::thread([this, raw, rid]() {
        const std::string status = raw->loop->run();  // blocks
        evo::execution::v1::RunOutcome outcome =
            evo::execution::v1::RunOutcome::FAILED;
        if (status == evo::run_status::kSucceeded) {
          outcome = evo::execution::v1::RunOutcome::SUCCEEDED;
        } else if (status == evo::run_status::kCanceled) {
          outcome = evo::execution::v1::RunOutcome::CANCELED;
        }
        {
          std::lock_guard lock(runs_mu_);
          raw->outcome = outcome;
          raw->done = true;
        }
        quota_gate_.release_run(raw->org_id);  // M36: run is terminal
        metrics_.counter_add(
            "evo_scheduler_runs_terminal_total",
            "Runs that reached a terminal state in this scheduler.",
            {{"outcome", evo::execution::v1::RunOutcome_Name(outcome)}}, 1.0);
        log_line("run_terminal", rid, raw->org_id,
                 std::string("outcome=") +
                     evo::execution::v1::RunOutcome_Name(outcome) +
                     " (resumed)");
      });

      evo::log::info("reconcile_resumed_run",
                     {{"service", "evo-scheduler"},
                      {"run_id", rid},
                      {"org_id", run->org_id}});
    }
  }

  // Fill GetRun from the loop's normalized events (thread-safe: events_mu).
  grpc::Status fill_run_distributed(
      const ActiveRun& e, evo::execution::v1::GetRunResponse* resp) {
    using evo::execution::v1::NodeState;
    using evo::execution::v1::RunStatus;

    struct NodeView {
      std::string type;
      std::string output;
      std::string error;
      NodeState state = NodeState::NODE_STATE_BLOCKED;
      bool started = false;
      bool finished = false;
      std::int64_t started_ms = 0;
      std::int64_t finished_ms = 0;
    };
    std::map<std::string, NodeView> views;
    {
      std::lock_guard lock(e.events_mu);
      for (const auto& ev : e.events) {
        if (ev.node_id.empty()) continue;  // run-level event
        NodeView& v = views[ev.node_id];
        if (ev.kind == "node_dispatched") {
          v.type = ev.detail;
          v.state = NodeState::NODE_STATE_RUNNING;
          v.started = true;
          v.started_ms = ev.wall_ms;
        } else if (ev.kind == "node_succeeded") {
          v.state = NodeState::NODE_STATE_SUCCEEDED;
          v.output = ev.detail;
          v.finished = true;
          v.finished_ms = ev.wall_ms;
        } else if (ev.kind == "node_failed") {
          v.state = NodeState::NODE_STATE_FAILED;
          v.error = ev.detail;
          v.finished = true;
          v.finished_ms = ev.wall_ms;
        } else if (ev.kind == "node_canceled") {
          if (v.state != NodeState::NODE_STATE_SUCCEEDED &&
              v.state != NodeState::NODE_STATE_FAILED) {
            v.state = NodeState::NODE_STATE_CANCELED;
          }
          v.finished = true;
          v.finished_ms = ev.wall_ms;
        } else if (ev.kind == "node_retry_scheduled") {
          // M32: parked for backoff — still in progress (not terminal).
          v.state = NodeState::NODE_STATE_RUNNING;
        } else if (ev.kind == "node_dead_lettered") {
          // M32: retries exhausted — terminal failure.
          v.state = NodeState::NODE_STATE_DEAD_LETTER;
          v.error = ev.detail;
          v.finished = true;
          v.finished_ms = ev.wall_ms;
        }
      }
    }

    // Report every DAG node (default BLOCKED for not-yet-dispatched).
    for (const auto& [nid, ntype] : e.node_id_type) {
      auto* ns = resp->add_nodes();
      ns->set_node_id(nid);
      ns->set_attempt_number(1);
      auto it = views.find(nid);
      if (it == views.end()) {
        ns->set_node_type(ntype);
        ns->set_state(NodeState::NODE_STATE_BLOCKED);
        continue;
      }
      const NodeView& v = it->second;
      ns->set_node_type(v.type.empty() ? ntype : v.type);
      ns->set_state(v.state);
      ns->set_output(v.output);
      ns->set_error(v.error);
      if (v.started) set_timestamp_ms(ns->mutable_started_at(), v.started_ms);
      if (v.finished) {
        set_timestamp_ms(ns->mutable_finished_at(), v.finished_ms);
      }
    }

    if (!e.done) {
      resp->set_status(RunStatus::RUN_RUNNING);
      resp->set_outcome(evo::execution::v1::RunOutcome::OUTCOME_UNSPECIFIED);
      return grpc::Status::OK;
    }
    resp->set_outcome(e.outcome);
    set_timestamp(resp->mutable_finished_at(), e.wall_anchor);
    switch (e.outcome) {
      case evo::execution::v1::RunOutcome::SUCCEEDED:
        resp->set_status(RunStatus::RUN_SUCCEEDED);
        break;
      case evo::execution::v1::RunOutcome::CANCELED:
        resp->set_status(RunStatus::RUN_CANCELED);
        break;
      case evo::execution::v1::RunOutcome::FAILED:
        resp->set_status(RunStatus::RUN_FAILED);
        break;
      default:
        resp->set_status(RunStatus::RUN_RUNNING);
        break;
    }
    return grpc::Status::OK;
  }
#endif  // EVO_HAVE_DISTRIBUTED

 private:

  // Fill GetRun from the local ConcurrentScheduler's run log (the M17 path).
  grpc::Status fill_run_local(const ActiveRun& e,
                              evo::execution::v1::GetRunResponse* resp) {
    using evo::execution::v1::NodeState;
    using evo::execution::v1::RunStatus;

    auto to_wall = [&](std::chrono::steady_clock::time_point sp) {
      // Bridge steady -> wall via the per-run anchor pair. Cast explicitly:
      // steady/system durations differ on libc++ (ns vs us), so plain
      // addition would yield a different time_point specialization.
      return std::chrono::system_clock::time_point(
          e.wall_anchor.time_since_epoch() +
          std::chrono::duration_cast<std::chrono::system_clock::duration>(
              sp - e.steady_anchor));
    };

    for (const auto& r : e.log.runs) {
      auto* ns = resp->add_nodes();
      ns->set_node_id(r.id.value);
      ns->set_node_type(r.type);
      ns->set_attempt_number(1);
      const bool started = r.started_at.time_since_epoch().count() != 0;
      if (r.ok()) {
        ns->set_state(NodeState::NODE_STATE_SUCCEEDED);
      } else if (!started) {
        ns->set_state(NodeState::NODE_STATE_CANCELED);
      } else {
        ns->set_state(NodeState::NODE_STATE_FAILED);
      }
      ns->set_output(r.result.output);
      if (started) set_timestamp(ns->mutable_started_at(), to_wall(r.started_at));
      if (r.finished_at.time_since_epoch().count() != 0) {
        set_timestamp(ns->mutable_finished_at(), to_wall(r.finished_at));
      }
    }

    if (!e.done) {
      resp->set_status(RunStatus::RUN_RUNNING);
      resp->set_outcome(evo::execution::v1::RunOutcome::OUTCOME_UNSPECIFIED);
      return grpc::Status::OK;
    }
    resp->set_outcome(e.outcome);
    set_timestamp(resp->mutable_finished_at(), e.wall_anchor);
    switch (e.outcome) {
      case evo::execution::v1::RunOutcome::SUCCEEDED:
        resp->set_status(RunStatus::RUN_SUCCEEDED);
        break;
      case evo::execution::v1::RunOutcome::CANCELED:
        resp->set_status(RunStatus::RUN_CANCELED);
        break;
      case evo::execution::v1::RunOutcome::FAILED:
        resp->set_status(RunStatus::RUN_FAILED);
        break;
      default:
        resp->set_status(RunStatus::RUN_RUNNING);
        break;
    }
    return grpc::Status::OK;
  }

  std::mutex runs_mu_;
  std::map<std::string, std::unique_ptr<ActiveRun>> runs_;  // stable addresses
  // Submission-order index backing bounded retention (register_run_locked).
  std::deque<std::string> run_order_;

  // Retention bound. GetRun serves in-memory state only while an entry lives
  // here, so terminal runs are pruned oldest-first once the registry exceeds
  // kMaxRetainedRuns — previously the map grew without bound over a long
  // uptime (every completed ActiveRun kept its full log/events forever).
  // Durable history is the Postgres run store's job; the app reconstructs
  // older runs from workflow_runs/node_runs, not from this process.
  static constexpr size_t kMaxRetainedRuns = 1024;

  // Insert a run, then prune. Caller holds runs_mu_.
  //
  // Pruning JOINS a finished runner before erasing its entry: the runner
  // lambdas read raw->org_id / bump metrics AFTER publishing done=true under
  // runs_mu_, so erase-without-join could free ActiveRun mid-tail. The join is
  // cheap there — done=true means only the metrics/log tail can remain — and
  // it never deadlocks because that tail never re-acquires runs_mu_.
  void register_run_locked(const std::string& rid,
                           std::unique_ptr<ActiveRun> entry) {
    run_order_.push_back(rid);
    runs_.emplace(rid, std::move(entry));

    size_t pruned = 0;
    while (runs_.size() > kMaxRetainedRuns && !run_order_.empty()) {
      const auto it = runs_.find(run_order_.front());
      if (it == runs_.end()) {
        run_order_.pop_front();  // already gone (defensive)
        continue;
      }
      if (!it->second->done) break;  // oldest not terminal — keep ordering
      if (it->second->runner.joinable()) it->second->runner.join();
      runs_.erase(it);
      run_order_.pop_front();
      ++pruned;
    }
    if (pruned > 0) {
      evo::log::info("runs_registry_pruned",
                     {{"service", "evo-scheduler"},
                      {"detail", "pruned=" + std::to_string(pruned)}});
    }
  }

  // M36: service-wide multi-tenant quota gate. Shared by every run loop (via
  // DistributedRunConfig.quota_gate) and consulted at SubmitRun admission.
  // Configured from env (0 => unlimited, backwards compatible):
  //   EVO_QUOTA_MAX_ACTIVE_RUNS_PER_ORG   per-org active-run cap
  //   EVO_QUOTA_MAX_ACTIVE_RUNS_GLOBAL    global active-run cap
  //   EVO_QUOTA_MAX_INFLIGHT_TASKS_PER_ORG per-org in-flight task cap
  //   EVO_QUOTA_BROWSER_CAPACITY          global browser-session capacity
  //   EVO_QUOTA_EXTERNAL_IO_CAPACITY      global side-effect capacity
  // M37 fair scheduling (opt-in; default off = M36 first-come-first-served):
  //   EVO_FAIR_SCHEDULING                 "1" => weighted fair tenant selection
  //   EVO_ORG_WEIGHTS                     "org-a:2,org-b:1" explicit weights
  //   EVO_FAIR_DEMAND_TIMEOUT_MS          demand-freshness window (default 5000)
  evo::TenantQuotaGate quota_gate_{make_quota_config()};

  // M38: service-wide metrics registry, rendered at GET /metrics (Prometheus
  // text format) by the loopback metrics HTTP server. Counters are bumped on
  // the submit/terminal paths below; the active-run gauge is derived from the
  // runs_ map at render time.
  evo::metrics::MetricsRegistry metrics_;

  // M38: shared engine token for service-to-service auth. Read once from
  // EVO_ENGINE_TOKEN at construction; empty => auth disabled (backwards
  // compatible). Never logged, never exposed to a browser.
  const std::string engine_token_{env_or("EVO_ENGINE_TOKEN", "")};

 public:
  // Render the full Prometheus document: service counters + quota gate
  // counters + the current active-run gauge. Called per /metrics scrape.
  // Public so main() can hand it to the loopback metrics HTTP server.
  std::string render_metrics() {
    {
      std::lock_guard lock(runs_mu_);
      // Count only non-terminal entries: the registry retains finished runs
      // up to the retention bound, so its size is no longer "active".
      size_t active = 0;
      for (const auto& [rid, entry] : runs_) {
        if (!entry->done) ++active;
      }
      metrics_.gauge_set("evo_scheduler_active_runs",
                         "Runs currently active (not terminal) in this scheduler.",
                         {}, static_cast<double>(active));
    }
    const auto qc = quota_gate_.counters();
    metrics_.counter_set(
        "evo_quota_admitted_runs_total",
        "Runs admitted by the tenant quota gate since process start.", {},
        static_cast<double>(qc.admitted_runs));
    metrics_.counter_set(
        "evo_quota_rejected_runs_total",
        "Runs rejected (RESOURCE_EXHAUSTED) by the tenant quota gate.", {},
        static_cast<double>(qc.rejected_runs));
    metrics_.counter_set(
        "evo_quota_acquired_tasks_total",
        "Task slots acquired from the tenant quota gate.", {},
        static_cast<double>(qc.acquired_tasks));
    metrics_.counter_set(
        "evo_quota_deferred_tasks_total",
        "Task dispatches deferred by the tenant quota gate (backpressure).", {},
        static_cast<double>(qc.deferred_tasks));
    metrics_.counter_set(
        "evo_quota_fair_order_deferrals_total",
        "Task dispatches deferred by fair-scheduling tenant ordering (M37).", {},
        static_cast<double>(qc.fair_order_deferrals));
    return metrics_.render();
  }

 private:
  static evo::QuotaConfig make_quota_config() {
    evo::QuotaConfig cfg;
    cfg.max_active_runs_per_org =
        std::atoi(env_or("EVO_QUOTA_MAX_ACTIVE_RUNS_PER_ORG", "0").c_str());
    cfg.max_active_runs_global =
        std::atoi(env_or("EVO_QUOTA_MAX_ACTIVE_RUNS_GLOBAL", "0").c_str());
    cfg.max_inflight_tasks_per_org =
        std::atoi(env_or("EVO_QUOTA_MAX_INFLIGHT_TASKS_PER_ORG", "0").c_str());
    const int browser_cap =
        std::atoi(env_or("EVO_QUOTA_BROWSER_CAPACITY", "0").c_str());
    if (browser_cap > 0) {
      cfg.global_class_capacity[evo::ResourceClass::Browser] = browser_cap;
    }
    const int io_cap =
        std::atoi(env_or("EVO_QUOTA_EXTERNAL_IO_CAPACITY", "0").c_str());
    if (io_cap > 0) {
      cfg.global_class_capacity[evo::ResourceClass::ExternalIo] = io_cap;
    }
    // M37: fair scheduling is an explicit opt-in (default off preserves the
    // M36 first-come-first-served behavior).
    cfg.fair_scheduling = env_or("EVO_FAIR_SCHEDULING", "0") == "1";
    cfg.fair_demand_timeout_ms =
        std::atoll(env_or("EVO_FAIR_DEMAND_TIMEOUT_MS", "0").c_str());
    // Explicit per-org weights: "org-a:2,org-b:1". Absent orgs default to 1.
    const std::string weights = env_or("EVO_ORG_WEIGHTS", "");
    std::size_t pos = 0;
    while (pos < weights.size()) {
      const std::size_t comma = weights.find(',', pos);
      const std::string pair =
          weights.substr(pos, comma == std::string::npos ? std::string::npos
                                                         : comma - pos);
      const std::size_t colon = pair.find(':');
      if (colon != std::string::npos && colon > 0 && colon + 1 < pair.size()) {
        const std::string org = pair.substr(0, colon);
        const int w = std::atoi(pair.substr(colon + 1).c_str());
        if (w >= 1) cfg.org_weights[org] = w;
      }
      if (comma == std::string::npos) break;
      pos = comma + 1;
    }
    return cfg;
  }
};

}  // namespace evo::service

int main() {
  // Safe local default; override with EVO_SCHEDULER_ADDR. Never bind 0.0.0.0
  // by default (M17 no-go: no unauthenticated non-local exposure).
  const char* env_addr = std::getenv("EVO_SCHEDULER_ADDR");
  const std::string addr = env_addr ? env_addr : "127.0.0.1:50051";

  evo::service::ControlServiceImpl service;

#ifdef EVO_HAVE_DISTRIBUTED
  // M35: resume active evo runs from the durable store BEFORE accepting new
  // RPCs, so GetRun/CancelRun can see recovered runs from the first request.
  // Best-effort: unreachable infra or unrecoverable runs are logged/skipped,
  // never fatal (the server must still start for fresh submissions).
  service.reconcile_active_runs();
#endif

  grpc::ServerBuilder builder;
  builder.AddListeningPort(addr, grpc::InsecureServerCredentials());
  builder.RegisterService(&service);
  builder.SetMaxMessageSize(64 * 1024 * 1024);  // 64MB DAG payloads
  std::shared_ptr<grpc::Server> server(builder.BuildAndStart());
  if (!server) {
    evo::log::error("grpc_bind_failed",
                    {{"service", "evo-scheduler"}, {"addr", addr}});
    return 1;
  }
  evo::log::info("grpc_listening",
                 {{"service", "evo-scheduler"},
                  {"addr", addr},
                  {"commit", EVO_BUILD_COMMIT}});

  // M38: loopback Prometheus metrics endpoint (GET /metrics). Opt-in port via
  // EVO_METRICS_PORT (default 9090); set EVO_METRICS_PORT=0 to disable. A bind
  // failure is logged and non-fatal — the scheduler still serves gRPC.
  std::unique_ptr<evo::service::MetricsHttpServer> metrics_server;
  const char* metrics_port_env = std::getenv("EVO_METRICS_PORT");
  const int metrics_port =
      (metrics_port_env && *metrics_port_env) ? std::atoi(metrics_port_env) : 9090;
  if (metrics_port > 0) {
    metrics_server = std::make_unique<evo::service::MetricsHttpServer>(
        metrics_port, [&service] { return service.render_metrics(); });
    if (!metrics_server->start()) {
      evo::log::warn("metrics_http_bind_failed",
                     {{"service", "evo-scheduler"},
                      {"port", std::to_string(metrics_port)}});
      metrics_server.reset();
    }
  }

  // Block SIGINT/SIGTERM in all threads, then wait for one on a dedicated
  // thread that triggers a clean Shutdown() (async-signal-safe, no busy-spin).
  sigset_t sigset;
  sigemptyset(&sigset);
  sigaddset(&sigset, SIGINT);
  sigaddset(&sigset, SIGTERM);
  pthread_sigmask(SIG_BLOCK, &sigset, nullptr);
  std::thread signal_thread([&server, &sigset]() {
    int sig = 0;
    sigwait(&sigset, &sig);  // blocks until SIGINT/SIGTERM
    // sigwait returns in normal thread context (not a signal handler), so
    // structured logging here is safe.
    evo::log::info("signal_received",
                   {{"service", "evo-scheduler"},
                    {"signal", std::to_string(sig)},
                    {"detail", "shutting down"}});
    server->Shutdown();
  });

  server->Wait();  // returns once Shutdown() is called
  if (signal_thread.joinable()) signal_thread.join();

  service.shutdown();  // drain in-flight runs
  evo::log::info("stopped_cleanly", {{"service", "evo-scheduler"}});
  return 0;
}
