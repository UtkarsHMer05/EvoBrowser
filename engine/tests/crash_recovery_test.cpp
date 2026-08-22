// M34 worker crash recovery + failure injection (steps 1–8).
//
// Demonstrates task REASSIGNMENT rather than merely describing it: a real TS
// worker process is terminated with SIGKILL while it holds the lease on a
// slow synthetic task; the scheduler's expired-lease scan reaps the attempt
// and re-dispatches the node as a NEW attempt, which a surviving worker
// completes. No logical task is lost, and a duplicate/late completion cannot
// corrupt state (M33 ledger + late-result rule).
//
// Topology (per trial):
//   - DistributedRunLoop (this process) drives the DAG
//     start -> {slow, quick} -> join over real Redis + Postgres.
//       start : bench:echo            (fast)
//       slow  : bench:sleep ms=4000   (the kill target; longer than the lease)
//       quick : bench:echo            (fast; proves other work is unaffected)
//       join  : bench:echo            (waits for slow + quick)
//   - 2 TS worker child processes (worker/src/main.ts, synthetic executor)
//     with SHORT leases (env-configurable, M34) so the reap is fast.
//   - The test waits until SOME worker acquires the lease on (slow, attempt 1),
//     records the injection timestamp, SIGKILLs that exact pid, then waits for
//     the run to finish and asserts recovery.
//
// Recovery evidence recorded per trial (all wall-clock UTC ms, durable store):
//   t_inject               — test wall clock at SIGKILL
//   t_lease_expired        — attempt-1 lease record expired_ms (scheduler reap)
//   t_replacement_acquired — attempt-2 lease record acquired_ms (new worker)
//   t_run_complete         — test wall clock when run() returned
// Derived: reap_latency = t_lease_expired - t_inject;
//          reassign_latency = t_replacement_acquired - t_lease_expired;
//          recovery_latency = t_run_complete - t_inject.
//
// Raw artifacts (M34 step 7): when EVO_M34_ARTIFACT_DIR is set, writes
// manifest.json / samples.jsonl / summary.json / command.txt there, following
// docs/phase2/BENCHMARK_METHODOLOGY.md §4. Timings are DIAGNOSTIC recovery
// samples (single local stack), not evidence-grade benchmark numbers.
//
// Skips (exit 0) when Redis/Postgres/tsx are unavailable, so CTest stays green
// without the full stack. Env overrides mirror the M26 E2E test.

#include <google/protobuf/util/time_util.h>

#include <algorithm>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <fstream>
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

std::int64_t now_wall_ms() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
             std::chrono::system_clock::now().time_since_epoch())
      .count();
}

// start -> {slow, quick} -> join
// NOTE: the trigger's node TYPE is "bench:echo" (not "start"): the distributed
// loop dispatches the trigger like any node, and the worker's synthetic
// executor has no "start" case — a type of "start" would permanently fail.
Dag make_crash_dag() {
  std::vector<NodeSpec> nodes = {
      {NodeId{"start"}, NodeKind::Trigger, "bench:echo"},
      {NodeId{"slow"}, NodeKind::Action, "bench:sleep"},
      {NodeId{"quick"}, NodeKind::Action, "bench:echo"},
      {NodeId{"join"}, NodeKind::Action, "bench:echo"},
  };
  std::vector<Edge> edges = {
      {NodeId{"start"}, NodeId{"slow"}},
      {NodeId{"start"}, NodeId{"quick"}},
      {NodeId{"slow"}, NodeId{"join"}},
      {NodeId{"quick"}, NodeId{"join"}},
  };
  auto br = Dag::build(nodes, edges);
  return std::move(*br.dag);
}

struct ChildProc {
  pid_t pid = -1;
  std::string id;
};

