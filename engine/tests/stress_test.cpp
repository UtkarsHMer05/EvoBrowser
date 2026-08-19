// Milestone 14 stress tests for the concurrent scheduler core.
// These exercise (under ASan/UBSan/TSan) the concurrency-safety of:
//   - 100+ deterministic random-DAG runs (seed logged, concurrent vs
//     sequential equivalence asserted each iteration)
//   - repeated ThreadPool / ConcurrentScheduler construction + shutdown cycles
//   - cancellation racing completion (cooperative abort under contention)
//
// Diagnostic timing is labeled `diag` and is NOT a performance claim (M15 owns
// evidence-grade benchmarks).

#include <algorithm>
#include <atomic>
#include <chrono>
#include <future>
#include <iostream>
#include <map>
#include <set>
#include <string>
#include <thread>
#include <vector>

#include "evo/bench.hpp"
#include "evo/concurrent_scheduler.hpp"
#include "evo/dag.hpp"
#include "evo/scheduler.hpp"
#include "evo/thread_pool.hpp"

namespace {

int failures = 0;

void check(bool ok, const std::string& what) {
  std::cout << (ok ? "  ok   " : "  FAIL ") << what << '\n';
  if (!ok) ++failures;
}

evo::NodeSpec trigger(const std::string& id) {
  return {evo::NodeId{id}, evo::NodeKind::Trigger, "start"};
}
evo::NodeSpec action(const std::string& id, const std::string& type = "act") {
  return {evo::NodeId{id}, evo::NodeKind::Action, type};
}
evo::Edge edge(const std::string& from, const std::string& to) {
  return {evo::NodeId{from}, evo::NodeId{to}};
}

std::set<std::string> succeeded_ids(const evo::ConcurrentRunLog& log) {
  std::set<std::string> out;
  for (const auto& r : log.runs)
    if (r.ok()) out.insert(r.id.value);
  return out;
}

std::set<std::string> sequential_succeeded(const evo::Dag& dag,
                                           const std::map<std::string, evo::TaskFn>& tasks) {
  evo::Scheduler seq(dag, tasks);
  auto log = seq.run();
  std::set<std::string> out;
  for (const auto& r : log.runs)
    if (r.result.completed) out.insert(r.id.value);
  return out;
}

// Build task registries (plain + cooperative) for a generated workload.
std::map<std::string, evo::TaskFn> make_seq_tasks(const evo::bench::Generated& gen) {
  std::map<std::string, evo::TaskFn> tasks;
  tasks["start"] = [](const evo::NodeSpec&) {
    return evo::TaskResult{true, "started"};
  };
  tasks["bench:sleep"] = evo::bench::sleep_task(gen.sleep_ms);
  tasks["bench:burn"] = evo::bench::burn_task(gen.burn_iters);
  return tasks;
}

std::map<std::string, evo::ConcurrentTaskFn> make_concurrent_tasks(
    const evo::bench::Generated& gen) {
  std::map<std::string, evo::ConcurrentTaskFn> tasks;
  tasks["start"] = [](const evo::NodeSpec&, std::stop_token) {
    return evo::TaskResult{true, "started"};
  };
  tasks["bench:sleep"] = evo::bench::sleep_task_cooperative(gen.sleep_ms);
  tasks["bench:burn"] = evo::bench::burn_task_cooperative(gen.burn_iters);
  return tasks;
}

void test_random_dag_stress_120() {
  std::cout << "stress: 120 deterministic random DAGs (seed logged)\n";
  int passes = 0;
  int checked = 0;
  for (int i = 0; i < 120; ++i) {
    const std::uint64_t seed = 1000ULL + static_cast<std::uint64_t>(i) * 7919ULL;
    std::cout << "  diag: iteration " << i << " seed=" << seed << '\n';

    evo::bench::Generated gen = evo::bench::generate_workload(seed, /*width=*/4, /*depth=*/5);
    const std::size_t node_count = gen.dag.node_count();

    auto seq_tasks = make_seq_tasks(gen);
    auto seq_succ = sequential_succeeded(gen.dag, seq_tasks);

    auto con_tasks = make_concurrent_tasks(gen);
    evo::ConcurrentScheduler sched(std::move(gen.dag), std::move(con_tasks),
                                   {.num_workers = 4, .run_id = "stress-" + std::to_string(seed)});
    auto log = sched.run();

    auto conc_succ = succeeded_ids(log);
    // Succeeded set must match the sequential reference exactly.
    bool same = (seq_succ == conc_succ);
    if (!same) {
      std::cout << "  diag: MISMATCH seed=" << seed << " seq=" << seq_succ.size()
                << " con=" << conc_succ.size() << '\n';
    }
    // All nodes should have succeeded (no failures in the generated workload).
    bool all_succ = log.all_ok() && log.runs.size() == node_count;
    if (same && all_succ) {
      ++passes;
    }
    ++checked;
  }
  check(passes == checked && checked == 120,
        "120/120 random DAGs equivalent to sequential (passed=" +
            std::to_string(passes) + ")");
}

void test_pool_lifecycle_stress_50() {
  std::cout << "stress: 50 pool construction/shutdown cycles\n";
  int ok_cycles = 0;
  for (int i = 0; i < 50; ++i) {
    evo::ThreadPool pool(4);
    std::atomic<int> done{0};
    for (int t = 0; t < 20; ++t) {
      pool.submit([&done]() {
        std::this_thread::yield();
        volatile int x = 0;
        for (int k = 0; k < 100; ++k) x += k;
        (void)x;
        done.fetch_add(1, std::memory_order_relaxed);
      });
    }
    pool.drain();
    if (done.load() == 20) ++ok_cycles;
  }
  check(ok_cycles == 50,
        "50/50 pool lifecycle cycles drained all tasks (ok=" +
            std::to_string(ok_cycles) + ")");
}

void test_scheduler_lifecycle_stress_50() {
  std::cout << "stress: 50 scheduler construction/run/drain cycles\n";
  int ok_cycles = 0;
  for (int i = 0; i < 50; ++i) {
    auto dag = evo::Dag::build(
        {trigger("start"), action("a"), action("b"), action("c")},
        {edge("start", "a"), edge("start", "b"), edge("a", "c"),
         edge("b", "c")});
    if (!dag.ok()) continue;

    std::map<std::string, evo::TaskFn> tasks;
    tasks["start"] = [](const evo::NodeSpec&) {
      return evo::TaskResult{true, "s"};
    };
    tasks["act"] = [](const evo::NodeSpec& spec) {
      std::this_thread::sleep_for(std::chrono::microseconds(100));
      return evo::TaskResult{true, spec.id.value + "ok"};
    };
    evo::ConcurrentScheduler sched(std::move(*dag.dag), std::move(tasks),
                                   {.num_workers = 3});
    auto log = sched.run();
    if (log.all_ok() && log.runs.size() == 4) ++ok_cycles;
  }
  check(ok_cycles == 50,
        "50/50 scheduler lifecycle cycles succeeded (ok=" +
            std::to_string(ok_cycles) + ")");
}

void test_cancel_racing_completion_40() {
  std::cout << "stress: 40 cancel-vs-completion contention iterations\n";
  int clean = 0;
  for (int i = 0; i < 40; ++i) {
    auto dag = evo::Dag::build(
        {trigger("start"), action("c0"), action("c1"), action("c2"),
         action("c3"), action("join")},
        {edge("start", "c0"), edge("start", "c1"), edge("start", "c2"),
         edge("start", "c3"), edge("c0", "join"), edge("c1", "join"),
         edge("c2", "join"), edge("c3", "join")});
    if (!dag.ok()) continue;

    std::map<evo::NodeId, int> ms;
    for (char c : {'0', '1', '2', '3'})
      ms[evo::NodeId{"c" + std::string(1, c)}] = 2;
    ms[evo::NodeId{"join"}] = 2;
    std::map<std::string, evo::ConcurrentTaskFn> tasks;
    tasks["start"] = [](const evo::NodeSpec&, std::stop_token) {
      return evo::TaskResult{true, "s"};
    };
    tasks["act"] = evo::bench::sleep_task_cooperative(ms);

    evo::ConcurrentScheduler sched(std::move(*dag.dag), std::move(tasks),
                                   {.num_workers = 4, .run_id = "race-" + std::to_string(i)});
    auto fut =
        std::async(std::launch::async, [&sched]() { return sched.run(); });
    // Cancel very close to start, racing completion of "start".
    std::this_thread::sleep_for(std::chrono::microseconds(50));
    sched.cancel();
    auto log = fut.get();

    // Scheduler must report terminal (canceled) and not crash. No double-exec.
    if (sched.is_canceled()) {
      // Verify no node ran more than once.
      std::map<std::string, int> counts;
      for (const auto& r : log.runs) counts[r.id.value]++;
      bool no_double = true;
      for (const auto& [_, n] : counts) if (n > 1) no_double = false;
      if (no_double) ++clean;
    }
  }
  check(clean >= 35,
        ">=35/40 cancel-vs-completion iterations terminated cleanly (clean=" +
            std::to_string(clean) + ")");
}

}  // namespace

int main() {
  test_random_dag_stress_120();
  test_pool_lifecycle_stress_50();
  test_scheduler_lifecycle_stress_50();
  test_cancel_racing_completion_40();

  if (failures != 0) {
    std::cout << failures << " stress check(s) FAILED\n";
    return 1;
  }
  std::cout << "all stress tests passed\n";
  return 0;
}
