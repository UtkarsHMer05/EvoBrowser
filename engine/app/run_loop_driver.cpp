// M35 scheduler driver binary (used by scheduler_restart_test).
//
// Runs ONE DistributedRunLoop against real Redis + Postgres so the restart
// integration test can SIGKILL the scheduler as a REAL process (a crash, not a
// graceful stop()) and then restart it with resume=true. The test asserts the
// run reaches a consistent terminal outcome across the kill/restart boundary.
//
// Configuration (env):
//   EVO_DRIVER_RUN_ID      run id to drive (required)
//   EVO_DRIVER_PREFIX      stream namespace prefix (required)
//   EVO_DRIVER_RESUME      "1" => resume mode (reconstruct from durable store)
//   EVO_DRIVER_SLOW_MS     bench:sleep duration for the slow node (default 4000)
//   EVO_PHASE2_REDIS_HOST / EVO_PHASE2_REDIS_PORT   Redis endpoint
//   EVO_PHASE2_PG_HOST / EVO_PHASE2_PG_PORT / EVO_PHASE2_PG_USER /
//   EVO_PHASE2_PG_PASSWORD / EVO_PHASE2_PG_DB       Postgres endpoint
//
// DAG (fixed, mirrors the M34 crash DAG):
//   start(bench:echo) -> { slow(bench:sleep), quick(bench:echo) } -> join
// The trigger's node TYPE is "bench:echo" (not "start"): the worker's
// synthetic executor has no "start" case (see crash_recovery_test.cpp).
//
// Exit code: 0 when the run reaches SUCCEEDED, 1 otherwise. Prints the final
// status line to stdout for the test to parse.

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <string>

#include "evo/dag.hpp"
#include "evo/distributed_run_loop.hpp"
#include "evo/pg_run_store.hpp"
#include "evo/redis_transport.hpp"

using namespace std::chrono_literals;

namespace {

std::string env_or(const char* name, const std::string& fallback) {
  const char* v = std::getenv(name);
  return (v && *v) ? std::string(v) : fallback;
}

evo::Dag make_driver_dag() {
  std::vector<evo::NodeSpec> nodes = {
      {evo::NodeId{"start"}, evo::NodeKind::Trigger, "bench:echo"},
      {evo::NodeId{"slow"}, evo::NodeKind::Action, "bench:sleep"},
      {evo::NodeId{"quick"}, evo::NodeKind::Action, "bench:echo"},
      {evo::NodeId{"join"}, evo::NodeKind::Action, "bench:echo"},
  };
  std::vector<evo::Edge> edges = {
      {evo::NodeId{"start"}, evo::NodeId{"slow"}},
      {evo::NodeId{"start"}, evo::NodeId{"quick"}},
      {evo::NodeId{"slow"}, evo::NodeId{"join"}},
      {evo::NodeId{"quick"}, evo::NodeId{"join"}},
  };
  auto br = evo::Dag::build(nodes, edges);
  return std::move(*br.dag);
}

}  // namespace

int main() {
  const std::string run_id = env_or("EVO_DRIVER_RUN_ID", "");
  const std::string prefix = env_or("EVO_DRIVER_PREFIX", "");
  const bool resume = env_or("EVO_DRIVER_RESUME", "0") == "1";
  if (run_id.empty() || prefix.empty()) {
    printf("driver: EVO_DRIVER_RUN_ID and EVO_DRIVER_PREFIX are required\n");
    return 2;
  }

  evo::RedisTransportConfig rcfg;
  rcfg.host = env_or("EVO_PHASE2_REDIS_HOST", "127.0.0.1");
  rcfg.port = std::atoi(env_or("EVO_PHASE2_REDIS_PORT", "6390").c_str());
  evo::RedisTransport transport(rcfg);
  if (!transport.connect()) {
    printf("driver: Redis unreachable at %s:%d\n", rcfg.host.c_str(),
           rcfg.port);
    return 2;
  }

  evo::PgRunStoreConfig pcfg;
  pcfg.host = env_or("EVO_PHASE2_PG_HOST", "127.0.0.1");
  pcfg.port = std::atoi(env_or("EVO_PHASE2_PG_PORT", "5433").c_str());
  pcfg.user = env_or("EVO_PHASE2_PG_USER", "evo");
  pcfg.password = env_or("EVO_PHASE2_PG_PASSWORD", "evo_dev_password");
  pcfg.dbname = env_or("EVO_PHASE2_PG_DB", "evo_phase2");
  evo::PgRunStore store(pcfg);
  if (!store.connect()) {
    printf("driver: Postgres unreachable at %s:%d\n", pcfg.host.c_str(),
           pcfg.port);
    return 2;
  }

  // Pre-create the workers' task-stream group (mirrors grpc_service / the M26
  // E2E) so no task is lost to a late "$" cursor. Idempotent.
  transport.ensure_group(evo::task_stream_key(prefix), "workers", "$");

  evo::DistributedRunConfig cfg;
  cfg.run_id = run_id;
  cfg.org_id = "org-m35";
  cfg.workflow_id = "35353535-3535-3535-3535-353535353535";
  cfg.env_prefix = prefix;
  cfg.result_group = "scheduler";
  cfg.consumer_id = "scheduler-1";  // stable across kill/restart (PEL reclaim)
  cfg.read_block_ms = 50ms;
  cfg.run_timeout = 60s;
  // Short leases + fast scan (match the workers' cadence, set by the test).
  cfg.lease_duration = 1500ms;
  cfg.lease_initial_duration = 10s;
  cfg.lease_scan_interval = 100ms;
  cfg.resume = resume;
  cfg.node_payloads["slow"] =
      "{\"ms\":" + env_or("EVO_DRIVER_SLOW_MS", "4000") + "}";

  evo::DistributedRunLoop loop(make_driver_dag(), transport, store, cfg);
  const std::string status = loop.run();

  printf("driver: run=%s resume=%d final_status=%s\n", run_id.c_str(),
         resume ? 1 : 0, status.c_str());
  return status == evo::run_status::kSucceeded ? 0 : 1;
}
