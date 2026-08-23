// Milestone 39 — distributed worker scaling campaign (Appendix S).
//
// Measures how end-to-end distributed throughput scales with the number of TS
// worker processes for a fixed synthetic workload. For each (worker_count,
// logical_task_count) cell it:
//   1. spawns `worker_count` real TS workers (worker/src/main.ts) against the
//      local Phase-2 Redis,
//   2. drives a WIDE DAG (start -> N independent bench:sleep actions) through
//      the DistributedRunLoop over real Redis + Postgres,
//   3. measures makespan (wall clock) and derives throughput (logical tasks/s),
//   4. audits the durable store: every node succeeded, exactly one attempt per
//      node (no lost/duplicated work), and counts retries/errors.
//
// Workload class: simulated I/O-bound (bench:sleep), INTERNAL resource class
// (unbounded capacity), so the wide DAG parallelizes fully across workers. This
// is the distributed analog of the local campaign's "wide/io" cell — it
// exercises transport + lease + result plumbing, not just in-process scheduling.
//
// Evidence-grade artifacts (BENCHMARK_METHODOLOGY.md §4) are written to
// EVO_M39_ARTIFACT_DIR when set: manifest.json / samples.jsonl / summary.json /
// command.txt. Scaling is computed relative to the 1-worker case only when all
// other conditions are identical (same task profile, DAG, machine, build).
//
// Skips (exit 0) when Redis/Postgres/tsx are unavailable so CTest stays green
// without the full stack. Env overrides mirror the M26/M34 tests.
//
// NOTE: per-node ready-to-dispatch / queue-latency percentiles are captured by
// the LOCAL scheduler campaign (evo-m39-local), which has full per-node
// steady_clock timestamps. This distributed campaign reports makespan +
// throughput + retries + errors, which are the evidence-grade scaling signals
// available from the durable store.

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

// Wide DAG: trigger "start" (bench:echo) -> N independent bench:sleep actions.
// The trigger's node TYPE is "bench:echo" (not "start"): the distributed loop
// dispatches the trigger like any node and the synthetic executor has no
// "start" case.
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

// Spawn one TS worker in its own process group with stdout/stderr -> /dev/null
// (same pattern as M34/M26 so shutdown kills the whole npx->tsx->node tree and
// a killed descendant can never wedge CTest's output pipe).
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

// One scaling cell's raw sample.
struct ScalingSample {
  int workers = 0;
  int tasks = 0;
  int trial = 0;
  std::int64_t makespan_ms = 0;
  double throughput_nps = 0;   // logical tasks / second
  int succeeded_nodes = 0;
  int total_attempts = 0;
  int retries = 0;             // total_attempts - tasks (extra attempts)
  bool run_succeeded = false;
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
    printf("SKIP: M39 scaling (Redis unreachable at %s)\n",
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
    printf("SKIP: M39 scaling (Postgres unreachable at %s:%d)\n",
           pcfg.host.c_str(), pcfg.port);
    return 0;
  }
  if (!store.ensure_workflow("00000000-0000-0000-0000-000000000039",
                             "org-m39-probe", "probe")) {
    printf("SKIP: M39 scaling (schema not migrated; run "
           "scripts/phase2/migrate-local.sh)\n");
    return 0;
  }

  const std::string repo_root = env_or("EVO_M39_REPO_ROOT", EVO_REPO_ROOT_DIR);
  const int task_ms = std::atoi(env_or("EVO_M39_TASK_MS", "20").c_str());
  // Worker/task counts are env-configurable so the campaign can smoke-test
  // cheaply; the evidence-grade campaign uses the Appendix S defaults.
  auto parse_ints = [](const std::string& s, std::vector<int> fallback) {
    std::vector<int> out;
    std::string cur;
    for (char c : s) {
      if (c == ',' || c == ' ') {
        if (!cur.empty()) out.push_back(std::atoi(cur.c_str()));
        cur.clear();
      } else {
        cur.push_back(c);
      }
    }
    if (!cur.empty()) out.push_back(std::atoi(cur.c_str()));
    return out.empty() ? fallback : out;
  };
  const std::vector<int> worker_counts =
      parse_ints(env_or("EVO_M39_WORKERS", ""), {1, 2, 4});
  const std::vector<int> task_counts =
      parse_ints(env_or("EVO_M39_TASKS", ""), {100, 500});
  // Repeated trials per cell (methodology §1.5: store raw samples, not only
  // aggregates). The worker fleet is spawned ONCE per cell and reused across
  // trials so every trial sees identical worker conditions.
  const int trials = std::atoi(env_or("EVO_M39_TRIALS", "2").c_str());

  std::vector<ScalingSample> samples;

