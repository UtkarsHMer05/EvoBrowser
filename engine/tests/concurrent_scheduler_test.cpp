// Milestone 10/11 tests for the concurrent dependency-aware DAG scheduler.
// Covers: equivalence with the sequential reference scheduler, concurrency
// overlap (independent branches overlap while dependencies do not), stress on
// seeded random DAGs, dependency-correctness invariants under concurrency, and
// (Milestone 11) cooperative cancellation and graceful shutdown.

#include <algorithm>
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

std::map<std::string, evo::TaskFn> noop_tasks() {
  std::map<std::string, evo::TaskFn> tasks;
  tasks["start"] = [](const evo::NodeSpec&) {
    return evo::TaskResult{true, "started"};
  };
  tasks["act"] = [](const evo::NodeSpec&) {
    return evo::TaskResult{true, "done"};
  };
  return tasks;
}

// Returns the set of node ids that completed successfully.
std::set<std::string> succeeded_ids(const evo::ConcurrentRunLog& log) {
  std::set<std::string> out;
  for (const auto& r : log.runs) {
    if (r.ok()) out.insert(r.id.value);
  }
  return out;
}

// Helper to read all edges from a Dag (the Dag exposes edges only indirectly;
// we reconstruct from adjacency of every node's successors).
std::vector<evo::Edge> all_edges(const evo::Dag& dag) {
  std::vector<evo::Edge> out;
  for (const auto& from : dag.node_ids()) {
    for (const auto& to : dag.successors(from)) {
      out.push_back(evo::Edge{from, to});
    }
  }
  return out;
}

void test_linear_equiv() {
  std::cout << "concurrent linear chain equivalent to sequential\n";
  auto dag = evo::Dag::build(
      {trigger("start"), action("a"), action("b"), action("c")},
      {edge("start", "a"), edge("a", "b"), edge("b", "c")});
  check(dag.ok(), "dag builds");

  evo::ConcurrentScheduler sched(std::move(*dag.dag), noop_tasks(),
                                 {.num_workers = 4});
  auto log = sched.run();

  check(log.runs.size() == 4, "all 4 nodes executed");
  check(log.all_ok(), "all succeeded");
  check(succeeded_ids(log).size() == 4, "4 distinct nodes succeeded");

  // Dependency ordering: start before a before b before c in (ready -> start).
  std::map<std::string, evo::ConcurrentNodeRun> by_id;
  for (const auto& r : log.runs) by_id[r.id.value] = r;
  check(by_id["start"].started_at <= by_id["a"].started_at,
        "start started before a");
  check(by_id["a"].started_at <= by_id["b"].started_at,
        "a started before b");
  check(by_id["b"].started_at <= by_id["c"].started_at,
        "b started before c");
}

void test_diamond_equiv() {
  std::cout << "concurrent diamond equivalent to sequential\n";
  auto dag = evo::Dag::build(
      {trigger("start"), action("left"), action("right"), action("join")},
      {edge("start", "left"), edge("start", "right"),
       edge("left", "join"), edge("right", "join")});
  check(dag.ok(), "dag builds");

  auto tasks = noop_tasks();
  // Count concurrent overlaps on the independent branches left/right.
  std::atomic<int> concurrent_peak{0};
  std::atomic<int> active{0};
  tasks["act"] = [&](const evo::NodeSpec& spec) -> evo::TaskResult {
    int now = active.fetch_add(1) + 1;
    concurrent_peak.store(std::max(concurrent_peak.load(), now));
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    active.fetch_sub(1);
    return evo::TaskResult{true, spec.id.value + " done"};
  };

  evo::ConcurrentScheduler sched(std::move(*dag.dag), std::move(tasks),
                                 {.num_workers = 4});
  auto log = sched.run();

  check(log.all_ok(), "diamond all succeeded");
  check(log.runs.size() == 4, "4 nodes ran");
  // left and right are independent: they should overlap (concurrent peak >= 2
  // among the act tasks, since start already finished or is finishing).
  check(concurrent_peak.load() >= 2, "independent branches overlapped (peak>=2)");

  // join must start after both left and right finish.
  std::map<std::string, evo::ConcurrentNodeRun> by_id;
  for (const auto& r : log.runs) by_id[r.id.value] = r;
  check(by_id["left"].finished_at <= by_id["join"].started_at,
        "left finished before join started");
  check(by_id["right"].finished_at <= by_id["join"].started_at,
        "right finished before join started");
}

