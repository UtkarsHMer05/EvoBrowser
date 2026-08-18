// Milestone 12 tests for execution resource classes and browser affinity.
// Covers: default policy classification, two ready browser nodes sharing an
// affinity key never overlap, independent affinity keys overlap when capacity
// permits, and non-browser work overlaps browser waiting.

#include <chrono>
#include <iostream>
#include <map>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <vector>

#include "evo/concurrent_scheduler.hpp"
#include "evo/dag.hpp"
#include "evo/execution_policy.hpp"

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

using Interval =
    std::pair<std::chrono::steady_clock::time_point,
              std::chrono::steady_clock::time_point>;

// Records [start,end) intervals keyed by node id so tests can assert overlap
// or serialization of resource-class work.
struct IntervalRecorder {
  std::mutex mu;
  std::map<std::string, Interval> by_id;
  void record(const std::string& id, Interval iv) {
    std::lock_guard lock(mu);
    by_id[id] = iv;
  }
  bool overlaps(const std::string& a, const std::string& b) const {
    auto it_a = by_id.find(a), it_b = by_id.find(b);
    if (it_a == by_id.end() || it_b == by_id.end()) return false;
    return it_a->second.first < it_b->second.second &&
           it_b->second.first < it_a->second.second;
  }
};

std::map<std::string, evo::ConcurrentTaskFn> browser_tasks(
    IntervalRecorder& rec, int ms) {
  std::map<std::string, evo::ConcurrentTaskFn> tasks;
  tasks["start"] = [](const evo::NodeSpec&, std::stop_token) {
    return evo::TaskResult{true, "started"};
  };
  tasks["open-url"] = [&rec, ms](const evo::NodeSpec& spec,
                                 std::stop_token) -> evo::TaskResult {
    auto s = std::chrono::steady_clock::now();
    std::this_thread::sleep_for(std::chrono::milliseconds(ms));
    auto e = std::chrono::steady_clock::now();
    rec.record(spec.id.value, {s, e});
    return evo::TaskResult{true, "opened " + spec.id.value};
  };
  tasks["send-email"] = [&rec, ms](const evo::NodeSpec& spec,
                                   std::stop_token) -> evo::TaskResult {
    auto s = std::chrono::steady_clock::now();
    std::this_thread::sleep_for(std::chrono::milliseconds(ms));
    auto e = std::chrono::steady_clock::now();
    rec.record(spec.id.value, {s, e});
    return evo::TaskResult{true, "emailed " + spec.id.value};
  };
  return tasks;
}

void test_policy_classification() {
  std::cout << "default execution policy classification\n";
  evo::ExecutionPolicy p{"run-X"};

  auto s = p.policy_for({evo::NodeId{"s"}, evo::NodeKind::Trigger, "start"});
  check(s.klass == evo::ResourceClass::Internal,
        "start classified Internal");

  auto o = p.policy_for({evo::NodeId{"o"}, evo::NodeKind::Action, "open-url"});
  check(o.klass == evo::ResourceClass::Browser, "open-url classified Browser");
  check(o.capacity == 1, "browser capacity is 1 (single session)");
  check(o.affinity_key == "run-X", "browser affinity keyed by run id");

  auto a = p.policy_for({evo::NodeId{"a"}, evo::NodeKind::Action, "act"});
  check(a.klass == evo::ResourceClass::Browser, "act classified Browser");

  auto e = p.policy_for({evo::NodeId{"e"}, evo::NodeKind::Action, "send-email"});
  check(e.klass == evo::ResourceClass::ExternalIo,
        "send-email classified ExternalIo");
  check(e.capacity == evo::ResourcePolicy::kUnbounded,
        "email capacity unbounded");
}