  for (int tasks : task_counts) {
    for (int nw : worker_counts) {
      printf("\n--- M39 scaling cell: workers=%d tasks=%d (%d trials) ---\n",
             nw, tasks, trials);

      // Spawn the worker fleet ONCE for this cell (reused across trials).
      const std::string fleet_prefix =
          "evo:m39scale:" + std::to_string(now_wall_ms()) + ":w" +
          std::to_string(nw) + "t" + std::to_string(tasks);
      std::vector<ChildProc> workers;
      bool spawn_ok = true;
      for (int i = 0; i < nw; ++i) {
        auto w = spawn_worker(repo_root, fleet_prefix,
                              "m39-worker-" + std::to_string(i), rcfg.host,
                              std::to_string(rcfg.port));
        if (w.pid < 0) {
          spawn_ok = false;
          break;
        }
        workers.push_back(w);
      }
      if (!spawn_ok) {
        printf("SKIP: M39 scaling (failed to spawn workers; is node/npx "
               "available?)\n");
        for (auto& k : workers) signal_worker(k, SIGTERM);
        return 0;
      }
      std::this_thread::sleep_for(1500ms);  // connect + join group

      for (int trial = 0; trial < trials; ++trial) {
        // All trials in a cell share the fleet's env_prefix (the workers are
        // bound to it at spawn), so every trial sees identical worker/stream
        // conditions. Only the run_id differs per trial (fresh durable run).
        const std::string prefix = fleet_prefix;
        const std::string run_id = "run-" + prefix + ":r" + std::to_string(trial);
        const std::string wf_id = "39393939-3939-3939-3939-393939393939";

        check(transport.ensure_group(evo::task_stream_key(prefix), "workers", "$"),
              "m39: task-stream worker group pre-created");

        // Configure the run: every action node sleeps task_ms.
        DistributedRunConfig cfg;
        cfg.run_id = run_id;
        cfg.org_id = "org-m39";
        cfg.workflow_id = wf_id;
        cfg.env_prefix = prefix;
        cfg.read_block_ms = 50ms;
        cfg.run_timeout = 120s;
        cfg.lease_duration = 30000ms;
        cfg.lease_initial_duration = 60000ms;  // generous queue-wait
        cfg.lease_scan_interval = 1000ms;
        for (int i = 0; i < tasks; ++i) {
          cfg.node_payloads["t" + std::to_string(i)] =
              "{\"ms\":" + std::to_string(task_ms) + "}";
        }

        auto dag = make_wide_dag(tasks);
        const std::int64_t t_start = now_wall_ms();
        DistributedRunLoop loop(std::move(dag), transport, store, cfg);
        const std::string status = loop.run();
        const std::int64_t t_end = now_wall_ms();

        // --- Audit the durable store -----------------------------------------
        const bool run_ok = (status == evo::run_status::kSucceeded);
        int succeeded_nodes = 0;
        int total_attempts = 0;
        for (int i = 0; i < tasks; ++i) {
          const std::string id = "t" + std::to_string(i);
          auto nr = store.get_node_run(run_id, id);
          if (nr.has_value() && nr->status == evo::node_status::kSucceeded) {
            ++succeeded_nodes;
          }
          total_attempts += static_cast<int>(store.attempt_row_count(run_id, id));
        }

        check(run_ok, "m39: wide run succeeded across workers");
        check(succeeded_nodes == tasks,
              "m39: every logical task succeeded (no lost work)");
        check(total_attempts == tasks,
              "m39: exactly one attempt per task (no duplicates/retries)");

        ScalingSample s;
        s.workers = nw;
        s.tasks = tasks;
        s.trial = trial;
        s.makespan_ms = t_end - t_start;
        s.throughput_nps = s.makespan_ms > 0
                               ? (1000.0 * static_cast<double>(tasks)) /
                                     static_cast<double>(s.makespan_ms)
                               : 0.0;
        s.succeeded_nodes = succeeded_nodes;
        s.total_attempts = total_attempts;
        s.retries = total_attempts - tasks;
        s.run_succeeded = run_ok;
        samples.push_back(s);

        const auto ts = loop.timing_stats();
        printf("  info m39 workers=%d tasks=%d trial=%d makespan=%lldms "
               "throughput=%.2f/s\n",
               nw, tasks, trial, (long long)s.makespan_ms, s.throughput_nps);
        printf("  info   loop timings: batches=%llu results=%llu "
               "dispatch_calls=%llu dispatch_ms=%lld consume_ms=%lld "
               "apply_ms=%lld\n",
               (unsigned long long)ts.batches_read,
               (unsigned long long)ts.results_consumed,
               (unsigned long long)ts.dispatch_calls,
               (long long)ts.dispatch_ms, (long long)ts.consume_ms,
               (long long)ts.apply_ms);
      }

      // Shut down the worker fleet (whole process groups) after all trials.
      for (auto& w : workers) signal_worker(w, SIGTERM);
      for (auto& w : workers) {
        int s;
        waitpid(w.pid, &s, 0);
      }
    }
  }

  check(!samples.empty(), "m39: at least one scaling cell ran");