void test_wide_fan_overlap() {
  std::cout << "wide fan-out concurrency overlap\n";
  std::vector<evo::NodeSpec> nodes{trigger("start"), action("sink")};
  std::vector<evo::Edge> edges;
  for (int i = 0; i < 8; ++i) {
    std::string id = "w" + std::to_string(i);
    nodes.push_back(action(id));
    edges.push_back(edge("start", id));
    edges.push_back(edge(id, "sink"));
  }
  auto dag = evo::Dag::build(std::move(nodes), std::move(edges));
  check(dag.ok(), "dag builds");

  std::atomic<int> concurrent_peak{0};
  std::atomic<int> active{0};
  std::map<std::string, evo::TaskFn> tasks;
  tasks["start"] = noop_tasks()["start"];
  tasks["act"] = [&](const evo::NodeSpec&) -> evo::TaskResult {
    int now = active.fetch_add(1) + 1;
    concurrent_peak.store(std::max(concurrent_peak.load(), now));
    std::this_thread::sleep_for(std::chrono::milliseconds(15));
    active.fetch_sub(1);
    return evo::TaskResult{true, "done"};
  };

  evo::ConcurrentScheduler sched(std::move(*dag.dag), std::move(tasks),
                                 {.num_workers = 8});
  auto log = sched.run();
  check(log.all_ok(), "wide fan all succeeded");
  check(log.runs.size() == 10, "10 nodes ran");
  // With 8 independent branches and 8 workers, we expect heavy overlap.
  check(concurrent_peak.load() >= 4,
        "wide independent fan overlapped (peak>=4)");
}

void test_seeded_random_equiv() {
  std::cout << "seeded random DAG equivalence vs sequential\n";
  const std::uint64_t seeds[] = {1, 2, 3, 12345, 99999};

  for (std::uint64_t seed : seeds) {
    const auto gen = evo::bench::generate_workload(seed, 4, 4);
    check(gen.dag.execution_problems().empty(),
          "bench graph executable (seed " + std::to_string(seed) + ")");

    // Sequential reference
    std::map<std::string, evo::TaskFn> seq_tasks;
    seq_tasks["start"] = noop_tasks()["start"];
    seq_tasks["bench:sleep"] = evo::bench::sleep_task(gen.sleep_ms);
    seq_tasks["bench:burn"] = evo::bench::burn_task(gen.burn_iters);
    evo::Scheduler seq_sched(gen.dag, seq_tasks);
    auto seq_log = seq_sched.run();

    // Concurrent
    std::map<std::string, evo::TaskFn> con_tasks;
    con_tasks["start"] = noop_tasks()["start"];
    con_tasks["bench:sleep"] = evo::bench::sleep_task(gen.sleep_ms);
    con_tasks["bench:burn"] = evo::bench::burn_task(gen.burn_iters);
    evo::ConcurrentScheduler con_sched(gen.dag, con_tasks, {.num_workers = 4});
    auto con_log = con_sched.run();

    check(con_log.runs.size() == seq_log.runs.size(),
          "same node count (seed " + std::to_string(seed) + ")");
    check(con_log.all_ok() == seq_log.all_ok(),
          "same overall ok (seed " + std::to_string(seed) + ")");

    // Same set of succeeded nodes.
    auto seq_ok = std::set<std::string>{};
    for (const auto& r : seq_log.runs) {
      if (r.ok()) seq_ok.insert(r.id.value);
    }
    auto con_ok = succeeded_ids(con_log);
    check(seq_ok == con_ok,
          "same succeeded node set (seed " + std::to_string(seed) + ")");

    // Same per-node outputs (deterministic task results).
    std::map<std::string, std::string> seq_out, con_out;
    for (const auto& r : seq_log.runs) seq_out[r.id.value] = r.result.output;
    for (const auto& r : con_log.runs) con_out[r.id.value] = r.result.output;
    check(seq_out == con_out,
          "same per-node outputs (seed " + std::to_string(seed) + ")");
  }
}

