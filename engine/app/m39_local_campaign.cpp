// Milestone 39 — final reproducible local-scheduler performance campaign.
//
// Measures the sequential reference scheduler (Scheduler) against the
// concurrent scheduler (ConcurrentScheduler) across the workload matrix the
// master prompt requires:
//   shapes  : linear, diamond, wide, layered, seeded-random
//   sizes   : 10 / 50 / 100 / 500 / 1000 logical action nodes
//   profiles: simulated I/O-bound (bench:sleep) and synthetic CPU (bench:burn),
//             run SEPARATELY and never conflated (methodology §1.6)
//   threads : 1 / 2 / 4 / 8 concurrent worker threads
//
// Every measurement is a repeated trial with warmup; raw per-trial samples are
// preserved (never only aggregates). All durations use std::chrono::steady_clock
// (monotonic). Output follows docs/phase2/BENCHMARK_METHODOLOGY.md §4:
//   manifest.json / samples.jsonl / summary.json / command.txt
// written to the directory in argv[1] (default "m39-local-results").
//
// Evidence-grade provenance (methodology §1.4/§5): the manifest records commit
// (EVO_BUILD_COMMIT baked at configure time), build mode, hardware, OS,
// compiler, thread counts, workload definition, RNG seed, warmup + sample
// counts. A result is only evidence-grade when workload/build-mode/hardware/
// sample-count/commit are all identified together.
//
// This is a LOCAL scheduler-only campaign (in-process C++ DAGs, no external
// I/O, no browser). Per methodology §1.7 its speedups must NEVER be generalized
// into browser-automation speedups. Distributed worker scaling + chaos evidence
// are captured separately by evo_m39_scaling_test and the fault-injection tests.

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <numeric>
#include <string>
#include <vector>

#include "evo/bench.hpp"
#include "evo/concurrent_scheduler.hpp"
#include "evo/dag.hpp"
#include "evo/scheduler.hpp"

namespace fs = std::filesystem;
using evo::ConcurrentScheduler;
using evo::ConcurrentTaskFn;
using evo::Dag;
using evo::Edge;
using evo::NodeKind;
using evo::NodeId;
using evo::NodeSpec;
using evo::Scheduler;
using evo::TaskFn;

