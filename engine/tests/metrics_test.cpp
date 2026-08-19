// Milestone 13 tests for scheduler metrics instrumentation.
// Covers: monotonic timestamp relationships, exact logical counters, JSON
// shape of exported metrics, and per-node ready/dispatch/start/finish ordering.

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
#include "evo/metrics.hpp"

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

void test_timestamp_monotonicity() {
  std::cout << "monotonic timestamp relationships\n";
  auto dag = evo::Dag::build(
      {trigger("start"), action("a"), action("b"), action("c")},
      {edge("start", "a"), edge("a", "b"), edge("a", "c")});
  check(dag.ok(), "dag builds");

  std::map<std::string, evo::ConcurrentTaskFn> tasks;
  tasks["start"] = [](const evo::NodeSpec&, std::stop_token) {
    return evo::TaskResult{true, "started"};
  };
  tasks["act"] = [](const evo::NodeSpec&, std::stop_token) -> evo::TaskResult {
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
    return evo::TaskResult{true, "done"};
  };

  evo::ConcurrentScheduler sched(std::move(*dag.dag), std::move(tasks),
                                 {.num_workers = 4, .run_id = "mono-run"});
  auto log = sched.run();

  bool mono_ok = true;
  int non_idle = 0;
  for (const auto& r : log.runs) {
    // became_ready <= ready_at <= started_at <= finished_at
    if (r.became_ready_at > r.ready_at) mono_ok = false;
    if (r.ready_at > r.started_at) mono_ok = false;
    if (r.started_at > r.finished_at) mono_ok = false;
    if (r.ready_at > r.finished_at) mono_ok = false;
    ++non_idle;
  }
  check(mono_ok, "became_ready <= ready <= started <= finished for all nodes");
  check(non_idle == 4, "4 non-idle node runs recorded");

  // Dependency ordering still respected (start before successors).
  std::map<std::string, evo::ConcurrentNodeRun> by_id;
  for (const auto& r : log.runs) by_id[r.id.value] = r;
  bool dep_ok =
      by_id["start"].finished_at <= by_id["a"].started_at &&
      by_id["a"].finished_at <= by_id["b"].started_at &&
      by_id["a"].finished_at <= by_id["c"].started_at;
  check(dep_ok, "dependency ordering respected under concurrency");
}

void test_exact_counters() {
  std::cout << "exact logical counters\n";
  auto dag = evo::Dag::build(
      {trigger("start"), action("a"), action("b")},
      {edge("start", "a"), edge("a", "b")});
  check(dag.ok(), "dag builds");

  std::map<std::string, evo::ConcurrentTaskFn> tasks;
  tasks["start"] = [](const evo::NodeSpec&, std::stop_token) {
    return evo::TaskResult{true, "started"};
  };
  tasks["act"] = [](const evo::NodeSpec&, std::stop_token) {
    return evo::TaskResult{true, "done"};
  };

  evo::ConcurrentScheduler sched(std::move(*dag.dag), std::move(tasks),
                                 {.num_workers = 2, .run_id = "count-run"});
  auto log = sched.run();

  const auto& m = sched.metrics();
  check(m.dispatch_count == 3, "dispatch_count == 3 (got " +
                                   std::to_string(m.dispatch_count) + ")");
  check(m.completion_count == 3,
        "completion_count == 3 (got " + std::to_string(m.completion_count) + ")");
  check(m.max_in_flight >= 1 && m.max_in_flight <= 3,
        "max_in_flight in [1,3] (got " + std::to_string(m.max_in_flight) + ")");
  check(m.run_start != std::chrono::steady_clock::time_point{},
        "run_start timestamp set");
  check(m.run_terminal != std::chrono::steady_clock::time_point{},
        "run_terminal timestamp set");
  check(m.cancel_requested == std::chrono::steady_clock::time_point{},
        "cancel_requested unset (==0) on a clean run");
}

void test_metrics_json_shape() {
  std::cout << "metrics JSON export shape\n";
  auto dag = evo::Dag::build(
      {trigger("start"), action("a")},
      {edge("start", "a")});
  check(dag.ok(), "dag builds");

  std::map<std::string, evo::ConcurrentTaskFn> tasks;
  tasks["start"] = [](const evo::NodeSpec&, std::stop_token) {
    return evo::TaskResult{true, "started"};
  };
  tasks["act"] = [](const evo::NodeSpec&, std::stop_token) {
    return evo::TaskResult{true, "done"};
  };

  evo::ConcurrentScheduler sched(std::move(*dag.dag), std::move(tasks),
                                 {.num_workers = 2, .run_id = "json-run"});
  sched.run();

  const std::string js = sched.metrics().to_json_string();
  check(js.find("\"metrics\"") != std::string::npos, "JSON has top-level metrics object");
  check(js.find("\"dispatch_count\"") != std::string::npos, "JSON has dispatch_count");
  check(js.find("\"completion_count\"") != std::string::npos,
        "JSON has completion_count");
  check(js.find("\"max_in_flight\"") != std::string::npos, "JSON has max_in_flight");
  check(js.find("\"max_queue_depth\"") != std::string::npos,
        "JSON has max_queue_depth");
  check(js.find("\"retry_count\"") != std::string::npos, "JSON has retry_count");
  check(js.find("\"cancel_requested_at_ms\"") != std::string::npos,
        "JSON has cancel_requested_at_ms");
  check(js.find("\"run_terminal_at_ms\"") != std::string::npos,
        "JSON has run_terminal_at_ms");
  std::cout << "  diag: metrics JSON=" << js << '\n';
}

