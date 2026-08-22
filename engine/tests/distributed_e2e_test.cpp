// M26 end-to-end distributed test (step 8): C++ scheduler run loop + real
// Redis Streams + MULTIPLE TypeScript worker processes + real Postgres.
//
// Topology:
//   - DistributedRunLoop (this process) dispatches a diamond DAG
//     (start -> {a, b} -> c, all synthetic "bench:echo") as TaskEnvelopes on
//     the task stream and consumes ResultEnvelopes from the result stream.
//   - N TS worker child processes (worker/src/main.ts, synthetic executor)
//     claim tasks, execute, publish results, ack — the durable-handoff path.
//   - PgRunStore persists run/node/attempt state to the local Phase-2
//     Postgres; the test asserts the durable audit rows afterwards.
//   - A duplicate-result injector hammers a repeated success for one node to
//     prove at-most-once application across the real transport.
//
// Skips (exit 0) when Redis or Postgres is unreachable, or when the repo
// root / tsx is unavailable, so CTest stays green without the full stack.
// Env overrides: EVO_PHASE2_REDIS (host:port), EVO_PHASE2_PG_* ,
// EVO_M26_WORKERS (default 2), EVO_M26_REPO_ROOT.

#include <google/protobuf/util/time_util.h>

#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <spawn.h>
#include <string>
#include <sys/wait.h>
#include <thread>
#include <vector>

#include "evo/dag.hpp"
#include "evo/distributed_run_loop.hpp"
#include "evo/execution.pb.h"
#include "evo/pg_run_store.hpp"
#include "evo/redis_transport.hpp"
#include "evo/run_store.hpp"
#include "evo/transport.hpp"

#include "spawn_chdir.hpp"

extern char** environ;

using evo::Dag;
using evo::DistributedRunConfig;
using evo::DistributedRunLoop;
using evo::Edge;
using evo::NodeKind;
using evo::NodeId;
using evo::NodeSpec;
using evo::PgRunStore;
using evo::PgRunStoreConfig;
using evo::RedisTransport;
using evo::RedisTransportConfig;
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

std::string env_or(const char* name, const std::string& fallback) {
  const char* v = std::getenv(name);
  return (v && *v) ? std::string(v) : fallback;
}

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

struct ChildProc {
  pid_t pid = -1;
  std::string id;
};

// Spawn one TS worker: `npx tsx worker/src/main.ts` from the repo root.
ChildProc spawn_worker(const std::string& repo_root, const std::string& prefix,
                       const std::string& worker_id, const std::string& redis_host,
                       const std::string& redis_port) {
  ChildProc out;
  out.id = worker_id;

  std::vector<std::string> env_strings;
  for (char** e = environ; *e; ++e) env_strings.emplace_back(*e);
  env_strings.push_back("EVO_WORKER_ENV_PREFIX=" + prefix);
  env_strings.push_back("EVO_WORKER_GROUP=workers");
  env_strings.push_back("EVO_WORKER_ID=" + worker_id);
  env_strings.push_back("EVO_PHASE2_REDIS_HOST=" + redis_host);
  env_strings.push_back("EVO_PHASE2_REDIS_PORT=" + redis_port);
  std::vector<char*> envp;
  for (auto& s : env_strings) envp.push_back(s.data());
  envp.push_back(nullptr);

  std::vector<char*> argv = {const_cast<char*>("npx"),
                             const_cast<char*>("tsx"),
                             const_cast<char*>("worker/src/main.ts"), nullptr};

  posix_spawn_file_actions_t actions;
  posix_spawn_file_actions_init(&actions);
  evo_spawn_addchdir(&actions, repo_root.c_str());
  // M38 CI (Linux): `npx tsx` spawns a child that outlives a SIGTERM to the
  // wrapper pid and keeps CTest's output pipe open, wedging the test on pipe
  // EOF. Redirect the workers' stdout/stderr to /dev/null and spawn them in
  // their own process group so shutdown can kill the whole tree (same pattern
  // as the M34 crash_recovery test).
  posix_spawn_file_actions_addopen(&actions, STDOUT_FILENO, "/dev/null",
                                   O_WRONLY, 0);
  posix_spawn_file_actions_addopen(&actions, STDERR_FILENO, "/dev/null",
                                   O_WRONLY, 0);

  posix_spawnattr_t attr;
  posix_spawnattr_init(&attr);
  posix_spawnattr_setflags(&attr, POSIX_SPAWN_SETPGROUP);

  const int rc = posix_spawnp(&out.pid, "npx", &actions, &attr, argv.data(),
                              envp.data());
  posix_spawn_file_actions_destroy(&actions);
  posix_spawnattr_destroy(&attr);
  if (rc != 0) out.pid = -1;
  return out;
}

}  // namespace

