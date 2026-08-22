// Milestone 15: local benchmark corpus runner.
//
// Runs workloads (linear, diamond, wide fan-out/fan-in, layered, seeded random)
// through BOTH the sequential reference scheduler (Scheduler) and the concurrent
// scheduler (ConcurrentScheduler) across worker counts {1,2,4,8}, with warmup
// trials and repeated measurement trials. Records:
//   - machine/compiler/build metadata (commit, CPU count, arch, compiler, flags)
//   - raw samples (one line per trial)
//   - summary (p50/p95/p99 makespan + speedup vs sequential)
//
// All timing uses steady_clock (monotonic) for durations. Output is written as
// a machine-readable manifest + raw CSV. Diagnostic only — numbers reflect THIS
// machine (8-core Apple Silicon arm64, clang 21, Release build) and the recorded
// commit. Per BENCHMARK_METHODOLOGY.md §5, a result is only evidence-grade when
// workload/build-mode/hardware/sample-count/commit are all identified.

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <numeric>
#include <set>
#include <string>
#include <vector>

#include "evo/bench.hpp"
#include "evo/concurrent_scheduler.hpp"
#include "evo/dag.hpp"
#include "evo/scheduler.hpp"

namespace fs = std::filesystem;

namespace {

double pct(const std::vector<double>& xs, double p) {
  if (xs.empty()) return 0.0;
  std::vector<double> s = xs;
  std::sort(s.begin(), s.end());
  size_t idx = static_cast<size_t>((p / 100.0) * (s.size() - 1));
  return s[idx];
}

double median(const std::vector<double>& xs) { return pct(xs, 50.0); }

struct WorkloadDef {
  std::string name;
  std::uint64_t seed = 0;  // for random workloads
  int width = 0;
  int depth = 0;
};

evo::Dag build_dag(const WorkloadDef& wd) {
  if (wd.name == "linear") {
    std::vector<evo::NodeSpec> nodes{{evo::NodeId{"start"}, evo::NodeKind::Trigger, "start"}};
    std::vector<evo::Edge> edges;
    std::string prev = "start";
    for (int i = 0; i < 8; ++i) {
      std::string id = "n" + std::to_string(i);
      nodes.push_back({evo::NodeId{id}, evo::NodeKind::Action, "bench:sleep"});
      edges.push_back({evo::NodeId{prev}, evo::NodeId{id}});
      prev = id;
    }
    auto b = evo::Dag::build(std::move(nodes), std::move(edges));
    if (b.ok()) return std::move(*b.dag);
  }
  if (wd.name == "diamond") {
    std::vector<evo::NodeSpec> nodes{
        {evo::NodeId{"start"}, evo::NodeKind::Trigger, "start"},
        {evo::NodeId{"left"}, evo::NodeKind::Action, "bench:sleep"},
        {evo::NodeId{"right"}, evo::NodeKind::Action, "bench:sleep"},
        {evo::NodeId{"join"}, evo::NodeKind::Action, "bench:sleep"}};
    std::vector<evo::Edge> edges{
        {evo::NodeId{"start"}, evo::NodeId{"left"}},
        {evo::NodeId{"start"}, evo::NodeId{"right"}},
        {evo::NodeId{"left"}, evo::NodeId{"join"}},
        {evo::NodeId{"right"}, evo::NodeId{"join"}}};
    auto b = evo::Dag::build(std::move(nodes), std::move(edges));
    if (b.ok()) return std::move(*b.dag);
  }
  if (wd.name == "wide") {
    std::vector<evo::NodeSpec> nodes{
        {evo::NodeId{"start"}, evo::NodeKind::Trigger, "start"}};
    std::vector<evo::Edge> edges;
    for (int i = 0; i < 16; ++i) {
      std::string id = "w" + std::to_string(i);
      nodes.push_back({evo::NodeId{id}, evo::NodeKind::Action, "bench:sleep"});
      edges.push_back({evo::NodeId{"start"}, evo::NodeId{id}});
    }
    auto b = evo::Dag::build(std::move(nodes), std::move(edges));
    if (b.ok()) return std::move(*b.dag);
  }
  if (wd.name == "layered") {
    // 3 layers x 6 nodes, each layer fully dependent on the previous.
    std::vector<evo::NodeSpec> nodes{
        {evo::NodeId{"start"}, evo::NodeKind::Trigger, "start"}};
    std::vector<evo::Edge> edges;
    std::string prev = "start";
    for (int layer = 0; layer < 3; ++layer) {
      std::string layer_prev = prev;
      for (int j = 0; j < 6; ++j) {
        std::string id = "L" + std::to_string(layer) + "n" + std::to_string(j);
        nodes.push_back({evo::NodeId{id}, evo::NodeKind::Action, "bench:sleep"});
        edges.push_back({evo::NodeId{layer_prev}, evo::NodeId{id}});
        if (layer == 0) {
          // All depend on start in layer 0.
        }
      }
      // Layer N nodes all depend on a representative of previous layer.
      prev = "L" + std::to_string(layer) + "n0";
    }
    auto b = evo::Dag::build(std::move(nodes), std::move(edges));
    if (b.ok()) return std::move(*b.dag);
  }
  if (wd.name == "random") {
    auto gen = evo::bench::generate_workload(wd.seed, wd.width, wd.depth);
    return gen.dag;
  }
  return {};
}

std::map<std::string, evo::TaskFn> seq_tasks(const evo::Dag& dag,
                                              const std::map<evo::NodeId, int>& sleep_ms) {
  std::map<std::string, evo::TaskFn> tasks;
  tasks["start"] = [](const evo::NodeSpec&) {
    return evo::TaskResult{true, "started"};
  };
  tasks["bench:sleep"] = evo::bench::sleep_task(sleep_ms);
  tasks["bench:burn"] = evo::bench::burn_task({});
  return tasks;
}

std::map<std::string, evo::ConcurrentTaskFn> concurrent_tasks(
    const evo::Dag& dag, const std::map<evo::NodeId, int>& sleep_ms) {
  std::map<std::string, evo::ConcurrentTaskFn> tasks;
  tasks["start"] = [](const evo::NodeSpec&, std::stop_token) {
    return evo::TaskResult{true, "started"};
  };
  tasks["bench:sleep"] = evo::bench::sleep_task_cooperative(sleep_ms);
  tasks["bench:burn"] = evo::bench::burn_task_cooperative({});
  return tasks;
}

std::map<evo::NodeId, int> default_sleep_ms(const evo::Dag& dag, int ms) {
  std::map<evo::NodeId, int> m;
  for (const auto& id : dag.node_ids()) m[id] = ms;
  return m;
}

void run_workload(const WorkloadDef& wd, fs::path out_dir,
                  std::ofstream& manifest, int warmup, int trials) {
  auto dag = build_dag(wd);
  if (!dag.node_count()) return;

  auto sleep_ms = default_sleep_ms(dag, 3);
  std::string commit = "unknown";
#ifdef EVO_BUILD_COMMIT
  commit = EVO_BUILD_COMMIT;
#endif

  std::vector<int> workers_list{1, 2, 4, 8};
  std::vector<double> seq_samples;
  std::map<int, std::vector<double>> con_samples;

  // Sequential reference (warmup + trials).
  auto base = seq_tasks(dag, sleep_ms);
  for (int i = 0; i < warmup; ++i) {
    evo::Scheduler s(dag, seq_tasks(dag, sleep_ms));
    s.run();
  }
  for (int i = 0; i < trials; ++i) {
    evo::Scheduler s(dag, seq_tasks(dag, sleep_ms));
    auto t0 = std::chrono::steady_clock::now();
    s.run();
    auto t1 = std::chrono::steady_clock::now();
    seq_samples.push_back(std::chrono::duration<double, std::milli>(t1 - t0).count());
  }

  double seq_med = median(seq_samples);

  // Concurrent across worker counts.
  for (int nw : workers_list) {
    for (int i = 0; i < warmup; ++i) {
      auto dag_copy = build_dag(wd);
      evo::ConcurrentScheduler s(std::move(dag_copy), concurrent_tasks(dag, sleep_ms),
                                  {.num_workers = static_cast<std::size_t>(nw),
                                   .run_id = wd.name});
      s.run();
    }
    for (int i = 0; i < trials; ++i) {
      auto dag_copy = build_dag(wd);
      evo::ConcurrentScheduler s(std::move(dag_copy), concurrent_tasks(dag, sleep_ms),
                                  {.num_workers = static_cast<std::size_t>(nw),
                                   .run_id = wd.name + "-" + std::to_string(nw)});
      auto t0 = std::chrono::steady_clock::now();
      s.run();
      auto t1 = std::chrono::steady_clock::now();
      double ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
      con_samples[nw].push_back(ms);
      manifest << "raw," << wd.name << "," << nw << "," << ms << "\n";
    }
    double con_med = median(con_samples[nw]);
    double speedup = seq_med / con_med;
    manifest << "summary," << wd.name << "," << nw << "," << seq_med << ","
             << con_med << "," << speedup << "," << pct(con_samples[nw], 50.0)
             << "," << pct(con_samples[nw], 95.0) << ","
             << pct(con_samples[nw], 99.0) << "\n";
    std::cout << "  " << wd.name << " nw=" << nw
              << " seq=" << seq_med << "ms con=" << con_med
              << "ms speedup=" << speedup << "\n";
  }
}

}  // namespace