namespace {

std::string env_or(const char* name, const std::string& fallback) {
  const char* v = std::getenv(name);
  return (v && *v) ? std::string(v) : fallback;
}

double pct(std::vector<double> xs, double p) {
  if (xs.empty()) return 0.0;
  std::sort(xs.begin(), xs.end());
  const double idx = (p / 100.0) * static_cast<double>(xs.size() - 1);
  return xs[static_cast<size_t>(idx + 0.5)];
}

double median(std::vector<double> xs) { return pct(std::move(xs), 50.0); }

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

std::string compiler_string() {
#if defined(__clang__)
  return std::string("clang ") + __clang_version__;
#elif defined(__GNUC__)
  return std::string("gcc ") + std::to_string(__GNUC__) + "." +
         std::to_string(__GNUC_MINOR__);
#else
  return "unknown";
#endif
}

// --- Parametric DAG generators (one per shape, sized by action-node count) ---

Dag build_linear(int n) {
  std::vector<NodeSpec> nodes{{NodeId{"start"}, NodeKind::Trigger, "bench:noop"}};
  std::vector<Edge> edges;
  std::string prev = "start";
  for (int i = 0; i < n; ++i) {
    const std::string id = "n" + std::to_string(i);
    nodes.push_back({NodeId{id}, NodeKind::Action, "bench:work"});
    edges.push_back({NodeId{prev}, NodeId{id}});
    prev = id;
  }
  auto b = Dag::build(std::move(nodes), std::move(edges));
  return std::move(*b.dag);
}

Dag build_wide(int n) {
  std::vector<NodeSpec> nodes{{NodeId{"start"}, NodeKind::Trigger, "bench:noop"}};
  std::vector<Edge> edges;
  for (int i = 0; i < n; ++i) {
    const std::string id = "w" + std::to_string(i);
    nodes.push_back({NodeId{id}, NodeKind::Action, "bench:work"});
    edges.push_back({NodeId{"start"}, NodeId{id}});
  }
  auto b = Dag::build(std::move(nodes), std::move(edges));
  return std::move(*b.dag);
}

// start -> two parallel chains of ~n/2 nodes each -> join.
Dag build_diamond(int n) {
  std::vector<NodeSpec> nodes{{NodeId{"start"}, NodeKind::Trigger, "bench:noop"}};
  std::vector<Edge> edges;
  const int half = std::max(1, n / 2);
  std::string prev_l = "start";
  std::string prev_r = "start";
  for (int i = 0; i < half; ++i) {
    const std::string l = "l" + std::to_string(i);
    const std::string r = "r" + std::to_string(i);
    nodes.push_back({NodeId{l}, NodeKind::Action, "bench:work"});
    nodes.push_back({NodeId{r}, NodeKind::Action, "bench:work"});
    edges.push_back({NodeId{prev_l}, NodeId{l}});
    edges.push_back({NodeId{prev_r}, NodeId{r}});
    prev_l = l;
    prev_r = r;
  }
  nodes.push_back({NodeId{"join"}, NodeKind::Action, "bench:work"});
  edges.push_back({NodeId{prev_l}, NodeId{"join"}});
  edges.push_back({NodeId{prev_r}, NodeId{"join"}});
  auto b = Dag::build(std::move(nodes), std::move(edges));
  return std::move(*b.dag);
}

// `layers` layers x `per_layer` nodes; every node in layer i depends on every
// node in layer i-1 (bipartite between adjacent layers).
Dag build_layered(int n) {
  int layers = 4;
  while (layers * layers < n && layers < 32) ++layers;
  const int per_layer = std::max(1, n / layers);
  std::vector<NodeSpec> nodes{{NodeId{"start"}, NodeKind::Trigger, "bench:noop"}};
  std::vector<Edge> edges;
  std::vector<std::string> prev_layer{"start"};
  for (int L = 0; L < layers; ++L) {
    std::vector<std::string> cur;
    for (int j = 0; j < per_layer; ++j) {
      const std::string id =
          "L" + std::to_string(L) + "n" + std::to_string(j);
      nodes.push_back({NodeId{id}, NodeKind::Action, "bench:work"});
      for (const auto& p : prev_layer) {
        edges.push_back({NodeId{p}, NodeId{id}});
      }
      cur.push_back(id);
    }
    prev_layer = std::move(cur);
  }
  auto b = Dag::build(std::move(nodes), std::move(edges));
  return std::move(*b.dag);
}

// Seeded random acyclic DAG: same construction as the M06 generator
// (each node wires to 1..3 uniformly-random EARLIER nodes, so acyclicity is by
// construction and "start" is always reachable), but with a uniform node type
// so the io/cpu profile separation stays clean. Same seed => identical graph.
Dag build_random(int n, std::uint64_t seed) {
  evo::bench::Rng rng(seed);
  std::vector<NodeSpec> nodes{{NodeId{"start"}, NodeKind::Trigger, "bench:noop"}};
  std::vector<Edge> edges;
  std::vector<NodeId> earlier{NodeId{"start"}};
  for (int i = 0; i < n; ++i) {
    const NodeId cur{"n" + std::to_string(i)};
    nodes.push_back({cur, NodeKind::Action, "bench:work"});
    const int cap = static_cast<int>(earlier.size());
    const int k = std::min(cap, 1 + static_cast<int>(rng.next(3)));
    std::vector<NodeId> pool = earlier;
    for (int j = 0; j < k && !pool.empty(); ++j) {
      const std::uint64_t idx =
          rng.next(static_cast<std::uint64_t>(pool.size()));
      edges.push_back({pool[idx], cur});
      pool.erase(pool.begin() + static_cast<std::ptrdiff_t>(idx));
    }
    earlier.push_back(cur);
  }
  auto b = Dag::build(std::move(nodes), std::move(edges));
  return std::move(*b.dag);
}

Dag build_dag(const std::string& shape, int n, std::uint64_t seed) {
  if (shape == "linear") return build_linear(n);
  if (shape == "wide") return build_wide(n);
  if (shape == "diamond") return build_diamond(n);
  if (shape == "layered") return build_layered(n);
  if (shape == "random") return build_random(n, seed);
  return {};
}

// Per-node task parameters for a profile. io: uniform sleep; cpu: uniform burn.
struct ProfileParams {
  std::map<NodeId, int> sleep_ms;
  std::map<NodeId, unsigned long long> burn_iters;
};

ProfileParams make_params(const Dag& dag, const std::string& profile, int io_ms,
                          unsigned long long cpu_iters) {
  ProfileParams p;
  for (const auto& id : dag.node_ids()) {
    if (profile == "io") {
      p.sleep_ms[id] = io_ms;
    } else {
      p.burn_iters[id] = cpu_iters;
    }
  }
  return p;
}

std::map<std::string, TaskFn> seq_tasks(const std::string& profile,
                                        const ProfileParams& p) {
  std::map<std::string, TaskFn> tasks;
  tasks["bench:noop"] = [](const evo::NodeSpec&) {
    return evo::TaskResult{true, "noop"};
  };
  tasks["bench:work"] = (profile == "io")
                            ? evo::bench::sleep_task(p.sleep_ms)
                            : evo::bench::burn_task(p.burn_iters);
  return tasks;
}

std::map<std::string, ConcurrentTaskFn> concurrent_tasks(
    const std::string& profile, const ProfileParams& p) {
  std::map<std::string, ConcurrentTaskFn> tasks;
  tasks["bench:noop"] = [](const evo::NodeSpec&, std::stop_token) {
    return evo::TaskResult{true, "noop"};
  };
  tasks["bench:work"] =
      (profile == "io") ? evo::bench::sleep_task_cooperative(p.sleep_ms)
                        : evo::bench::burn_task_cooperative(p.burn_iters);
  return tasks;
}

// One measurement cell: a (shape, size, profile, threads) point.
struct Sample {
  std::string shape;
  int size = 0;
  std::string profile;
  int threads = 0;         // 0 => sequential reference
  int trial = 0;
  double makespan_ms = 0;
  double throughput_nps = 0;   // logical nodes / second
  double r2d_p50_ms = 0;       // ready-to-dispatch latency percentiles
  double r2d_p95_ms = 0;
  double r2d_p99_ms = 0;
  double qlat_p50_ms = 0;      // ready-queue wait percentiles
  double qlat_p95_ms = 0;
  double qlat_p99_ms = 0;
  std::size_t max_queue_depth = 0;
  std::size_t max_in_flight = 0;
  std::size_t nodes = 0;
};

double run_sequential(const std::string& shape, int size, std::uint64_t seed,
                      const std::string& profile, const ProfileParams& params) {
  auto dag = build_dag(shape, size, seed);
  Scheduler s(std::move(dag), seq_tasks(profile, params));
  const auto t0 = std::chrono::steady_clock::now();
  s.run();
  const auto t1 = std::chrono::steady_clock::now();
  return std::chrono::duration<double, std::milli>(t1 - t0).count();
}

Sample run_concurrent(const std::string& shape, int size, std::uint64_t seed,
                      const std::string& profile, const ProfileParams& params,
                      int threads, int trial) {
  auto dag = build_dag(shape, size, seed);
  const std::size_t node_count = dag.node_count();
  ConcurrentScheduler s(std::move(dag), concurrent_tasks(profile, params),
                        {.num_workers = static_cast<std::size_t>(threads),
                         .run_id = shape + "-" + std::to_string(size)});
  const auto t0 = std::chrono::steady_clock::now();
  auto log = s.run();
  const auto t1 = std::chrono::steady_clock::now();
  const auto& m = s.metrics();

  Sample out;
  out.shape = shape;
  out.size = size;
  out.profile = profile;
  out.threads = threads;
  out.trial = trial;
  out.makespan_ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
  out.nodes = node_count;
  out.throughput_nps = out.makespan_ms > 0
                           ? (1000.0 * static_cast<double>(node_count)) /
                                 out.makespan_ms
                           : 0.0;
  out.max_queue_depth = m.max_queue_depth;
  out.max_in_flight = m.max_in_flight;

  std::vector<double> r2d, qlat;
  r2d.reserve(log.runs.size());
  qlat.reserve(log.runs.size());
  for (const auto& r : log.runs) {
    r2d.push_back(std::chrono::duration<double, std::milli>(
                      r.ready_to_dispatch_latency())
                      .count());
    qlat.push_back(
        std::chrono::duration<double, std::milli>(r.queue_latency()).count());
  }
  out.r2d_p50_ms = pct(r2d, 50);
  out.r2d_p95_ms = pct(r2d, 95);
  out.r2d_p99_ms = pct(r2d, 99);
  out.qlat_p50_ms = pct(qlat, 50);
  out.qlat_p95_ms = pct(qlat, 95);
  out.qlat_p99_ms = pct(qlat, 99);
  return out;
}

}  // namespace

