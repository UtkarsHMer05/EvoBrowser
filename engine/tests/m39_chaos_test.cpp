// Milestone 39 — infrastructure outage chaos campaign (Appendix T F09–F12).
//
// Injects a SHORT, controlled outage of the two durable dependencies while a
// distributed run is in flight, then verifies the run still reaches a terminal
// state and measures whether it recovered to success:
//   - Redis outage  : `docker pause evo-phase2-redis` for ~2.5s mid-run, then
//                     unpause. The RedisTransport's bounded reconnect backoff
//                     (base 50ms, cap 2s, max 5 attempts) must bridge the gap.
//   - Postgres outage: `docker pause evo-phase2-postgres` for ~2.0s mid-run,
//                     then unpause. The PgRunStore's lazy reconnect + bounded
//                     retry must bridge the gap.
//
// `docker pause` freezes the container's processes (cgroups freezer): existing
// TCP connections stay open but no response comes back — a clean simulation of a
// hung/unresponsive dependency, and it is instant + reversible (unpause), unlike
// stop/start. This is the "temporarily stop" fault (F09/F11); the "restart"
// variants (F10/F12) are covered by the transport/store's lazy reconnect, which
// this test exercises on unpause.
//
// Workload: wide DAG (start -> N independent bench:sleep), 2 TS workers, over
// real Redis + Postgres. The fault is injected only after several tasks are
// observably dispatched (polled from the durable store), so the outage always
// hits an in-flight run, not an idle one.
//
// Evidence-grade artifacts (BENCHMARK_METHODOLOGY.md §4) are written to
// EVO_M39_CHAOS_ARTIFACT_DIR when set. Recovery is recorded as a measured
// outcome per trial (methodology: preserve and explain worse-than-expected
// results rather than hiding them).
//
// Skips (exit 0) when Redis/Postgres/docker/the containers are unavailable so
// CTest stays green without the full stack.

#include <google/protobuf/util/time_util.h>

#include <algorithm>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <filesystem>
#include <fstream>
#include <spawn.h>
#include <string>
#include <sys/wait.h>
#include <thread>
#include <vector>

#include "evo/dag.hpp"
#include "evo/distributed_run_loop.hpp"
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

std::string hardware_string() {
  std::string hw = "unknown";
  if (FILE* p = popen("uname -sm", "r")) {
    char buf[256];
    if (fgets(buf, sizeof(buf), p)) {
      hw = buf;
      while (!hw.empty() && (hw.back() == '\n' || hw.back() == '\r'))
        hw.pop_back();
    }
    pclose(p);
  }
  return hw;
}

// Run a shell command, return its exit status (-1 on spawn failure).
int run_cmd(const std::string& cmd) {
  const int rc = std::system(cmd.c_str());
  if (rc == -1) return -1;
  return WIFEXITED(rc) ? WEXITSTATUS(rc) : -1;
}

bool docker_container_exists(const std::string& name) {
  return run_cmd("docker inspect " + name + " >/dev/null 2>&1") == 0;
}

Dag make_wide_dag(int n) {
  std::vector<NodeSpec> nodes = {
      {NodeId{"start"}, NodeKind::Trigger, "bench:echo"},
  };
  std::vector<Edge> edges;
  for (int i = 0; i < n; ++i) {
    const std::string id = "t" + std::to_string(i);
    nodes.push_back({NodeId{id}, NodeKind::Action, "bench:sleep"});
    edges.push_back({NodeId{"start"}, NodeId{id}});
  }
  auto br = Dag::build(nodes, edges);
  return std::move(*br.dag);
}

struct ChildProc {
  pid_t pid = -1;
  std::string id;
};

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

void signal_worker(const ChildProc& w, int sig) {
  if (w.pid > 0) ::kill(-w.pid, sig);
}

struct ChaosSample {
  std::string fault;            // "redis_outage" | "postgres_outage"
  int trial = 0;
  std::int64_t t_inject = 0;    // wall ms the pause began
  std::int64_t t_resume = 0;    // wall ms the unpause ran
  std::int64_t makespan_ms = 0;
  bool reached_terminal = false;
  bool recovered_to_success = false;
  int succeeded_nodes = 0;
  int tasks = 0;
};

}  // namespace