void test_stress_random_dags() {
  std::cout << "stress: many random DAGs concurrency-correct\n";
  bool all_correct = true;
  for (int trial = 0; trial < 20; ++trial) {
    const std::uint64_t seed = 1000u + static_cast<std::uint64_t>(trial);
    const auto gen = evo::bench::generate_workload(seed, 3, 5);
    if (!gen.dag.execution_problems().empty()) {
      all_correct = false;
      break;
    }

    std::map<std::string, evo::TaskFn> tasks;
    tasks["start"] = noop_tasks()["start"];
    tasks["bench:sleep"] = evo::bench::sleep_task(gen.sleep_ms);
    tasks["bench:burn"] = evo::bench::burn_task(gen.burn_iters);

    evo::ConcurrentScheduler sched(gen.dag, tasks, {.num_workers = 6});
    auto log = sched.run();

    // Dependency invariant: for every edge (u -> v), u finished before v started.
    const auto* dag = &gen.dag;
    std::map<std::string, evo::ConcurrentNodeRun> by_id;
    for (const auto& r : log.runs) by_id[r.id.value] = r;

    for (const auto& edge : all_edges(*dag)) {
      auto it_u = by_id.find(edge.from.value);
      auto it_v = by_id.find(edge.to.value);
      if (it_u == by_id.end() || it_v == by_id.end()) continue;
      if (it_u->second.finished_at > it_v->second.started_at) {
        all_correct = false;
      }
    }
  }
  check(all_correct, "dependency invariant held across 20 random DAGs");
}

void test_no_double_execution() {
  std::cout << "no logical node executes twice\n";
  auto dag = evo::Dag::build(
      {trigger("start"), action("a"), action("b")},
      {edge("start", "a"), edge("a", "b")});
  check(dag.ok(), "dag builds");

  std::map<std::string, int> exec_count;
  std::map<std::string, evo::TaskFn> tasks;
  tasks["start"] = [&](const evo::NodeSpec&) -> evo::TaskResult {
    exec_count["start"]++;
    return evo::TaskResult{true, "started"};
  };
  tasks["act"] = [&](const evo::NodeSpec& spec) -> evo::TaskResult {
    exec_count[spec.id.value]++;
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
    return evo::TaskResult{true, "done"};
  };

  evo::ConcurrentScheduler sched(std::move(*dag.dag), std::move(tasks),
                                 {.num_workers = 4});
  auto log = sched.run();

  check(exec_count["start"] == 1, "start executed exactly once");
  check(exec_count["a"] == 1, "a executed exactly once");
  check(exec_count["b"] == 1, "b executed exactly once");
  check(log.runs.size() == 3, "3 run records (no duplicates)");
}

void test_failure_propagation() {
  std::cout << "concurrent failure propagates cancellation\n";
  auto dag = evo::Dag::build(
      {trigger("start"), action("a"), action("b"), action("c")},
      {edge("start", "a"), edge("a", "b"), edge("a", "c")});
  check(dag.ok(), "dag builds");

  std::map<std::string, evo::TaskFn> tasks;
  tasks["start"] = noop_tasks()["start"];
  tasks["act"] = [&](const evo::NodeSpec& spec) -> evo::TaskResult {
    if (spec.id.value == "a") {
      return evo::TaskResult{false, "a failed deliberately"};
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
    return evo::TaskResult{true, "done"};
  };

  evo::ConcurrentScheduler sched(std::move(*dag.dag), std::move(tasks),
                                 {.num_workers = 4});
  auto log = sched.run();

  std::map<std::string, evo::ConcurrentNodeRun> by_id;
  for (const auto& r : log.runs) by_id[r.id.value] = r;

  check(by_id["a"].result.completed == false, "a reported failure");
  // b and c depend on a; they must never have run (canceled, not executed).
  check(by_id.find("b") == by_id.end() || !by_id["b"].result.completed,
        "b not succeeded (canceled by upstream failure)");
  check(by_id.find("c") == by_id.end() || !by_id["c"].result.completed,
        "c not succeeded (canceled by upstream failure)");
  check(!log.all_ok(), "run reported not-all-ok");
}

}  // namespace