int main(int argc, char** argv) {
  const fs::path out_dir = argc > 1 ? fs::path(argv[1]) : fs::path("m39-local");
  fs::create_directories(out_dir);

  const std::string commit =
#ifdef EVO_BUILD_COMMIT
      EVO_BUILD_COMMIT;
#else
      "unknown";
#endif
  const std::string build_mode = env_or("EVO_M39_BUILD_MODE", "Release");
  const int warmup = std::atoi(env_or("EVO_M39_WARMUP", "1").c_str());
  const int trials = std::atoi(env_or("EVO_M39_TRIALS", "3").c_str());
  const int io_ms = std::atoi(env_or("EVO_M39_IO_MS", "2").c_str());
  const unsigned long long cpu_iters =
      std::strtoull(env_or("EVO_M39_CPU_ITERS", "100000").c_str(), nullptr, 10);

  const std::vector<std::string> shapes = {"linear", "diamond", "wide",
                                           "layered", "random"};
  const std::vector<int> sizes = {10, 50, 100, 500, 1000};
  const std::vector<std::string> profiles = {"io", "cpu"};
  const std::vector<int> threads = {1, 2, 4, 8};
  const std::uint64_t seed = 0x5EED39;

  std::vector<Sample> samples;

  for (const auto& profile : profiles) {
    for (const auto& shape : shapes) {
      for (int size : sizes) {
        // Sequential reference (warmup + trials), identical DAG/params.
        auto base_dag = build_dag(shape, size, seed);
        auto params = make_params(base_dag, profile, io_ms, cpu_iters);
        for (int i = 0; i < warmup; ++i) {
          run_sequential(shape, size, seed, profile, params);
        }
        std::vector<double> seq_samples;
        for (int t = 0; t < trials; ++t) {
          seq_samples.push_back(
              run_sequential(shape, size, seed, profile, params));
        }
        const double seq_med = median(seq_samples);

        // Record the sequential reference as threads=0 samples.
        for (int t = 0; t < trials; ++t) {
          Sample s;
          s.shape = shape;
          s.size = size;
          s.profile = profile;
          s.threads = 0;
          s.trial = t;
          s.makespan_ms = seq_samples[t];
          s.nodes = base_dag.node_count();
          s.throughput_nps = seq_samples[t] > 0
                                 ? (1000.0 * static_cast<double>(s.nodes)) /
                                       seq_samples[t]
                                 : 0.0;
          samples.push_back(s);
        }

        // Concurrent across thread counts.
        for (int nw : threads) {
          for (int i = 0; i < warmup; ++i) {
            run_concurrent(shape, size, seed, profile, params, nw, -1);
          }
          for (int t = 0; t < trials; ++t) {
            samples.push_back(
                run_concurrent(shape, size, seed, profile, params, nw, t));
          }
        }

        std::printf("  %-8s %-7s n=%-5d seq_med=%.2fms\n", profile.c_str(),
                    shape.c_str(), size, seq_med);
      }
    }
  }

  // --- Emit §4 artifacts -----------------------------------------------------
  const std::string hw = hardware_string();
  const std::int64_t now_ms =
      std::chrono::duration_cast<std::chrono::milliseconds>(
          std::chrono::system_clock::now().time_since_epoch())
          .count();

  {
    std::ofstream mf(out_dir / "manifest.json");
    mf << "{\n"
       << "  \"slug\": \"m39_local_scheduler_campaign\",\n"
       << "  \"workload_class\": \"scheduler-only synthetic (in-process C++ "
          "DAGs; no external I/O)\",\n"
       << "  \"profiles\": [\"simulated I/O-bound (bench:sleep)\", \"synthetic "
          "CPU (bench:burn)\"],\n"
       << "  \"shapes\": [\"linear\", \"diamond\", \"wide\", \"layered\", "
          "\"seeded-random\"],\n"
       << "  \"sizes\": [10, 50, 100, 500, 1000],\n"
       << "  \"thread_counts\": [1, 2, 4, 8],\n"
       << "  \"sequential_reference\": \"threads=0 rows\",\n"
       << "  \"rng_seed\": " << seed << ",\n"
       << "  \"warmup\": " << warmup << ",\n"
       << "  \"trials\": " << trials << ",\n"
       << "  \"io_sleep_ms_per_node\": " << io_ms << ",\n"
       << "  \"cpu_burn_iters_per_node\": " << cpu_iters << ",\n"
       << "  \"build_mode\": \"" << build_mode << "\",\n"
       << "  \"commit\": \"" << commit << "\",\n"
       << "  \"hardware\": \"" << hw << "\",\n"
       << "  \"compiler\": \"" << compiler_string() << "\",\n"
       << "  \"clock\": \"steady_clock (monotonic) for all durations\",\n"
       << "  \"note\": \"local scheduler-only evidence; MUST NOT be "
          "generalized to browser end-to-end speedup (methodology 1.7)\",\n"
       << "  \"generated_at_wall_ms\": " << now_ms << "\n"
       << "}\n";
  }

  {
    std::ofstream sf(out_dir / "samples.jsonl");
    for (const auto& s : samples) {
      sf << "{\"shape\":\"" << s.shape << "\",\"size\":" << s.size
         << ",\"profile\":\"" << s.profile << "\",\"threads\":" << s.threads
         << ",\"trial\":" << s.trial << ",\"makespan_ms\":" << s.makespan_ms
         << ",\"throughput_nps\":" << s.throughput_nps
         << ",\"r2d_p50_ms\":" << s.r2d_p50_ms
         << ",\"r2d_p95_ms\":" << s.r2d_p95_ms
         << ",\"r2d_p99_ms\":" << s.r2d_p99_ms
         << ",\"qlat_p50_ms\":" << s.qlat_p50_ms
         << ",\"qlat_p95_ms\":" << s.qlat_p95_ms
         << ",\"qlat_p99_ms\":" << s.qlat_p99_ms
         << ",\"max_queue_depth\":" << s.max_queue_depth
         << ",\"max_in_flight\":" << s.max_in_flight
         << ",\"nodes\":" << s.nodes << "}\n";
    }
  }

  // Summary: per (profile, shape, size, threads) median makespan + speedup vs
  // the sequential reference median, plus parallel efficiency. Also emits
  // thread_scaling_vs_1t = con(t=1)/con(t=N): the honest CPU-profile scaling
  // signal, because the cooperative CPU task polls its stop_token every 256
  // iterations (adding overhead vs the plain sequential task), so the
  // seq-vs-con "speedup" understates the concurrent scheduler's own scaling.
  {
    auto key = [](const std::string& p, const std::string& sh, int sz, int th) {
      return p + "|" + sh + "|" + std::to_string(sz) + "|" + std::to_string(th);
    };
    std::map<std::string, std::vector<double>> makespans;
    for (const auto& s : samples) {
      makespans[key(s.profile, s.shape, s.size, s.threads)].push_back(
          s.makespan_ms);
    }
    std::ofstream sumf(out_dir / "summary.json");
    sumf << "{\n  \"cells\": [\n";
    bool first = true;
    for (const auto& profile : profiles) {
      for (const auto& shape : shapes) {
        for (int size : sizes) {
          const double seq_med =
              median(makespans[key(profile, shape, size, 0)]);
          const double con_t1 =
              median(makespans[key(profile, shape, size, 1)]);
          for (int nw : threads) {
            const auto& v = makespans[key(profile, shape, size, nw)];
            if (v.empty()) continue;
            const double med = median(v);
            const double speedup = med > 0 ? seq_med / med : 0.0;
            const double efficiency = speedup / static_cast<double>(nw);
            const double thread_scaling =
                (con_t1 > 0 && med > 0) ? con_t1 / med : 0.0;
            if (!first) sumf << ",\n";
            first = false;
            sumf << "    {\"profile\":\"" << profile << "\",\"shape\":\""
                 << shape << "\",\"size\":" << size << ",\"threads\":" << nw
                 << ",\"seq_median_ms\":" << seq_med
                 << ",\"con_median_ms\":" << med
                 << ",\"con_p95_ms\":" << pct(v, 95)
                 << ",\"con_p99_ms\":" << pct(v, 99)
                 << ",\"speedup\":" << speedup
                 << ",\"parallel_efficiency\":" << efficiency
                 << ",\"thread_scaling_vs_1t\":" << thread_scaling
                 << ",\"samples\":" << v.size() << "}";
          }
        }
      }
    }
    sumf << "\n  ]\n}\n";
  }

  {
    std::ofstream cf(out_dir / "command.txt");
    cf << "# Reproduce the M39 local scheduler campaign (Release build):\n"
       << "cmake --build engine/build --target evo-m39-local\n"
       << "EVO_M39_TRIALS=" << trials << " EVO_M39_WARMUP=" << warmup
       << " engine/build/evo-m39-local " << out_dir.string() << "\n";
  }

  std::printf("m39-local: wrote %zu samples to %s\n", samples.size(),
              out_dir.string().c_str());
  return 0;
}