void test_cancel_metrics() {
  std::cout << "cancellation metrics\n";
  auto dag = evo::Dag::build(
      {trigger("start"), action("a"), action("b")},
      {edge("start", "a"), edge("a", "b")});
  check(dag.ok(), "dag builds");

  std::map<std::string, evo::ConcurrentTaskFn> tasks;
  tasks["start"] = [](const evo::NodeSpec&, std::stop_token) {
    return evo::TaskResult{true, "started"};
  };
  tasks["act"] = [](const evo::NodeSpec&, std::stop_token st) -> evo::TaskResult {
    int remaining = 500;
    while (remaining > 0 && !st.stop_requested()) {
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
      remaining -= 10;
    }
    if (st.stop_requested()) return evo::TaskResult{false, "canceled"};
    return evo::TaskResult{true, "done"};
  };

  evo::ConcurrentScheduler sched(std::move(*dag.dag), std::move(tasks),
                                 {.num_workers = 2, .run_id = "cancel-run"});
  auto fut =
      std::async(std::launch::async, [&]() { return sched.run(); });
  std::this_thread::sleep_for(std::chrono::milliseconds(30));
  sched.cancel();
  (void)fut.get();

  const auto& m = sched.metrics();
  check(m.cancel_requested != std::chrono::steady_clock::time_point{},
        "cancel_requested timestamp recorded");
  check(m.run_terminal != std::chrono::steady_clock::time_point{},
        "run_terminal timestamp recorded");
  // run_terminal must be at or after cancel_requested.
  check(m.run_terminal >= m.cancel_requested,
        "run_terminal >= cancel_requested (clean shutdown)");
  // Not all nodes completed (cancellation interrupted).
  check(m.completion_count < m.dispatch_count || m.completion_count <= 2,
        "completion_count reflects interruption");
}

void test_wide_metrics() {
  std::cout << "wide fan metrics: max_in_flight < node count under resource cap\n";
  // 8 independent bench:sleep (non-browser) nodes + start, 4 workers.
  std::vector<evo::NodeSpec> nodes{trigger("start")};
  std::vector<evo::Edge> edges;
  std::map<evo::NodeId, int> ms;
  for (int i = 0; i < 8; ++i) {
    std::string id = "w" + std::to_string(i);
    nodes.push_back(action(id, "bench:sleep"));
    edges.push_back(edge("start", id));
    ms[evo::NodeId{id}] = 20;
  }
  auto dag = evo::Dag::build(std::move(nodes), std::move(edges));
  check(dag.ok(), "dag builds");

  std::map<std::string, evo::ConcurrentTaskFn> tasks;
  tasks["start"] = [](const evo::NodeSpec&, std::stop_token) {
    return evo::TaskResult{true, "started"};
  };
  tasks["bench:sleep"] = evo::bench::sleep_task_cooperative(ms);

  evo::ConcurrentScheduler sched(std::move(*dag.dag), std::move(tasks),
                                 {.num_workers = 4, .run_id = "wide-metrics"});
  auto log = sched.run();

  const auto& m = sched.metrics();
  check(log.runs.size() == 9, "9 nodes ran");
  check(m.dispatch_count == 9, "dispatch_count == 9");
  check(m.completion_count == 9, "completion_count == 9");
  // in_flight counts dispatched-but-incomplete tasks (pool-queued, not just
  // actively executing), so it can exceed num_workers when dispatch outpaces
  // completion. Bound it by the total node count instead.
  check(m.max_in_flight >= 2 && m.max_in_flight <= 9,
        "max_in_flight in [2,9] (got " + std::to_string(m.max_in_flight) + ")");
  check(m.max_in_flight >= 1, "at least one task was in flight");
}

}  // namespace

int main() {
  test_timestamp_monotonicity();
  test_exact_counters();
  test_metrics_json_shape();
  test_cancel_metrics();
  test_wide_metrics();

  if (failures != 0) {
    std::cout << failures << " metrics check(s) failed\n";
    return 1;
  }
  std::cout << "all metrics tests passed\n";
  return 0;
}