int main() {
  // --- Reachability + docker probes -----------------------------------------
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
    printf("SKIP: M39 chaos (Redis unreachable at %s)\n", redis_endpoint.c_str());
    return 0;
  }
  PgRunStoreConfig pcfg;
  pcfg.host = env_or("EVO_PHASE2_PG_HOST", "127.0.0.1");
  pcfg.port = std::atoi(env_or("EVO_PHASE2_PG_PORT", "5433").c_str());
  pcfg.user = env_or("EVO_PHASE2_PG_USER", "evo");
  pcfg.password = env_or("EVO_PHASE2_PG_PASSWORD", "evo_dev_password");
  pcfg.dbname = env_or("EVO_PHASE2_PG_DB", "evo_phase2");
  pcfg.max_retries = 1;
  PgRunStore store(pcfg);
  if (!store.connect()) {
    printf("SKIP: M39 chaos (Postgres unreachable at %s:%d)\n", pcfg.host.c_str(),
           pcfg.port);
    return 0;
  }
  if (!store.ensure_workflow("00000000-0000-0000-0000-000000000049",
                             "org-m39-chaos", "probe")) {
    printf("SKIP: M39 chaos (schema not migrated)\n");
    return 0;
  }
  if (run_cmd("docker info >/dev/null 2>&1") != 0) {
    printf("SKIP: M39 chaos (docker unavailable)\n");
    return 0;
  }
  const std::string redis_container = env_or("EVO_M39_REDIS_CONTAINER",
                                             "evo-phase2-redis");
  const std::string pg_container = env_or("EVO_M39_PG_CONTAINER",
                                          "evo-phase2-postgres");
  if (!docker_container_exists(redis_container) ||
      !docker_container_exists(pg_container)) {
    printf("SKIP: M39 chaos (containers %s / %s not present)\n",
           redis_container.c_str(), pg_container.c_str());
    return 0;
  }

  const std::string repo_root = env_or("EVO_M39_REPO_ROOT", EVO_REPO_ROOT_DIR);
  const int tasks = std::atoi(env_or("EVO_M39_CHAOS_TASKS", "30").c_str());
  const int task_ms = std::atoi(env_or("EVO_M39_CHAOS_TASK_MS", "40").c_str());
  const int redis_pause_ms = std::atoi(env_or("EVO_M39_REDIS_PAUSE_MS", "2500").c_str());
  const int pg_pause_ms = std::atoi(env_or("EVO_M39_PG_PAUSE_MS", "2000").c_str());

  struct FaultDef {
    std::string name;
    std::string container;
    int pause_ms;
  };
  const std::vector<FaultDef> faults = {
      {"redis_outage", redis_container, redis_pause_ms},
      {"postgres_outage", pg_container, pg_pause_ms},
  };

  std::vector<ChaosSample> samples;

  for (const auto& fault : faults) {
    printf("\n--- M39 chaos trial: %s (pause %dms) ---\n", fault.name.c_str(),
           fault.pause_ms);

    const std::string prefix =
        "evo:m39chaos:" + std::to_string(now_wall_ms()) + ":" + fault.name;
    const std::string run_id = "run-" + prefix;
    const std::string wf_id = "49494949-4949-4949-4949-494949494949";

    check(transport.ensure_group(evo::task_stream_key(prefix), "workers", "$"),
          "m39-chaos: task-stream worker group pre-created");

    std::vector<ChildProc> workers;
    bool spawn_ok = true;
    for (int i = 0; i < 2; ++i) {
      auto w = spawn_worker(repo_root, prefix,
                            "m39c-worker-" + std::to_string(i), rcfg.host,
                            std::to_string(rcfg.port));
      if (w.pid < 0) {
        spawn_ok = false;
        break;
      }
      workers.push_back(w);
    }
    if (!spawn_ok) {
      printf("SKIP: M39 chaos (failed to spawn workers)\n");
      for (auto& k : workers) signal_worker(k, SIGTERM);
      return 0;
    }
    std::this_thread::sleep_for(1500ms);

    DistributedRunConfig cfg;
    cfg.run_id = run_id;
    cfg.org_id = "org-m39-chaos";
    cfg.workflow_id = wf_id;
    cfg.env_prefix = prefix;
    cfg.read_block_ms = 50ms;
    cfg.run_timeout = 120s;
    cfg.lease_duration = 30000ms;
    cfg.lease_initial_duration = 60000ms;
    cfg.lease_scan_interval = 1000ms;
    for (int i = 0; i < tasks; ++i) {
      cfg.node_payloads["t" + std::to_string(i)] =
          "{\"ms\":" + std::to_string(task_ms) + "}";
    }

    auto dag = make_wide_dag(tasks);
    const std::int64_t t_start = now_wall_ms();
    DistributedRunLoop loop(std::move(dag), transport, store, cfg);
    std::string final_status;
    std::jthread loop_thread([&] { final_status = loop.run(); });

    // Wait until at least a few tasks are observably dispatched, so the outage
    // hits an in-flight run.
    {
      const auto deadline = std::chrono::steady_clock::now() + 20s;
      int dispatched = 0;
      while (std::chrono::steady_clock::now() < deadline && dispatched < 3) {
        dispatched = 0;
        for (int i = 0; i < tasks; ++i) {
          auto nr = store.get_node_run(run_id, "t" + std::to_string(i));
          if (nr.has_value() &&
              (nr->status == evo::node_status::kDispatched ||
               nr->status == evo::node_status::kRunning ||
               evo::node_status::is_terminal(nr->status))) {
            ++dispatched;
          }
        }
        std::this_thread::sleep_for(50ms);
      }
      check(dispatched >= 3, "m39-chaos: run in flight before fault injection");
    }

    // FAULT INJECTION: pause the dependency container.
    const std::int64_t t_inject = now_wall_ms();
    const int pause_rc = run_cmd("docker pause " + fault.container);
    check(pause_rc == 0, "m39-chaos: docker pause delivered");
    std::this_thread::sleep_for(std::chrono::milliseconds(fault.pause_ms));
    const std::int64_t t_resume = now_wall_ms();
    const int unpause_rc = run_cmd("docker unpause " + fault.container);
    check(unpause_rc == 0, "m39-chaos: docker unpause delivered");

    loop_thread.join();
    const std::int64_t t_end = now_wall_ms();

    for (auto& w : workers) signal_worker(w, SIGTERM);
    for (auto& w : workers) {
      int s;
      waitpid(w.pid, &s, 0);
    }

    // --- Audit ---------------------------------------------------------------
    int succeeded_nodes = 0;
    for (int i = 0; i < tasks; ++i) {
      auto nr = store.get_node_run(run_id, "t" + std::to_string(i));
      if (nr.has_value() && nr->status == evo::node_status::kSucceeded) {
        ++succeeded_nodes;
      }
    }
    const bool terminal =
        (final_status == evo::run_status::kSucceeded ||
         final_status == evo::run_status::kFailed ||
         final_status == evo::run_status::kCanceled);
    const bool recovered = (final_status == evo::run_status::kSucceeded);

    check(terminal, "m39-chaos: run reached a terminal state after the outage");
    if (recovered) {
      printf("  ok   m39-chaos: %s recovered to success (%d/%d tasks)\n",
             fault.name.c_str(), succeeded_nodes, tasks);
    } else {
      printf("  WARN m39-chaos: %s terminal=%s (%d/%d tasks succeeded) — "
             "recorded as evidence, not hidden\n",
             fault.name.c_str(), final_status.c_str(), succeeded_nodes, tasks);
    }

    ChaosSample s;
    s.fault = fault.name;
    s.trial = 0;
    s.t_inject = t_inject;
    s.t_resume = t_resume;
    s.makespan_ms = t_end - t_start;
    s.reached_terminal = terminal;
    s.recovered_to_success = recovered;
    s.succeeded_nodes = succeeded_nodes;
    s.tasks = tasks;
    samples.push_back(s);
  }

  check(!samples.empty(), "m39-chaos: at least one chaos trial ran");

  // --- Raw artifact emission (BENCHMARK_METHODOLOGY §4) ----------------------
  const std::string artifact_dir = env_or("EVO_M39_CHAOS_ARTIFACT_DIR", "");
  if (!artifact_dir.empty() && !samples.empty()) {
    std::filesystem::create_directories(artifact_dir);
    const std::string hw = hardware_string();
    std::ofstream manifest(artifact_dir + "/manifest.json");
    if (manifest) {
      manifest << "{\n"
               << "  \"slug\": \"m39_infrastructure_outage_chaos\",\n"
               << "  \"workload_class\": \"distributed synthetic I/O-bound "
                  "(bench:sleep), wide DAG, 2 TS workers\",\n"
               << "  \"faults\": [\"redis_outage (docker pause/unpause)\", "
                  "\"postgres_outage (docker pause/unpause)\"],\n"
               << "  \"tasks\": " << tasks << ",\n"
               << "  \"task_sleep_ms\": " << task_ms << ",\n"
               << "  \"redis_pause_ms\": " << redis_pause_ms << ",\n"
               << "  \"postgres_pause_ms\": " << pg_pause_ms << ",\n"
               << "  \"build_mode\": \""
               << env_or("EVO_M39_BUILD_MODE", "Release") << "\",\n"
               << "  \"commit\": \"" << EVO_BUILD_COMMIT << "\",\n"
               << "  \"hardware\": \"" << hw << "\",\n"
               << "  \"clock\": \"wall-clock UTC ms\",\n"
               << "  \"note\": \"recovery recorded as measured outcome per "
                  "trial; a non-recovery is preserved, not hidden "
                  "(methodology 15)\",\n"
               << "  \"generated_at_wall_ms\": " << now_wall_ms() << "\n"
               << "}\n";
    }
    std::ofstream samplesf(artifact_dir + "/samples.jsonl");
    for (const auto& s : samples) {
      samplesf << "{\"fault\":\"" << s.fault << "\",\"trial\":" << s.trial
               << ",\"t_inject\":" << s.t_inject << ",\"t_resume\":" << s.t_resume
               << ",\"makespan_ms\":" << s.makespan_ms
               << ",\"reached_terminal\":" << (s.reached_terminal ? "true" : "false")
               << ",\"recovered_to_success\":"
               << (s.recovered_to_success ? "true" : "false")
               << ",\"succeeded_nodes\":" << s.succeeded_nodes
               << ",\"tasks\":" << s.tasks << "}\n";
    }
    std::ofstream summary(artifact_dir + "/summary.json");
    if (summary) {
      summary << "{\n  \"trials\": [\n";
      bool first = true;
      for (const auto& s : samples) {
        if (!first) summary << ",\n";
        first = false;
        summary << "    {\"fault\":\"" << s.fault << "\",\"makespan_ms\":"
                << s.makespan_ms << ",\"reached_terminal\":"
                << (s.reached_terminal ? "true" : "false")
                << ",\"recovered_to_success\":"
                << (s.recovered_to_success ? "true" : "false")
                << ",\"succeeded_nodes\":" << s.succeeded_nodes
                << ",\"tasks\":" << s.tasks << "}";
      }
      summary << "\n  ]\n}\n";
    }
    std::ofstream command(artifact_dir + "/command.txt");
    if (command) {
      command << "ctest --test-dir engine/build -R m39_chaos --output-on-failure\n"
              << "# (or directly) EVO_M39_CHAOS_ARTIFACT_DIR=" << artifact_dir
              << " engine/build/evo_m39_chaos_test\n";
    }
    printf("  info m39 chaos artifacts written to %s\n", artifact_dir.c_str());
  }

  if (failures == 0) {
    printf("\nALL M39 CHAOS TESTS PASSED!\n");
    return 0;
  }
  printf("\n%d FAILURE(S)\n", failures);
  return 1;
}
