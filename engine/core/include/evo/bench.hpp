#pragma once

// Benchmark-only synthetic tasks and deterministic workload generation
// (Milestone 06).
//
// IMPORTANT: types prefixed `bench:` exist ONLY in this engine test/benchmark
// harness. They are intentionally absent from the product planner node
// registry (features/workflows/nodes/* in the TypeScript app) and must never
// be registered there — they are synthetic I/O / CPU stand-ins for evidence-
// grade measurement, not user-facing node types.

#include <cstdint>
#include <map>

#include "evo/concurrent_scheduler.hpp"
#include "evo/scheduler.hpp"

namespace evo::bench {

// Simulated I/O-bound task: sleeps for the caller-configured milliseconds.
// The per-node duration is captured by closure, keyed on NodeId.
TaskFn sleep_task(const std::map<NodeId, int>& ms_per_node);

// Synthetic CPU-bound task: performs `iters` deterministic work units so
// timing depends on CPU, not on wall-clock sleep. Per-node iteration count
// is captured by closure, keyed on NodeId.
TaskFn burn_task(const std::map<NodeId, unsigned long long>& iters_per_node);

// Cooperative variants (Milestone 11): the returned ConcurrentTaskFn polls the
// supplied stop_token and aborts promptly when cancellation is requested, so
// cancellation tests can verify in-flight tasks do not run to completion.
ConcurrentTaskFn sleep_task_cooperative(
    const std::map<NodeId, int>& ms_per_node);

ConcurrentTaskFn burn_task_cooperative(
    const std::map<NodeId, unsigned long long>& iters_per_node);

// Deterministic PRNG (xorshift64*) seeded for reproducible workloads and
// benchmark seeds. `next(n)` returns a uniform value in [0, n) with rejection
// of the modulo-bias remainder so small ranges stay fair.
class Rng {
 public:
  explicit Rng(std::uint64_t seed);
  std::uint64_t next();              // next raw 64-bit value
  std::uint64_t next(std::uint64_t n);  // uniform in [0, n); 0 if n == 0
 private:
  std::uint64_t state_;
};

// A generated workload: an acyclic, trigger-reachable Dag plus per-node bench
// parameters. Same seed + bounds => identical output (reproducible).
struct Generated {
  Dag dag;
  std::map<NodeId, int> sleep_ms;
  std::map<NodeId, unsigned long long> burn_iters;
};

// Build a random acyclic DAG: one trigger ("start") plus width*depth action
// nodes, each wired to 1..k random *earlier* nodes (earlier index => acyclicity
// by construction; start is always reachable). Per-node bench params are
// randomized with `seed`. Same seed => identical graph + identical params.
Generated generate_workload(std::uint64_t seed, int width, int depth);

}  // namespace evo::bench