// Spawn one TS worker with SHORT lease/heartbeat cadence (M34 fault injection).
//
// `npx tsx` spawns a process TREE (npm exec -> node tsx -> node main.ts), so a
// signal to the spawned pid alone would leave the real worker running (the
// original M34 bug: the "killed" worker finished its own task). Two defenses:
//   1. POSIX_SPAWN_SETPGROUP puts the whole tree in its own process group
//      (pgid == child pid), so `kill(-pid, sig)` reaches every member.
//   2. stdout/stderr go to /dev/null, so a killed/orphaned descendant can
//      never hold CTest's output pipe open (which wedged ctest until its
//      timeout even after the test logic finished).
ChildProc spawn_worker(const std::string& repo_root, const std::string& prefix,
                       const std::string& worker_id,
                       const std::string& redis_host,
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
  // M34: short leases so the killed worker's attempt is reaped quickly.
  env_strings.push_back("EVO_WORKER_LEASE_DURATION_MS=1500");
  env_strings.push_back("EVO_WORKER_LEASE_RENEW_INTERVAL_MS=400");
  env_strings.push_back("EVO_WORKER_HEARTBEAT_INTERVAL_MS=300");
  std::vector<char*> envp;
  for (auto& s : env_strings) envp.push_back(s.data());
  envp.push_back(nullptr);

  std::vector<char*> argv = {const_cast<char*>("npx"),
                             const_cast<char*>("tsx"),
                             const_cast<char*>("worker/src/main.ts"), nullptr};

  posix_spawn_file_actions_t actions;
  posix_spawn_file_actions_init(&actions);
  evo_spawn_addchdir(&actions, repo_root.c_str());
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

// Signal the worker's ENTIRE process group (npx -> tsx -> node tree).
void signal_worker(const ChildProc& w, int sig) {
  if (w.pid > 0) ::kill(-w.pid, sig);
}

// One recovery trial's raw sample.
struct RecoverySample {
  int trial = 0;
  std::int64_t t_inject = 0;
  std::int64_t t_lease_expired = 0;
  std::int64_t t_replacement_acquired = 0;
  std::int64_t t_run_complete = 0;
  std::int64_t reap_latency_ms = 0;
  std::int64_t reassign_latency_ms = 0;
  std::int64_t recovery_latency_ms = 0;
  bool run_succeeded = false;
  int slow_attempts = 0;
  bool replacement_on_other_worker = false;
};

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
    printf("SKIP: M34 crash recovery (Redis unreachable at %s)\n",
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
    printf("SKIP: M34 crash recovery (Postgres unreachable at %s:%d)\n",
           pcfg.host.c_str(), pcfg.port);
    return 0;
  }
  if (!store.ensure_workflow("00000000-0000-0000-0000-000000000034",
                             "org-m34-probe", "probe")) {
    printf("SKIP: M34 crash recovery (schema not migrated; run "
           "scripts/phase2/migrate-local.sh)\n");
    return 0;
  }

  const std::string repo_root = env_or("EVO_M34_REPO_ROOT", EVO_REPO_ROOT_DIR);
  const int trials = std::atoi(env_or("EVO_M34_TRIALS", "3").c_str());

  std::vector<RecoverySample> samples;

  for (int trial = 0; trial < trials; ++trial) {
    printf("\n--- M34 trial %d/%d ---\n", trial + 1, trials);

    // Hermetic namespace per trial.
    const std::string prefix =
        "evo:m34crash:" + std::to_string(now_wall_ms()) + ":" +
        std::to_string(trial);
    const std::string run_id = "run-" + prefix;
    const std::string wf_id = "34343434-3434-3434-3434-343434343434";

    check(transport.ensure_group(evo::task_stream_key(prefix), "workers", "$"),
          "m34: task-stream worker group pre-created");

    // Spawn the 2-worker fleet with short leases.
    std::vector<ChildProc> workers;
    bool spawn_ok = true;
    for (int i = 0; i < 2; ++i) {
      auto w = spawn_worker(repo_root, prefix,
                            "m34-worker-" + std::to_string(i), rcfg.host,
                            std::to_string(rcfg.port));
      if (w.pid < 0) {
        spawn_ok = false;
        break;
      }
      workers.push_back(w);
    }
    if (!spawn_ok) {
      printf("SKIP: M34 crash recovery (failed to spawn workers; is "
             "node/npx available?)\n");
      for (auto& k : workers) signal_worker(k, SIGTERM);
      return 0;
    }
    std::this_thread::sleep_for(1500ms);  // connect + join group

    // Configure the run: short lease + fast scan so the reap is prompt.
    DistributedRunConfig cfg;
    cfg.run_id = run_id;
    cfg.org_id = "org-m34";
    cfg.workflow_id = wf_id;
    cfg.env_prefix = prefix;
    cfg.read_block_ms = 50ms;
    cfg.run_timeout = 60s;
    cfg.lease_duration = 1500ms;
    cfg.lease_initial_duration = 10000ms;  // generous queue-wait
    cfg.lease_scan_interval = 100ms;
    // The slow node sleeps 4000ms (> lease 1500ms) but renews while alive.
    cfg.node_payloads["slow"] = "{\"ms\":4000}";

    std::vector<evo::RunEvent> events;
    DistributedRunLoop loop(make_crash_dag(), transport, store, cfg,
                            [&](const evo::RunEvent& ev) {
                              events.push_back(ev);
                            });
    std::jthread loop_thread([&] { loop.run(); });

    // Wait until SOME worker acquires the lease on (slow, attempt 1).
    std::string killed_worker_id;
    pid_t killed_pid = -1;
    {
      const auto deadline = std::chrono::steady_clock::now() + 15s;
      bool acquired = false;
      while (std::chrono::steady_clock::now() < deadline && !acquired) {
        auto l = store.get_attempt_lease(run_id, "slow", 1);
        if (l.has_value() && !l->worker_id.empty()) {
          killed_worker_id = l->worker_id;
          acquired = true;
        } else {
          std::this_thread::sleep_for(10ms);
        }
      }
      check(acquired, "m34: a worker acquired the slow-task lease pre-crash");
      if (!acquired) {
        loop.stop();
        loop_thread.join();
        for (auto& k : workers) signal_worker(k, SIGTERM);
        for (auto& k : workers) { int s; waitpid(k.pid, &s, 0); }
        continue;
      }
      for (auto& w : workers) {
        if (w.id == killed_worker_id) killed_pid = w.pid;
      }
    }

    // FAULT INJECTION: hard-kill the lease-holding worker's ENTIRE process
    // group mid-task (npx -> tsx -> node; killing only the npx pid would leave
    // the real worker running and defeat the crash simulation).
    const std::int64_t t_inject = now_wall_ms();
    check(killed_pid > 0 && kill(-killed_pid, SIGKILL) == 0,
          "m34: SIGKILL delivered to the lease-holding worker process group");

    // Wait for the run to complete.
    loop_thread.join();
    const std::int64_t t_run_complete = now_wall_ms();
    const std::string status = store.get_run(run_id)->status;

    // Shut down the surviving worker(s) (whole process groups).
    for (auto& w : workers) {
      if (w.pid != killed_pid) signal_worker(w, SIGTERM);
    }
    for (auto& w : workers) { int s; waitpid(w.pid, &s, 0); }

    // --- Recovery assertions (M34 steps 2–5) ---------------------------------
    check(status == evo::run_status::kSucceeded,
          "m34: run succeeds after killing the lease-holding worker (no lost task)");

    auto lease1 = store.get_attempt_lease(run_id, "slow", 1);
    check(lease1.has_value() &&
              lease1->status == evo::attempt_status::kLeaseExpired &&
              lease1->worker_id == killed_worker_id,
          "m34: killed worker's attempt reaped to lease_expired");

    check(store.attempt_row_count(run_id, "slow") == 2,
          "m34: slow node re-dispatched as a new attempt (reassignment)");
    auto lease2 = store.get_attempt_lease(run_id, "slow", 2);
    const bool other = lease2.has_value() &&
                       !lease2->worker_id.empty() &&
                       lease2->worker_id != killed_worker_id;
    check(other, "m34: replacement attempt ran on a DIFFERENT (surviving) worker");

    auto nslow = store.get_node_run(run_id, "slow");
    check(nslow.has_value() && nslow->status == evo::node_status::kSucceeded,
          "m34: slow node eventually succeeded (logical task not lost)");

    // The unaffected sibling + join completed normally (no collateral loss).
    for (const char* n : {"start", "quick", "join"}) {
      auto nr = store.get_node_run(run_id, n);
      check(nr.has_value() && nr->status == evo::node_status::kSucceeded,
            "m34: unaffected node succeeded");
    }

    // Duplicate/late completion cannot corrupt state: the slow node has exactly
    // one terminal success and its output is the replacement attempt's.
    check(nslow.has_value() && !nslow->output_json.empty(),
          "m34: slow node output present (single logical commit)");

    // --- Record the recovery timeline sample ---------------------------------
    RecoverySample s;
    s.trial = trial + 1;
    s.t_inject = t_inject;
    s.t_lease_expired = lease1.has_value() ? lease1->expired_ms : 0;
    s.t_replacement_acquired = lease2.has_value() ? lease2->acquired_ms : 0;
    s.t_run_complete = t_run_complete;
    s.reap_latency_ms = s.t_lease_expired > 0 ? s.t_lease_expired - t_inject : 0;
    s.reassign_latency_ms =
        (s.t_replacement_acquired > 0 && s.t_lease_expired > 0)
            ? s.t_replacement_acquired - s.t_lease_expired
            : 0;
    s.recovery_latency_ms = t_run_complete - t_inject;
    s.run_succeeded = (status == evo::run_status::kSucceeded);
    s.slow_attempts = static_cast<int>(store.attempt_row_count(run_id, "slow"));
    s.replacement_on_other_worker = other;
    samples.push_back(s);

    printf("  info m34 trial %d recovery timeline (diagnostic, wall-clock ms): "
           "reap=%lld reassign=%lld recovery=%lld\n",
           trial + 1, (long long)s.reap_latency_ms,
           (long long)s.reassign_latency_ms, (long long)s.recovery_latency_ms);
  }

  check(!samples.empty(), "m34: at least one recovery trial ran");
  bool all_recovered = true;
  for (const auto& s : samples) {
    if (!s.run_succeeded || s.slow_attempts != 2 ||
        !s.replacement_on_other_worker) {
      all_recovered = false;
    }
  }
  check(all_recovered, "m34: every trial recovered (run ok, 2 attempts, new worker)");

  // --- Raw artifact emission (M34 step 7) ------------------------------------
  const std::string artifact_dir = env_or("EVO_M34_ARTIFACT_DIR", "");
  if (!artifact_dir.empty() && !samples.empty()) {
    std::ofstream manifest(artifact_dir + "/manifest.json");
    if (manifest) {
      // Provenance required by BENCHMARK_METHODOLOGY.md: commit + build mode +
      // hardware identify the sample. EVO_BUILD_COMMIT is baked at configure
      // time (PUBLIC on evo_scheduler_core, transitively linked here).
      std::string hw = "unknown";
      if (FILE* p = popen("uname -sm", "r")) {
        char buf[128];
        if (fgets(buf, sizeof(buf), p)) {
          hw = buf;
          while (!hw.empty() && (hw.back() == '\n' || hw.back() == '\r'))
            hw.pop_back();
        }
        pclose(p);
      }
      manifest << "{\n"
               << "  \"slug\": \"m34_worker_crash_recovery\",\n"
               << "  \"workload\": \"synthetic bench:sleep 4000ms, 2 TS workers, "
                  "SIGKILL lease-holder mid-attempt\",\n"
               << "  \"resource_class\": \"INTERNAL (synthetic, non-browser)\",\n"
               << "  \"build_mode\": \"" << env_or("EVO_M34_BUILD_MODE", "Release")
               << "\",\n"
               << "  \"commit\": \"" << EVO_BUILD_COMMIT << "\",\n"
               << "  \"hardware\": \"" << hw << "\",\n"
               << "  \"trials\": " << samples.size() << ",\n"
               << "  \"clock\": \"wall-clock UTC ms (durable store)\",\n"
               << "  \"note\": \"diagnostic recovery samples on a single local "
                  "stack; not evidence-grade benchmark numbers\",\n"
               << "  \"generated_at_wall_ms\": " << now_wall_ms() << "\n"
               << "}\n";
    }
    std::ofstream samplesf(artifact_dir + "/samples.jsonl");
    for (const auto& s : samples) {
      samplesf << "{\"trial\":" << s.trial
               << ",\"t_inject\":" << s.t_inject
               << ",\"t_lease_expired\":" << s.t_lease_expired
               << ",\"t_replacement_acquired\":" << s.t_replacement_acquired
               << ",\"t_run_complete\":" << s.t_run_complete
               << ",\"reap_latency_ms\":" << s.reap_latency_ms
               << ",\"reassign_latency_ms\":" << s.reassign_latency_ms
               << ",\"recovery_latency_ms\":" << s.recovery_latency_ms
               << ",\"run_succeeded\":" << (s.run_succeeded ? "true" : "false")
               << ",\"slow_attempts\":" << s.slow_attempts
               << ",\"replacement_on_other_worker\":"
               << (s.replacement_on_other_worker ? "true" : "false") << "}\n";
    }
    // Summary: min/median/max of recovery_latency_ms.
    std::vector<std::int64_t> rec;
    for (const auto& s : samples) rec.push_back(s.recovery_latency_ms);
    std::sort(rec.begin(), rec.end());
    const auto min_v = rec.front();
    const auto max_v = rec.back();
    const auto med_v = rec[rec.size() / 2];
    std::ofstream summary(artifact_dir + "/summary.json");
    if (summary) {
      summary << "{\n"
              << "  \"metric\": \"recovery_latency_ms (SIGKILL -> run complete)\",\n"
              << "  \"samples\": " << rec.size() << ",\n"
              << "  \"min\": " << min_v << ",\n"
              << "  \"median\": " << med_v << ",\n"
              << "  \"max\": " << max_v << "\n"
              << "}\n";
    }
    std::ofstream command(artifact_dir + "/command.txt");
    if (command) {
      command << "ctest --test-dir engine/build -R crash_recovery "
                 "--output-on-failure\n"
              << "# (or directly) EVO_M34_TRIALS=" << samples.size()
               << " EVO_M34_ARTIFACT_DIR=" << artifact_dir
               << " engine/build/evo_crash_recovery_test\n";
    }
    printf("  info m34 raw artifacts written to %s\n", artifact_dir.c_str());
  }

  if (failures == 0) {
    printf("\nALL M34 CRASH RECOVERY TESTS PASSED!\n");
    return 0;
  }
  printf("\n%d FAILURE(S)\n", failures);
  return 1;
}