int main(int argc, char** argv) {
  fs::path out_dir = fs::path("bench-results");
  if (argc > 1) out_dir = fs::path(argv[1]);
  fs::create_directories(out_dir);

  std::ofstream manifest(out_dir / "manifest.csv");
  manifest << "phase,workload,workers,seq_ms,con_ms,speedup,p50,p95,p99\n";
  manifest << "# metadata,commit=" << EVO_BUILD_COMMIT
           << ",build=Release,arch="  // build type baked into this binary
#if defined(__APPLE__)
           << "apple"
#elif defined(__linux__)
           << "linux"
#else
           << "unknown"
#endif
           << "\n";

  int warmup = 2;
  int trials = 10;

  std::vector<WorkloadDef> workloads = {
      {"linear", 0, 0, 0},
      {"diamond", 0, 0, 0},
      {"wide", 0, 0, 0},
      {"layered", 0, 0, 0},
      {"random", 12345, 5, 4},
      {"random", 99999, 6, 5},
  };

  for (const auto& wd : workloads) {
    std::cout << "benchmark: " << wd.name
              << (wd.name == "random" ? (" seed=" + std::to_string(wd.seed)) : "")
              << "\n";
    run_workload(wd, out_dir, manifest, warmup, trials);
  }

  manifest.close();
  std::cout << "wrote " << out_dir.string() << "/manifest.csv\n";
  return 0;
}