  // --- Raw artifact emission (BENCHMARK_METHODOLOGY §4) ----------------------
  const std::string artifact_dir = env_or("EVO_M39_ARTIFACT_DIR", "");
  if (!artifact_dir.empty() && !samples.empty()) {
    std::filesystem::create_directories(artifact_dir);
    const std::string hw = hardware_string();
    std::ofstream manifest(artifact_dir + "/manifest.json");
    if (manifest) {
      manifest << "{\n"
               << "  \"slug\": \"m39_distributed_worker_scaling\",\n"
               << "  \"workload_class\": \"distributed synthetic I/O-bound "
                  "(bench:sleep), INTERNAL resource class\",\n"
               << "  \"dag_shape\": \"wide (start -> N independent tasks)\",\n"
               << "  \"task_sleep_ms\": " << task_ms << ",\n"
               << "  \"worker_counts\": [";
      for (size_t i = 0; i < worker_counts.size(); ++i) {
        manifest << (i ? ", " : "") << worker_counts[i];
      }
      manifest << "],\n"
               << "  \"task_counts\": [";
      for (size_t i = 0; i < task_counts.size(); ++i) {
        manifest << (i ? ", " : "") << task_counts[i];
      }
      manifest << "],\n"
               << "  \"build_mode\": \""
               << env_or("EVO_M39_BUILD_MODE", "Release") << "\",\n"
               << "  \"result_batch_size\": "
               << DistributedRunConfig{}.result_batch_size << ",\n"
               << "  \"commit\": \"" << EVO_BUILD_COMMIT << "\",\n"
               << "  \"hardware\": \"" << hw << "\",\n"
               << "  \"clock\": \"wall-clock UTC ms (makespan)\",\n"
               << "  \"note\": \"scaling relative to 1-worker case only; "
                  "per-node latency percentiles are in the local campaign "
                  "(evo-m39-local)\",\n"
               << "  \"generated_at_wall_ms\": " << now_wall_ms() << "\n"
               << "}\n";
    }
    std::ofstream samplesf(artifact_dir + "/samples.jsonl");
    for (const auto& s : samples) {
      samplesf << "{\"workers\":" << s.workers << ",\"tasks\":" << s.tasks
               << ",\"trial\":" << s.trial
               << ",\"makespan_ms\":" << s.makespan_ms
               << ",\"throughput_nps\":" << s.throughput_nps
               << ",\"succeeded_nodes\":" << s.succeeded_nodes
               << ",\"total_attempts\":" << s.total_attempts
               << ",\"retries\":" << s.retries
               << ",\"run_succeeded\":" << (s.run_succeeded ? "true" : "false")
               << "}\n";
    }
    // Summary: per (workers, tasks) cell, MEDIAN throughput + makespan across
    // trials, and scaling vs the 1-worker cell's median (identical conditions).
    auto median_of = [](std::vector<double> v) {
      if (v.empty()) return 0.0;
      std::sort(v.begin(), v.end());
      return v[v.size() / 2];
    };
    std::ofstream summary(artifact_dir + "/summary.json");
    if (summary) {
      summary << "{\n  \"cells\": [\n";
      bool first = true;
      for (int tasks : task_counts) {
        // 1-worker baseline median for this task count.
        std::vector<double> base_tputs;
        for (const auto& s : samples) {
          if (s.tasks == tasks && s.workers == 1) {
            base_tputs.push_back(s.throughput_nps);
          }
        }
        const double base_tput = median_of(base_tputs);
        for (int nw : worker_counts) {
          std::vector<double> tputs, spans;
          int retries = 0;
          for (const auto& s : samples) {
            if (s.tasks == tasks && s.workers == nw) {
              tputs.push_back(s.throughput_nps);
              spans.push_back(static_cast<double>(s.makespan_ms));
              retries += s.retries;
            }
          }
          if (tputs.empty()) continue;
          const double med_tput = median_of(tputs);
          const double med_span = median_of(spans);
          const double scaling = base_tput > 0 ? med_tput / base_tput : 0.0;
          const double efficiency =
              nw > 0 ? scaling / static_cast<double>(nw) : 0.0;
          if (!first) summary << ",\n";
          first = false;
          summary << "    {\"workers\":" << nw << ",\"tasks\":" << tasks
                  << ",\"trials\":" << tputs.size()
                  << ",\"median_makespan_ms\":" << med_span
                  << ",\"median_throughput_nps\":" << med_tput
                  << ",\"scaling_vs_1w\":" << scaling
                  << ",\"parallel_efficiency\":" << efficiency
                  << ",\"retries\":" << retries << "}";
        }
      }
      summary << "\n  ]\n}\n";
    }
    std::ofstream command(artifact_dir + "/command.txt");
    if (command) {
      command << "ctest --test-dir engine/build -R m39_scaling "
                 "--output-on-failure\n"
              << "# (or directly) EVO_M39_ARTIFACT_DIR=" << artifact_dir
              << " engine/build/evo_m39_scaling_test\n";
    }
    printf("  info m39 scaling artifacts written to %s\n", artifact_dir.c_str());
  }

  if (failures == 0) {
    printf("\nALL M39 SCALING TESTS PASSED!\n");
    return 0;
  }
  printf("\n%d FAILURE(S)\n", failures);
  return 1;
}