void test_browser_affinity_serializes() {
  std::cout << "two ready browser nodes sharing affinity never overlap\n";
  auto dag = evo::Dag::build(
      {trigger("start"), action("a", "open-url"), action("b", "open-url")},
      {edge("start", "a"), edge("start", "b")});
  check(dag.ok(), "dag builds");

  IntervalRecorder rec;
  auto tasks = browser_tasks(rec, 60);
  evo::ConcurrentScheduler sched(std::move(*dag.dag), std::move(tasks),
                                 {.num_workers = 4, .run_id = "run-A"});
  auto log = sched.run();

  check(log.all_ok(), "both browser nodes completed");
  check(!rec.overlaps("a", "b"),
        "browser a and b did NOT overlap (serialized on one session)");
}

void test_independent_affinity_concurrent() {
  std::cout << "independent affinity keys overlap when capacity permits\n";
  auto dag = evo::Dag::build(
      {trigger("start"), action("a", "open-url"), action("b", "open-url")},
      {edge("start", "a"), edge("start", "b")});
  check(dag.ok(), "dag builds");

  IntervalRecorder rec;
  auto tasks = browser_tasks(rec, 60);
  evo::ConcurrentScheduler sched(std::move(*dag.dag), std::move(tasks),
                                 {.num_workers = 4, .run_id = "run-B"});
  // Give a and b distinct affinity keys => separate capacity-1 resources.
  sched.policy().set_node_affinity(evo::NodeId{"a"}, "sess-1");
  sched.policy().set_node_affinity(evo::NodeId{"b"}, "sess-2");
  auto log = sched.run();

  check(log.all_ok(), "both nodes completed");
  check(rec.overlaps("a", "b"),
        "independent affinity keys DID overlap (parallel sessions)");
}

void test_nonbrowser_overlaps_browser() {
  std::cout << "non-browser work overlaps browser waiting\n";
  auto dag = evo::Dag::build(
      {trigger("start"), action("b", "open-url"), action("e", "send-email")},
      {edge("start", "b"), edge("start", "e")});
  check(dag.ok(), "dag builds");

  IntervalRecorder rec;
  // Browser task is long; email is short -> email should run while browser waits.
  auto tasks = browser_tasks(rec, 80);
  evo::ConcurrentScheduler sched(std::move(*dag.dag), std::move(tasks),
                                 {.num_workers = 4, .run_id = "run-C"});
  auto log = sched.run();

  check(log.all_ok(), "browser and email completed");
  check(rec.overlaps("b", "e"),
        "email overlapped browser work (non-browser concurrency)");
}

void test_wide_fan_browser_serial() {
  std::cout << "wide fan of browser nodes all serialize on one session\n";
  std::vector<evo::NodeSpec> nodes{trigger("start")};
  std::vector<evo::Edge> edges;
  for (int i = 0; i < 4; ++i) {
    std::string id = "b" + std::to_string(i);
    nodes.push_back(action(id, "open-url"));
    edges.push_back(edge("start", id));
  }
  auto dag = evo::Dag::build(std::move(nodes), std::move(edges));
  check(dag.ok(), "dag builds");

  IntervalRecorder rec;
  auto tasks = browser_tasks(rec, 40);
  evo::ConcurrentScheduler sched(std::move(*dag.dag), std::move(tasks),
                                 {.num_workers = 4, .run_id = "run-D"});
  auto log = sched.run();
  check(log.all_ok(), "all browser nodes completed");

  // No pair among b0..b3 may overlap (all share the run's capacity-1 session).
  bool any_overlap = false;
  for (int i = 0; i < 4; ++i) {
    for (int j = i + 1; j < 4; ++j) {
      if (rec.overlaps("b" + std::to_string(i), "b" + std::to_string(j))) {
        any_overlap = true;
      }
    }
  }
  check(!any_overlap, "no two browser nodes in the fan overlapped");
}

}  // namespace

int main() {
  test_policy_classification();
  test_browser_affinity_serializes();
  test_independent_affinity_concurrent();
  test_nonbrowser_overlaps_browser();
  test_wide_fan_browser_serial();

  if (failures != 0) {
    std::cout << failures << " affinity-scheduler check(s) failed\n";
    return 1;
  }
  std::cout << "all affinity-scheduler tests passed\n";
  return 0;
}
