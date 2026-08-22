// M35 scheduler restart recovery + durable reconciliation (steps 1–8).
//
// Demonstrates that the orchestrator is RESTARTABLE without forgetting active
// runs: a REAL scheduler process (engine/app/run_loop_driver.cpp) is driven
// against real Redis + Postgres + 2 real TS workers, then SIGKILLed mid-run
// (a crash, not a graceful stop()). A second scheduler process is then started
// with resume=true; it reconstructs the run's logical state from the durable
// store, drains the dead scheduler's pending (unacked) result messages, and
// drives the run to a consistent terminal outcome. No logical task is lost and
// no node is double-completed.
//
// Topology (per trial, mirrors the M34 crash DAG):
//   start(bench:echo) -> { slow(bench:sleep 4000ms), quick(bench:echo) } -> join
//
// Per-trial sequence:
//   1. Spawn 2 TS workers (short lease cadence) + the scheduler driver (fresh).
//   2. Wait until a worker acquires the lease on (slow, attempt 1) — the run
//      is now in-flight with durable state.
//   3. SIGKILL the scheduler driver's process group (t_kill). Workers survive.
//   4. Assert the run is still NON-TERMINAL in Postgres (the crash left it
//      active; nothing was finalized by the dead scheduler).
//   5. Restart the driver with resume=1. It reconstructs + drains + resumes.
//   6. Assert the run reaches SUCCEEDED and every node has exactly ONE
//      terminal success with no duplicate attempts (no double-completion).
//   7. Restart-with-no-active-work: run the driver again (resume=1) against the
//      now-terminal run; it must report the durable outcome immediately without
//      re-dispatching any node (no resurrection).
//
// Skips (exit 0) when Redis/Postgres/tsx/the driver binary are unavailable, so
// CTest stays green without the full stack. Env overrides mirror the M34 test.

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
#include <unistd.h>
#include <vector>

#include "evo/dag.hpp"
#include "evo/pg_run_store.hpp"
#include "evo/redis_transport.hpp"
#include "evo/run_store.hpp"

#include "spawn_chdir.hpp"

extern char** environ;

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

struct ChildProc {
  pid_t pid = -1;
  std::string id;
};

// Spawn one TS worker with SHORT lease/heartbeat cadence (matches the M34
// fault-injection spawn; see crash_recovery_test.cpp for the process-group
// rationale). stdout/stderr -> /dev/null so a killed/orphaned descendant can
// never wedge CTest on pipe EOF.
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

// Spawn the scheduler driver binary (a single compiled process) in its own
// process group so it can be SIGKILLed cleanly. `resume` selects fresh vs
// resume mode. stdout/stderr -> /dev/null (the test asserts on Postgres state,
// which is authoritative, not on the driver's stdout).
ChildProc spawn_driver(const std::string& driver_bin, const std::string& run_id,
                       const std::string& prefix, bool resume,
                       const std::string& redis_host,
                       const std::string& redis_port,
                       const std::string& pg_host, const std::string& pg_port,
                       const std::string& pg_user, const std::string& pg_pass,
                       const std::string& pg_db,
                       const std::string& stdout_path = "/dev/null") {
  ChildProc out;
  out.id = "driver";

  std::vector<std::string> env_strings;
  for (char** e = environ; *e; ++e) env_strings.emplace_back(*e);
  env_strings.push_back("EVO_DRIVER_RUN_ID=" + run_id);
  env_strings.push_back("EVO_DRIVER_PREFIX=" + prefix);
  env_strings.push_back(std::string("EVO_DRIVER_RESUME=") + (resume ? "1" : "0"));
  env_strings.push_back("EVO_PHASE2_REDIS_HOST=" + redis_host);
  env_strings.push_back("EVO_PHASE2_REDIS_PORT=" + redis_port);
  env_strings.push_back("EVO_PHASE2_PG_HOST=" + pg_host);
  env_strings.push_back("EVO_PHASE2_PG_PORT=" + pg_port);
  env_strings.push_back("EVO_PHASE2_PG_USER=" + pg_user);
  env_strings.push_back("EVO_PHASE2_PG_PASSWORD=" + pg_pass);
  env_strings.push_back("EVO_PHASE2_PG_DB=" + pg_db);
  std::vector<char*> envp;
  for (auto& s : env_strings) envp.push_back(s.data());
  envp.push_back(nullptr);

  std::vector<char*> argv = {const_cast<char*>(driver_bin.c_str()), nullptr};

  posix_spawn_file_actions_t actions;
  posix_spawn_file_actions_init(&actions);
  posix_spawn_file_actions_addopen(&actions, STDOUT_FILENO, stdout_path.c_str(),
                                   O_WRONLY | O_CREAT | O_TRUNC, 0644);
  posix_spawn_file_actions_addopen(&actions, STDERR_FILENO, "/dev/null",
                                   O_WRONLY, 0);

  posix_spawnattr_t attr;
  posix_spawnattr_init(&attr);
  posix_spawnattr_setflags(&attr, POSIX_SPAWN_SETPGROUP);

  const int rc = posix_spawnp(&out.pid, driver_bin.c_str(), &actions, &attr,
                              argv.data(), envp.data());
  posix_spawn_file_actions_destroy(&actions);
  posix_spawnattr_destroy(&attr);
  if (rc != 0) out.pid = -1;
  return out;
}