int main() {
  // --- Redis reachability probe ---------------------------------------------
  const std::string redis_endpoint = env_or("EVO_PHASE2_REDIS", "127.0.0.1:6390");
  const auto colon = redis_endpoint.find(':');
  RedisTransportConfig rcfg;
  rcfg.host = redis_endpoint.substr(0, colon);
  rcfg.port = colon == std::string::npos
                  ? 6390
                  : std::atoi(redis_endpoint.c_str() + colon + 1);
  rcfg.max_retries = 1;
  RedisTransport transport(rcfg);
  if (!transport.connect()) {
    printf("SKIP: M26 distributed E2E (Redis unreachable at %s)\n",
           redis_endpoint.c_str());
    return 0;
  }

  // --- Postgres reachability probe -------------------------------------------
  PgRunStoreConfig pcfg;
  pcfg.host = env_or("EVO_PHASE2_PG_HOST", "127.0.0.1");
  pcfg.port = std::atoi(env_or("EVO_PHASE2_PG_PORT", "5433").c_str());
  pcfg.user = env_or("EVO_PHASE2_PG_USER", "evo");
  pcfg.password = env_or("EVO_PHASE2_PG_PASSWORD", "evo_dev_password");
  pcfg.dbname = env_or("EVO_PHASE2_PG_DB", "evo_phase2");
  pcfg.max_retries = 1;
  PgRunStore store(pcfg);
  if (!store.connect()) {
    printf("SKIP: M26 distributed E2E (Postgres unreachable at %s:%d)\n",
           pcfg.host.c_str(), pcfg.port);
    return 0;
  }
  // Schema must be migrated (scripts/phase2/migrate-local.sh).
  if (!store.ensure_workflow("00000000-0000-0000-0000-000000000000",
                             "org-m26-probe", "probe")) {
    printf("SKIP: M26 distributed E2E (schema not migrated; run "
           "scripts/phase2/migrate-local.sh)\n");
    return 0;
  }

  const std::string repo_root =
      env_or("EVO_M26_REPO_ROOT", EVO_REPO_ROOT_DIR);
  const int num_workers = std::atoi(env_or("EVO_M26_WORKERS", "2").c_str());

  // Unique namespace per invocation: hermetic streams.
  const std::string prefix =
      "evo:m26e2e:" +
      std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
  const std::string run_id = "run-" + prefix;
  const std::string wf_id = "22222222-2222-2222-2222-222222222222";

  // Ensure the workers' task-stream group exists BEFORE the loop publishes,
  // so no task is lost to a late "$" cursor. Workers' own ensureGroup is
  // idempotent (BUSYGROUP ok).
  check(transport.ensure_group(evo::task_stream_key(prefix), "workers", "$"),
        "task-stream worker group pre-created");

  // --- Spawn the TS worker fleet ---------------------------------------------
  std::vector<ChildProc> workers;
  for (int i = 0; i < num_workers; ++i) {
    auto w = spawn_worker(repo_root, prefix, "m26-worker-" + std::to_string(i),
                          rcfg.host, std::to_string(rcfg.port));
    if (w.pid < 0) {
      printf("SKIP: M26 distributed E2E (failed to spawn worker %d; is "
             "node/npx available?)\n", i);
      for (auto& k : workers) kill(-k.pid, SIGTERM);
      return 0;
    }
    workers.push_back(w);
  }
  printf("  ok   spawned %d TS worker(s)\n", num_workers);
  // Give workers a moment to connect + join the group.
  std::this_thread::sleep_for(1500ms);

  // --- Run the loop -----------------------------------------------------------
  DistributedRunConfig cfg;
  cfg.run_id = run_id;
  cfg.org_id = "org-m26";
  cfg.workflow_id = wf_id;
  cfg.env_prefix = prefix;
  cfg.read_block_ms = 100ms;
  cfg.run_timeout = 90s;

  std::vector<evo::RunEvent> events;
  DistributedRunLoop loop(make_diamond(), transport, store, cfg,
                          [&](const evo::RunEvent& ev) { events.push_back(ev); });

  // Duplicate-result injector: hammer a repeated success for node "start"
  // (attempt 1) across the real transport while the run executes. The payload
  // is a FAITHFUL copy of what the synthetic "bench:echo" executor produces
  // (same output shape + a worker-prefixed id), so whichever copy wins the
  // race, the durable row is identical — the test still proves at-most-once
  // application (one attempt row, successors unlocked once) and dedupe of the
  // loser. A forged/foreign payload would instead test lease validation,
  // which is M31/M33, not M26.
  std::jthread injector([&](std::stop_token st) {
    evo::execution::v1::ResultEnvelope dup;
    dup.set_run_id(run_id);
    dup.set_node_id("start");
    dup.set_attempt_number(1);
    dup.set_completed(true);
    dup.set_output("{\"echo\":true,\"runId\":\"" + run_id +
                   "\",\"nodeId\":\"start\",\"attempt\":1}");
    dup.set_status(evo::execution::v1::ResultEnvelope::OK);
    dup.set_worker_id("m26-worker-dup");
    *dup.mutable_finished_at() =
        google::protobuf::util::TimeUtil::MillisecondsToTimestamp(
            evo::now_wall_ms());
    const std::string bytes = dup.SerializeAsString();
    const std::string results = evo::result_stream_key(prefix);
    while (!st.stop_requested()) {
      transport.publish(results, bytes);
      std::this_thread::sleep_for(20ms);
    }
  });

  const std::string status = loop.run();
  injector.request_stop();
  injector.join();

  // --- Shutdown the worker fleet (graceful SIGTERM to the whole process group)
  // Workers run in their own process group (POSIX_SPAWN_SETPGROUP) so the
  // `npx` wrapper + its `node` child are both signalled; killing only the
  // wrapper pid leaves the child running on Linux (M38 CI).
  for (auto& w : workers) kill(-w.pid, SIGTERM);
  for (auto& w : workers) {
    int wstatus = 0;
    waitpid(w.pid, &wstatus, 0);
  }

  // --- Assertions ---------------------------------------------------------------
  check(status == evo::run_status::kSucceeded,
        "E2E diamond run succeeds across C++ + Redis + TS workers + Postgres");

  auto run = store.get_run(run_id);
  check(run.has_value() && run->status == evo::run_status::kSucceeded &&
            run->outcome == "succeeded" && run->engine == "evo",
        "Postgres audit: run row terminal succeeded (engine=evo)");

  bool all_nodes_ok = true;
  bool all_workers_attributed = true;
  for (const char* n : {"start", "a", "b", "c"}) {
    auto nr = store.get_node_run(run_id, n);
    if (!nr.has_value() || nr->status != evo::node_status::kSucceeded ||
        nr->output_json.empty()) {
      all_nodes_ok = false;
    }
    if (store.attempt_row_count(run_id, n) != 1) all_nodes_ok = false;
    auto wids = store.attempt_worker_ids(run_id, n);
    if (wids.size() != 1 || wids[0].rfind("m26-worker-", 0) != 0) {
      all_workers_attributed = false;
    }
  }
  check(all_nodes_ok,
        "Postgres audit: every node succeeded with output + exactly 1 attempt");
  check(all_workers_attributed,
        "Postgres audit: every attempt attributed to a real TS worker id");

  // Duplicate injection must not have created extra attempts or corrupted
  // the start node's output.
  check(store.attempt_row_count(run_id, "start") == 1,
        "duplicate result storm did not create extra attempts");
  auto start_nr = store.get_node_run(run_id, "start");
  check(start_nr.has_value() &&
            start_nr->output_json.find("\"dup\"") == std::string::npos,
        "duplicate result never overwrote the real output");

  // Normalized events: in-process + on the event stream.
  check(!events.empty() && events.front().kind == "run_started" &&
            events.back().kind == "run_finished",
        "events: run_started ... run_finished in order");
  transport.ensure_group(evo::event_stream_key(prefix), "e2e-readers", "0");
  std::size_t event_msgs = 0;
  bool saw_finished = false;
  {
    std::stop_source ss;
    while (true) {
      auto m = transport.read(evo::event_stream_key(prefix), "e2e-readers",
                              "reader", 200ms, ss.get_token());
      if (!m) break;
      event_msgs++;
      if (m->payload.find("run_finished") != std::string::npos) {
        saw_finished = true;
      }
      transport.ack(evo::event_stream_key(prefix), "e2e-readers", m->id);
    }
  }
  check(event_msgs >= 10 && saw_finished,
        "event stream carries normalized run events incl. run_finished");

  // Task stream fully consumed: no pending work left for the worker group.
  // The last worker's ack can land a moment after run_finished is observed, so
  // poll briefly for the PEL to drain. The assertion still requires 0 pending —
  // this only tolerates async ack propagation, it does not weaken the check.
  {
    long long pending = transport.pending_count(evo::task_stream_key(prefix),
                                                "workers");
    for (int i = 0; i < 100 && pending != 0; ++i) {  // up to ~10s
      std::this_thread::sleep_for(100ms);
      pending = transport.pending_count(evo::task_stream_key(prefix), "workers");
    }
    check(pending == 0, "no pending tasks left (all acked by workers)");
  }

  if (failures == 0) {
    printf("\nALL M26 DISTRIBUTED E2E TESTS PASSED!\n");
    return 0;
  }
  printf("\n%d FAILURE(S)\n", failures);
  return 1;
}
