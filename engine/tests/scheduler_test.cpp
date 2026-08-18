// Milestone 06 tests for the sequential reference scheduler.
// Covers: linear ordering, diamond dependency ordering, deterministic reruns,
// unregistered-type failure, reproducible seeded workload generation, and a
// wide fan-in/fan-out workload running to completion.

#include <algorithm>
#include <chrono>
#include <iostream>
#include <set>
#include <string>
#include <thread>
#include <vector>

#include "evo/bench.hpp"
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

std::vector<std::string> run_order(const evo::RunLog& log) {
  std::vector<std::string> out;
  for (const auto& r : log.runs) out.push_back(r.id.value);
  return out;
}

evo::TaskFn start_noop() {
  return [](const evo::NodeSpec&) {
    return evo::TaskResult{true, "started"};
  };
}

evo::TaskFn act_sleep(int ms) {
  return [ms](const evo::NodeSpec&) {
    std::this_thread::sleep_for(std::chrono::milliseconds(ms));
    return evo::TaskResult{true, "done"};
  };
}

void test_linear() {
  std::cout << "sequential linear chain\n";
  auto dag = evo::Dag::build(
      {trigger("start"), action("a"), action("b"), action("c")},
      {edge("start", "a"), edge("a", "b"), edge("b", "c")});
  check(dag.ok(), "dag builds");

  std::map<std::string, evo::TaskFn> tasks;
  tasks["start"] = start_noop();
  tasks["act"] = act_sleep(1);
  evo::Scheduler s(std::move(*dag.dag), std::move(tasks));
  const auto log = s.run();

  const auto order = run_order(log);
  check(order == std::vector<std::string>({"start", "a", "b", "c"}),
        "topological execution order");
  check(log.runs.size() == 4, "every node ran exactly once");
  check(log.all_ok(), "run completed cleanly");
  check(log.runs[0].sequence == 0 && log.runs[3].sequence == 3, "seq 0..3");
}

void test_diamond() {
  std::cout << "sequential diamond\n";
  auto dag = evo::Dag::build(
      {trigger("start"), action("left"), action("right"), action("join")},
      {edge("start", "left"), edge("start", "right"), edge("left", "join"),
       edge("right", "join")});
  check(dag.ok(), "dag builds");

  std::map<std::string, evo::TaskFn> tasks;
  tasks["start"] = start_noop();
  tasks["act"] = act_sleep(1);
  evo::Scheduler s(std::move(*dag.dag), std::move(tasks));
  const auto log = s.run();

  const auto order = run_order(log);
  check(order.front() == "start", "start first");
  check(order.back() == "join", "join last");
  check(order.size() == 4, "all four ran");
  check(std::find(order.begin(), order.end(), "left") <
            std::find(order.begin(), order.end(), "join"),
        "left before join");
  check(std::find(order.begin(), order.end(), "right") <
            std::find(order.begin(), order.end(), "join"),
        "right before join");
  check(log.all_ok(), "run completed cleanly");
}

void test_determinism() {
  std::cout << "deterministic ordering across reruns\n";
  auto dag = evo::Dag::build(
      {trigger("start"), action("a"), action("b"), action("c")},
      {edge("start", "a"), edge("a", "b"), edge("a", "c"), edge("b", "c")});
  check(dag.ok(), "dag builds");

  // Capture canonical JSON before moving the Dag into the first scheduler.
  const std::string canon = dag.dag->to_json_string();
  auto again = evo::Dag::from_json_string(canon);
  check(again.ok(), "rebuilt from canonical JSON");

  std::map<std::string, evo::TaskFn> tasks;
  tasks["start"] = start_noop();
  tasks["act"] = act_sleep(1);
  evo::Scheduler one(std::move(*dag.dag), std::move(tasks));

  std::map<std::string, evo::TaskFn> tasks2;
  tasks2["start"] = start_noop();
  tasks2["act"] = act_sleep(1);
  evo::Scheduler two(std::move(*again.dag), std::move(tasks2));

  const auto a = one.run();
  const auto b = two.run();
  check(run_order(a) == run_order(b), "identical execution order on rerun");
  check(a.to_json_string() == b.to_json_string(),
        "byte-identical run log serialization");
  std::cout << "  diag: run log bytes=" << a.to_json_string().size() << '\n';
}

void test_unregistered_fails_node() {
  std::cout << "unregistered task type fails the node\n";
  auto dag = evo::Dag::build({trigger("start"), action("a", "act"),
                              action("c", "bench:sleep")},
                             {edge("start", "a"), edge("a", "c")});
  check(dag.ok(), "dag builds");

  std::map<std::string, evo::TaskFn> tasks;
  tasks["start"] = start_noop();
  tasks["act"] = act_sleep(1);
  // "bench:sleep" intentionally NOT registered here.
  evo::Scheduler s(std::move(*dag.dag), std::move(tasks));
  const auto log = s.run();

  check(!log.all_ok(), "run stopped (failed)");
  check(log.runs.back().type == "bench:sleep",
        "failed node is the unregistered bench type");
  check(!log.runs.back().ok(), "failing node recorded as not ok");
  check(log.runs.size() == 3, "stop after the failing node (no later nodes)");
}

void test_reproducible_workload() {
  std::cout << "seeded workload reproducibility\n";
  const auto a = evo::bench::generate_workload(12345u, 4, 4);
  const auto b = evo::bench::generate_workload(12345u, 4, 4);

  check(a.dag.node_count() == b.dag.node_count(), "same node count");
  check(a.dag.edge_count() == b.dag.edge_count(), "same edge count");
  check(a.dag.to_json_string() == b.dag.to_json_string(),
        "byte-identical graph serialization");
  check(a.sleep_ms == b.sleep_ms, "identical sleep params");
  check(a.burn_iters == b.burn_iters, "identical burn params");

  const auto c = evo::bench::generate_workload(99999u, 4, 4);
  check(c.dag.to_json_string() != a.dag.to_json_string(),
        "different seed differs from a");
}

void test_bench_workload_runs() {
  std::cout << "bench workload completes sequentially\n";
  const auto gen = evo::bench::generate_workload(7u, 4, 4);
  check(gen.dag.execution_problems().empty(), "bench graph is executable");

  std::map<std::string, evo::TaskFn> tasks;
  tasks["start"] = start_noop();
  tasks["bench:sleep"] = evo::bench::sleep_task(gen.sleep_ms);
  tasks["bench:burn"] = evo::bench::burn_task(gen.burn_iters);
  evo::Scheduler s(gen.dag, std::move(tasks));
  const auto log = s.run();

  check(log.runs.size() == gen.dag.node_count(),
        "every node ran exactly once");
  check(log.all_ok(), "all bench nodes succeeded");

  std::vector<std::string> topo;
  for (const auto& id : gen.dag.topo_order()) topo.push_back(id.value);
  check(run_order(log) == topo, "execution order == topo order");

  using namespace std::chrono;
  const auto total = duration_cast<milliseconds>(
      log.runs.back().finished_at - log.runs.front().started_at);
  std::cout << "  diag(scheduler): bench workload makespan=" << total.count()
            << "ms over " << log.runs.size() << " nodes (not a perf claim)\n";
}

}  // namespace

int main() {
  test_linear();
  test_diamond();
  test_determinism();
  test_unregistered_fails_node();
  test_reproducible_workload();
  test_bench_workload_runs();

  if (failures != 0) {
    std::cout << failures << " scheduler check(s) failed\n";
    return 1;
  }
  std::cout << "all scheduler tests passed\n";
  return 0;
}
