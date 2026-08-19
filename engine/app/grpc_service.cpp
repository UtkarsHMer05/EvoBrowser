// Milestone 17: C++ scheduler service over gRPC.
//
// Wraps the scheduler core (M10–M13) behind the versioned ControlService
// (engine/proto/evo/execution.proto, generated stubs). Submits runs
// asynchronously, tracks them in an in-memory registry (local mode — durable
// Redis/Postgres state is M21+/M31, not this milestone), and serves
// GetRun/CancelRun/Health. Executes the DAG via the ConcurrentScheduler with a
// synthetic bench-task registry (no real browser/Stagehand work here, per the
// M16/M17 no-go; real node execution is delegated to TS workers in M23–M24).
//
// Correctness notes:
//  - Run entries are heap-stable (std::map of std::unique_ptr<ActiveRun>) so a
//    runner thread holding a raw pointer is never invalidated by rehash/insert.
//  - The runner thread publishes its result under runs_mu_; GetRun/CancelRun
//    read under the same lock, so there is no data race on log/outcome/done.
//  - Graceful shutdown: a dedicated sigwait thread calls server->Shutdown() on
//    SIGINT/SIGTERM (async-signal-safe; no busy-spin), then in-flight runs are
//    canceled and their runner threads joined before exit.
//  - Timestamps crossing the service boundary are wall-clock UTC (§7). The
//    scheduler records steady_clock internally; we bridge steady->wall with a
//    per-run (steady,wall) anchor pair captured at submit time.
//  - Structured logs carry run/org identifiers only; never secrets.

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

#include <grpcpp/grpcpp.h>

#include "evo/bench.hpp"
#include "evo/concurrent_scheduler.hpp"
#include "evo/dag.hpp"
#include "evo/execution.grpc.pb.h"
#include "evo/scheduler.hpp"

namespace evo::service {

namespace {

void log_line(const std::string& event, const std::string& run_id,
              const std::string& org_id, const std::string& detail = "") {
  // Structured, secret-free log: event + identifiers only.
  std::fprintf(stderr, "[evo-scheduler] event=%s run_id=%s org_id=%s%s%s\n",
               event.c_str(), run_id.empty() ? "-" : run_id.c_str(),
               org_id.empty() ? "-" : org_id.c_str(),
               detail.empty() ? "" : " detail=", detail.c_str());
}

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

}  // namespace

struct ActiveRun {
  std::unique_ptr<evo::ConcurrentScheduler> scheduler;
  std::thread runner;
  // Published under runs_mu_ exactly once at completion:
  bool done = false;
  evo::ConcurrentRunLog log;
  evo::execution::v1::RunOutcome outcome =
      evo::execution::v1::RunOutcome::OUTCOME_UNSPECIFIED;
  // steady->wall anchors captured at submit (for boundary timestamps).
  std::chrono::steady_clock::time_point steady_anchor;
  std::chrono::system_clock::time_point wall_anchor;
  std::string org_id;
  std::string workflow_version_id;
};

class ControlServiceImpl final
    : public evo::execution::v1::ControlService::Service {
 public:
  grpc::Status SubmitRun(grpc::ServerContext*,
                         const evo::execution::v1::SubmitRunRequest* req,
                         evo::execution::v1::SubmitRunResponse* resp) override {
    const std::string rid = req->run_id();
    if (rid.empty()) {
      return grpc::Status(grpc::INVALID_ARGUMENT, "run_id is required");
    }

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

    // Validate + parse the canonical DAG JSON into an engine Dag before
    // mutating any state (trust-boundary rule: validate before durable write).
    auto parse = evo::Dag::from_json_string(req->dag_json());
    if (!parse.ok() || !parse.dag.has_value()) {
      std::string err = "malformed or invalid DAG";
      if (!parse.errors.empty()) err += ": " + parse.errors[0].message;
      log_line("submit_rejected", rid, req->org_id(), err);
      return grpc::Status(grpc::INVALID_ARGUMENT, err);
    }
    evo::Dag dag = std::move(*parse.dag);

    // Default small sleep per node for the local synthetic executor.
    std::map<evo::NodeId, int> sleep_ms;
    for (const auto& id : dag.node_ids()) sleep_ms[id] = 3;

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
      runs_.emplace(rid, std::move(entry));
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
      log_line("run_terminal", rid, raw->org_id,
               std::string("outcome=") +
                   evo::execution::v1::RunOutcome_Name(outcome));
    });