void signal_proc(const ChildProc& p, int sig) {
  if (p.pid > 0) ::kill(-p.pid, sig);  // whole process group
}

// Wait for a process group to exit; returns the exit status (or -1 on error).
int wait_proc(const ChildProc& p) {
  if (p.pid <= 0) return -1;
  int status = 0;
  waitpid(p.pid, &status, 0);
  if (WIFEXITED(status)) return WEXITSTATUS(status);
  return -1;
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
    printf("SKIP: M35 scheduler restart (Redis unreachable at %s)\n",
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
    printf("SKIP: M35 scheduler restart (Postgres unreachable at %s:%d)\n",
           pcfg.host.c_str(), pcfg.port);
    return 0;
  }
  if (!store.ensure_workflow("00000000-0000-0000-0000-000000000035",
                             "org-m35-probe", "probe")) {
    printf("SKIP: M35 scheduler restart (schema not migrated; run "
           "scripts/phase2/migrate-local.sh)\n");
    return 0;
  }

  // --- Driver binary discovery ------------------------------------------------
  const std::string driver_bin = env_or("EVO_M35_DRIVER_BIN", "");
  if (driver_bin.empty()) {
    printf("SKIP: M35 scheduler restart (EVO_M35_DRIVER_BIN not set)\n");
    return 0;
  }
  if (access(driver_bin.c_str(), X_OK) != 0) {
    printf("SKIP: M35 scheduler restart (driver binary not executable: %s)\n",
           driver_bin.c_str());
    return 0;
  }

  const std::string repo_root = env_or("EVO_M35_REPO_ROOT", EVO_REPO_ROOT_DIR);
  const int trials = std::atoi(env_or("EVO_M35_TRIALS", "2").c_str());

  const std::string redis_host = rcfg.host;
  const std::string redis_port = std::to_string(rcfg.port);

  for (int trial = 0; trial < trials; ++trial) {
    printf("\n--- M35 trial %d/%d ---\n", trial + 1, trials);

    // Hermetic namespace per trial.
    const std::string prefix =
        "evo:m35restart:" + std::to_string(now_wall_ms()) + ":" +
        std::to_string(trial);
    const std::string run_id = "run-" + prefix;

    // Spawn the 2-worker fleet (short leases).
    std::vector<ChildProc> workers;
    bool spawn_ok = true;
    for (int i = 0; i < 2; ++i) {
      auto w = spawn_worker(repo_root, prefix,
                            "m35-worker-" + std::to_string(i), redis_host,
                            redis_port);
      if (w.pid < 0) {
        spawn_ok = false;
        break;
      }
      workers.push_back(w);
    }
    if (!spawn_ok) {
      printf("SKIP: M35 scheduler restart (failed to spawn workers; is "
             "node/npx available?)\n");
      for (auto& k : workers) signal_proc(k, SIGTERM);
      return 0;
    }
    std::this_thread::sleep_for(1500ms);  // connect + join group

    // --- Phase 1: fresh scheduler drives the run; kill it mid-flight --------
    auto driver = spawn_driver(driver_bin, run_id, prefix, /*resume=*/false,
                               redis_host, redis_port, pcfg.host,
                               std::to_string(pcfg.port), pcfg.user,
                               pcfg.password, pcfg.dbname);
    check(driver.pid > 0, "m35: fresh scheduler driver spawned");
    if (driver.pid <= 0) {
      for (auto& k : workers) signal_proc(k, SIGTERM);
      for (auto& k : workers) { int s; waitpid(k.pid, &s, 0); }
      continue;
    }

    // Wait until a worker acquires the lease on (slow, attempt 1): the run is
    // now in-flight with durable state (start dispatched, slow claimed).
    {
      const auto deadline = std::chrono::steady_clock::now() + 20s;
      bool acquired = false;
      while (std::chrono::steady_clock::now() < deadline && !acquired) {
        auto l = store.get_attempt_lease(run_id, "slow", 1);
        acquired = l.has_value() && !l->worker_id.empty();
        if (!acquired) std::this_thread::sleep_for(10ms);
      }
      check(acquired, "m35: run in-flight (slow lease acquired) pre-crash");
      if (!acquired) {
        signal_proc(driver, SIGKILL);
        wait_proc(driver);
        for (auto& k : workers) signal_proc(k, SIGTERM);
        for (auto& k : workers) { int s; waitpid(k.pid, &s, 0); }
        continue;
      }
    }

    // FAULT INJECTION: hard-kill the scheduler (whole process group). Workers
    // survive and keep renewing their leases.
    signal_proc(driver, SIGKILL);
    wait_proc(driver);
    check(true, "m35: scheduler SIGKILLed mid-run (crash injected)");

    // Diagnostic: the result-stream state at crash time. A result published by
    // a worker before the crash is either (a) already delivered to the dead
    // scheduler's consumer (pending/unacked -> drained on resume) or (b) still
    // in the stream ahead of the group cursor (delivered by the resumed main
    // loop). Either way it must be recovered.
    const std::string result_stream = evo::result_stream_key(prefix);
    printf("  info m35 crash-time result stream: len=%zu pending(scheduler)=%zu\n",
           transport.stream_length(result_stream),
           transport.pending_count(result_stream, "scheduler"));

    // The crash left the run ACTIVE: the dead scheduler finalized nothing.
    auto run_after_kill = store.get_run(run_id);
    check(run_after_kill.has_value() &&
              run_after_kill->status == evo::run_status::kRunning,
          "m35: run still non-terminal immediately after the scheduler crash");

    // --- Phase 2: restart the scheduler with resume=true ---------------------
    const std::string restart_log =
        "/tmp/m35_restart_driver_" + std::to_string(now_wall_ms()) + ".log";
    auto restarted = spawn_driver(driver_bin, run_id, prefix, /*resume=*/true,
                                  redis_host, redis_port, pcfg.host,
                                  std::to_string(pcfg.port), pcfg.user,
                                  pcfg.password, pcfg.dbname, restart_log);
    check(restarted.pid > 0, "m35: restarted scheduler driver spawned");
    int restart_exit = -1;
    if (restarted.pid > 0) {
      restart_exit = wait_proc(restarted);  // blocks until the run is terminal
    }
    if (restart_exit != 0) {
      printf("  info m35 restarted driver log (%s):\n", restart_log.c_str());
      std::ifstream lf(restart_log);
      std::string line;
      while (std::getline(lf, line)) printf("    | %s\n", line.c_str());
    }
    check(restart_exit == 0,
          "m35: restarted scheduler drove the run to SUCCEEDED");

    // --- Recovery assertions (M35 steps 2–6) ---------------------------------
    auto run_final = store.get_run(run_id);
    check(run_final.has_value() &&
              run_final->status == evo::run_status::kSucceeded,
          "m35: run reached a consistent terminal success across the restart");

    // No duplicate logical completion: every node has exactly ONE terminal
    // success and no node was re-dispatched (the workers stayed alive and kept
    // renewing, so no lease expired and no replacement attempt was needed).
    bool all_once = true;
    for (const char* n : {"start", "slow", "quick", "join"}) {
      auto nr = store.get_node_run(run_id, n);
      const bool ok = nr.has_value() &&
                      nr->status == evo::node_status::kSucceeded &&
                      store.attempt_row_count(run_id, n) == 1;
      if (!ok) all_once = false;
      check(ok, "m35: node succeeded exactly once (no double-completion)");
    }
    check(all_once, "m35: no node double-completed across the restart");

    // --- Phase 3: restart with NO active work (run already terminal) --------
    // A restarted scheduler against a terminal run must report the durable
    // outcome immediately without re-dispatching any node (no resurrection).
    auto noop = spawn_driver(driver_bin, run_id, prefix, /*resume=*/true,
                             redis_host, redis_port, pcfg.host,
                             std::to_string(pcfg.port), pcfg.user,
                             pcfg.password, pcfg.dbname);
    int noop_exit = -1;
    if (noop.pid > 0) noop_exit = wait_proc(noop);
    check(noop_exit == 0,
          "m35: restart against a terminal run exits cleanly (no resurrection)");
    bool no_new_attempts = true;
    for (const char* n : {"start", "slow", "quick", "join"}) {
      if (store.attempt_row_count(run_id, n) != 1) no_new_attempts = false;
    }
    check(no_new_attempts,
          "m35: no node re-dispatched for a terminal run on restart");

    // Shut down the worker fleet (whole process groups).
    for (auto& w : workers) signal_proc(w, SIGTERM);
    for (auto& w : workers) { int s; waitpid(w.pid, &s, 0); }
  }

  check(failures == 0, "m35: every restart-recovery trial passed");

  if (failures == 0) {
    printf("\nALL M35 SCHEDULER RESTART RECOVERY TESTS PASSED!\n");
    return 0;
  }
  printf("\n%d FAILURE(S)\n", failures);
  return 1;
}