// ===========================================================================
// Milestone 11 — cooperative cancellation and graceful shutdown
// ===========================================================================

namespace {

void test_cancel_before_start() {
  std::cout << "cancel before run() starts\n";
  auto dag = evo::Dag::build(
      {trigger("start"), action("a"), action("b")},
      {edge("start", "a"), edge("a", "b")});
  check(dag.ok(), "dag builds");

  std::map<std::string, evo::TaskFn> tasks = noop_tasks();
  evo::ConcurrentScheduler sched(std::move(*dag.dag), std::move(tasks),
                                 {.num_workers = 4});
  sched.cancel();  // requested before run()

  auto log = sched.run();

  check(sched.is_canceled(), "scheduler reports canceled");
  check(log.runs.empty(), "no nodes ran (canceled before start)");
  check(sched.cancel_requested_at() !=
            std::chrono::steady_clock::time_point{},
        "cancel timestamp recorded");
}

void test_cancel_during_many_ready() {
  std::cout << "cancel during many ready tasks (clean shutdown)\n";
  // start -> 8 independent w-nodes -> sink. Cancel early. The 8 w-tasks are
  // dispatched and abort cooperatively; the sink (dependent on all 8) never
  // becomes ready and therefore never runs.
  std::vector<evo::NodeSpec> nodes{trigger("start"), action("sink")};
  std::vector<evo::Edge> edges;
  for (int i = 0; i < 8; ++i) {
    std::string id = "w" + std::to_string(i);
    nodes.push_back(action(id));
    edges.push_back(edge("start", id));
    edges.push_back(edge(id, "sink"));
  }
  auto dag = evo::Dag::build(std::move(nodes), std::move(edges));
  check(dag.ok(), "dag builds");

  std::atomic<int> started{0};
  std::map<std::string, evo::ConcurrentTaskFn> tasks;
  tasks["start"] = [](const evo::NodeSpec&, std::stop_token) {
    return evo::TaskResult{true, "started"};
  };
  tasks["act"] = [&](const evo::NodeSpec&, std::stop_token st) -> evo::TaskResult {
    started.fetch_add(1);
    // Long cooperative task (500ms) so cancellation interrupts it.
    int remaining = 500;
    while (remaining > 0 && !st.stop_requested()) {
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
      remaining -= 10;
    }
    if (st.stop_requested()) return evo::TaskResult{false, "canceled"};
    return evo::TaskResult{true, "done"};
  };

  evo::ConcurrentScheduler sched(std::move(*dag.dag), std::move(tasks),
                                 {.num_workers = 4});

  // Run in background so we can cancel mid-execution.
  auto fut = std::async(std::launch::async, [&]() { return sched.run(); });
  std::this_thread::sleep_for(std::chrono::milliseconds(50));
  sched.cancel();
  auto log = fut.get();  // must return (no hang) — pool drains cleanly

  check(sched.is_canceled(), "scheduler reports canceled");
  check(sched.cancel_requested_at() !=
            std::chrono::steady_clock::time_point{},
        "cancel timestamp recorded");
  // The sink depends on all 8 w-nodes succeeding; they are canceled, so the
  // sink stays blocked and is never dispatched.
  std::map<std::string, evo::ConcurrentNodeRun> by_id;
  for (const auto& r : log.runs) by_id[r.id.value] = r;
  check(by_id.find("sink") == by_id.end(),
        "sink never ran (blocked on canceled dependencies)");
}

void test_cancel_during_blocked_deps() {
  std::cout << "cancel while dependencies are blocked\n";
  auto dag = evo::Dag::build(
      {trigger("start"), action("a"), action("b"), action("c")},
      {edge("start", "a"), edge("a", "b"), edge("b", "c")});
  check(dag.ok(), "dag builds");

  std::map<std::string, evo::ConcurrentTaskFn> tasks;
  tasks["start"] = [](const evo::NodeSpec&, std::stop_token) {
    return evo::TaskResult{true, "started"};
  };
  tasks["act"] = [](const evo::NodeSpec& spec, std::stop_token st) -> evo::TaskResult {
    if (spec.id.value == "a") {
      // Long cooperative task on the only ready branch.
      int remaining = 500;
      while (remaining > 0 && !st.stop_requested()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        remaining -= 10;
      }
      if (st.stop_requested()) return evo::TaskResult{false, "a canceled"};
    }
    return evo::TaskResult{true, "done"};
  };

  evo::ConcurrentScheduler sched(std::move(*dag.dag), std::move(tasks),
                                 {.num_workers = 2});
  auto fut = std::async(std::launch::async, [&]() { return sched.run(); });
  std::this_thread::sleep_for(std::chrono::milliseconds(50));
  sched.cancel();
  auto log = fut.get();

  std::map<std::string, evo::ConcurrentNodeRun> by_id;
  for (const auto& r : log.runs) by_id[r.id.value] = r;

  // b and c were blocked on a; they must never have run to success.
  check(by_id.find("b") == by_id.end() || !by_id["b"].result.completed,
        "b not succeeded (was blocked on canceled a)");
  check(by_id.find("c") == by_id.end() || !by_id["c"].result.completed,
        "c not succeeded (was blocked on canceled a)");
  check(sched.is_canceled(), "scheduler is canceled");
}

void test_cancel_idempotent() {
  std::cout << "cancel is idempotent\n";
  auto dag = evo::Dag::build(
      {trigger("start"), action("a")},
      {edge("start", "a")});
  check(dag.ok(), "dag builds");

  std::map<std::string, evo::TaskFn> tasks = noop_tasks();
  evo::ConcurrentScheduler sched(std::move(*dag.dag), std::move(tasks),
                                 {.num_workers = 2});
  sched.cancel();
  sched.cancel();  // second call must be a no-op (no crash/double state)
  auto log = sched.run();
  check(sched.is_canceled(), "still canceled after repeated cancel");
  check(log.runs.empty(), "nothing ran");
}

void test_cooperative_abort_early() {
  std::cout << "cooperative task aborts before full duration\n";
  auto dag = evo::Dag::build(
      {trigger("start"), action("long")},
      {edge("start", "long")});
  check(dag.ok(), "dag builds");

  // Use the cooperative bench sleep (500ms) so we can detect early abort.
  std::map<evo::NodeId, int> ms;
  ms[evo::NodeId{"long"}] = 500;
  std::map<std::string, evo::ConcurrentTaskFn> tasks;
  tasks["start"] = [](const evo::NodeSpec&, std::stop_token) {
    return evo::TaskResult{true, "started"};
  };
  tasks["long"] = evo::bench::sleep_task_cooperative(ms);

  evo::ConcurrentScheduler sched(std::move(*dag.dag), std::move(tasks),
                                 {.num_workers = 2});
  auto fut = std::async(std::launch::async, [&]() { return sched.run(); });
  std::this_thread::sleep_for(std::chrono::milliseconds(50));
  sched.cancel();
  auto log = fut.get();

  std::map<std::string, evo::ConcurrentNodeRun> by_id;
  for (const auto& r : log.runs) by_id[r.id.value] = r;
  // The long task would take 500ms; cancellation at 50ms should make it abort
  // well before that. Its recorded duration must be < 500ms.
  if (auto it = by_id.find("long"); it != by_id.end()) {
    auto dur_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                      it->second.finished_at - it->second.started_at)
                      .count();
    check(dur_ms < 500, "cooperative task aborted early (<500ms), got " +
                            std::to_string(dur_ms) + "ms");
    check(!it->second.result.completed, "cooperative task reported canceled");
  } else {
    check(false, "long task should appear in log");
  }
}

}  // namespace

int main() {
  test_linear_equiv();
  test_diamond_equiv();
  test_wide_fan_overlap();
  test_seeded_random_equiv();
  test_stress_random_dags();
  test_no_double_execution();
  test_failure_propagation();

  // Milestone 11 — cancellation
  test_cancel_before_start();
  test_cancel_during_many_ready();
  test_cancel_during_blocked_deps();
  test_cancel_idempotent();
  test_cooperative_abort_early();

  if (failures != 0) {
    std::cout << failures << " concurrent-scheduler check(s) failed\n";
    return 1;
  }
  std::cout << "all concurrent-scheduler tests passed\n";
  return 0;
}