    resp->set_run_id(rid);
    resp->set_accepted(true);
    resp->set_message("run accepted");
    log_line("submit_accepted", rid, req->org_id());
    return grpc::Status::OK;
  }

  grpc::Status CancelRun(grpc::ServerContext*,
                         const evo::execution::v1::CancelRunRequest* req,
                         evo::execution::v1::CancelRunResponse* resp) override {
    std::lock_guard lock(runs_mu_);
    auto it = runs_.find(req->run_id());
    if (it == runs_.end()) {
      resp->set_ok(false);
      resp->set_outcome(evo::execution::v1::RunOutcome::OUTCOME_UNSPECIFIED);
      return grpc::Status(grpc::NOT_FOUND, "run not found");
    }
    it->second->scheduler->cancel();
    resp->set_ok(true);
    resp->set_outcome(evo::execution::v1::RunOutcome::CANCELED);
    log_line("cancel_requested", req->run_id(), it->second->org_id,
             req->reason());
    return grpc::Status::OK;
  }

  grpc::Status GetRun(grpc::ServerContext*,
                      const evo::execution::v1::GetRunRequest* req,
                      evo::execution::v1::GetRunResponse* resp) override {
    std::lock_guard lock(runs_mu_);
    auto it = runs_.find(req->run_id());
    if (it == runs_.end()) {
      return grpc::Status(grpc::NOT_FOUND, "run not found");
    }
    const ActiveRun& e = *it->second;
    resp->set_run_id(req->run_id());
    resp->set_workflow_version_id(e.workflow_version_id);
    set_timestamp(resp->mutable_created_at(), e.wall_anchor);

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
    set_timestamp(resp->mutable_finished_at(), e.wall_anchor);  // refined below
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

  grpc::Status Health(grpc::ServerContext*,
                      const evo::execution::v1::HealthRequest*,
                      evo::execution::v1::HealthResponse* resp) override {
    resp->set_ok(true);
    resp->set_detail("SERVING");
    return grpc::Status::OK;
  }

  // Graceful drain: cancel every run, then join all runner threads. Called
  // after the gRPC server has stopped accepting new RPCs.
  void shutdown() {
    std::map<std::string, ActiveRun*> to_join;
    {
      std::lock_guard lock(runs_mu_);
      for (auto& [rid, e] : runs_) {
        e->scheduler->cancel();
        to_join.emplace(rid, e.get());
      }
    }
    for (auto& [rid, e] : to_join) {
      if (e->runner.joinable()) e->runner.join();
    }
    log_line("shutdown_complete", "", "");
  }

 private:
  std::mutex runs_mu_;
  std::map<std::string, std::unique_ptr<ActiveRun>> runs_;  // stable addresses
};

}  // namespace evo::service

int main() {
  // Safe local default; override with EVO_SCHEDULER_ADDR. Never bind 0.0.0.0
  // by default (M17 no-go: no unauthenticated non-local exposure).
  const char* env_addr = std::getenv("EVO_SCHEDULER_ADDR");
  const std::string addr = env_addr ? env_addr : "127.0.0.1:50051";

  evo::service::ControlServiceImpl service;
  grpc::ServerBuilder builder;
  builder.AddListeningPort(addr, grpc::InsecureServerCredentials());
  builder.RegisterService(&service);
  builder.SetMaxMessageSize(64 * 1024 * 1024);  // 64MB DAG payloads
  std::shared_ptr<grpc::Server> server(builder.BuildAndStart());
  if (!server) {
    std::fprintf(stderr, "[evo-scheduler] failed to bind %s\n", addr.c_str());
    return 1;
  }
  std::fprintf(stderr, "[evo-scheduler] listening on %s commit=%s\n",
               addr.c_str(), EVO_BUILD_COMMIT);

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
    std::fprintf(stderr, "[evo-scheduler] signal %d received; shutting down\n",
                 sig);
    server->Shutdown();
  });

  server->Wait();  // returns once Shutdown() is called
  if (signal_thread.joinable()) signal_thread.join();

  service.shutdown();  // drain in-flight runs
  std::fprintf(stderr, "[evo-scheduler] stopped cleanly\n");
  return 0;
}
