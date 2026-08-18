#include "evo/bench.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <string>
#include <thread>
#include <vector>

namespace evo::bench {

namespace {
// Magic constant mixed per-iteration so the compiler cannot optimize the loop
// away while keeping it deterministic.
constexpr unsigned long long kMix = 0x9E3779B97F4A7C15ULL;
}  // namespace

TaskFn sleep_task(const std::map<NodeId, int>& ms_per_node) {
  return [ms = ms_per_node](const NodeSpec& spec) -> TaskResult {
    auto it = ms.find(spec.id);
    const int duration = it == ms.end() ? 0 : it->second;
    if (duration > 0) {
      std::this_thread::sleep_for(std::chrono::milliseconds(duration));
    }
    return TaskResult{true, "slept " + std::to_string(duration) + "ms"};
  };
}

TaskFn burn_task(const std::map<NodeId, unsigned long long>& iters_per_node) {
  return [iters = iters_per_node](const NodeSpec& spec) -> TaskResult {
    auto it = iters.find(spec.id);
    const unsigned long long n = it == iters.end() ? 0 : it->second;
    volatile unsigned long long acc = 0;
    for (unsigned long long i = 0; i < n; ++i) {
      acc += i * kMix;
    }
    (void)acc;
    return TaskResult{true, "burned " + std::to_string(n) + " iters"};
  };
}

Rng::Rng(std::uint64_t seed) {
  state_ = seed != 0 ? seed : 0x9E3779B97F4A7C15ULL;
  for (int i = 0; i < 20; ++i) next();  // warmup to escape seed bias
}

std::uint64_t Rng::next() {
  std::uint64_t x = state_;
  x ^= x >> 12;
  x ^= x << 25;
  x ^= x >> 27;
  state_ = x;
  return x * 0x2545F4914F6CDD1DULL;
}

std::uint64_t Rng::next(std::uint64_t n) {
  if (n == 0) return 0;
  const std::uint64_t limit = ~std::uint64_t{0} - (~std::uint64_t{0} % n);
  std::uint64_t r;
  do {
    r = next();
  } while (r >= limit);
  return r % n;
}

Generated generate_workload(std::uint64_t seed, int width, int depth) {
  Rng rng(seed);
  Generated gen;

  std::vector<NodeSpec> nodes;
  std::vector<Edge> edges;
  nodes.push_back({NodeId{"start"}, NodeKind::Trigger, "start"});

  const int total_actions = width * depth;
  std::vector<NodeId> action_ids;
  action_ids.reserve(total_actions);
  for (int i = 0; i < total_actions; ++i) {
    NodeId id{"n" + std::to_string(i)};
    action_ids.push_back(id);
    const std::string bench_type = (i % 2 == 0) ? "bench:sleep" : "bench:burn";
    nodes.push_back({id, NodeKind::Action, bench_type});
    gen.sleep_ms[id] = static_cast<int>(1 + rng.next(50));        // 1..50 ms
    gen.burn_iters[id] = 1000ULL + rng.next(4000ULL);             // ~1000..4999
  }

  // Predecessors are always earlier-indexed (acyclic by construction).
  std::vector<NodeId> earlier;
  earlier.push_back(NodeId{"start"});
  for (int i = 0; i < total_actions; ++i) {
    const NodeId cur = action_ids[i];
    const int cap = static_cast<int>(earlier.size());
    const int k = std::min(cap, 1 + static_cast<int>(rng.next(3)));  // 1..3 picks
    std::vector<NodeId> pool = earlier;
    for (int j = 0; j < k && !pool.empty(); ++j) {
      const std::uint64_t idx = rng.next(static_cast<std::uint64_t>(pool.size()));
      const NodeId pred = pool[idx];
      edges.push_back(Edge{pred, cur});
      pool.erase(pool.begin() + static_cast<std::ptrdiff_t>(idx));
    }
    earlier.push_back(cur);  // new node joins the "earlier" pool for successors
  }

  auto built = Dag::build(std::move(nodes), std::move(edges));
  // generate_workload is a test/bench helper; a build failure here is a logic
  // bug in the generator, so assert rather than silently return a bad graph.
  // (No exceptions: fail fast via abort to surface generator bugs loudly.)
  if (!built.ok()) {
    std::abort();
  }
  gen.dag = std::move(*built.dag);
  return gen;
}

}  // namespace evo::bench
