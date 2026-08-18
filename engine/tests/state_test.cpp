// Milestone 07 tests for the scheduler state machine and dependency counters.
// Covers: initial state, dependency counters, fan-in, idempotent completion,
// failure propagation, illegal transitions, and cancel_run.

#include <algorithm>
#include <iostream>
#include <string>
#include <vector>

#include "evo/dag.hpp"
#include "evo/scheduler.hpp"
#include "evo/state_machine.hpp"

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

using evo::NodeId;
using evo::NodeState;
using evo::RunState;

NodeId n(const std::string& s) { return NodeId{s}; }

// Run a node through all states: dispatch → start → complete(success).
void execute_success(evo::SchedulerState& st, const NodeId& id) {
  st.dispatch_node(id);
  st.start_node(id);
  st.complete_node(id, evo::TaskResult{true, "ok"});
}

void test_initial_state() {
  std::cout << "initial state after start_run\n";
  auto dag = evo::Dag::build(
      {trigger("start"), action("a"), action("b"), action("c")},
      {edge("start", "a"), edge("a", "b"), edge("b", "c")});
  check(dag.ok(), "dag builds");

  evo::SchedulerState st(std::move(*dag.dag));
  check(st.run_state() == RunState::Queued, "run starts QUEUED");
  check(st.node_state(n("start")) == NodeState::Blocked, "all blocked before start");

  st.start_run();
  check(st.run_state() == RunState::Running, "run transitions to Running");
  check(st.node_state(n("start")) == NodeState::Ready, "trigger is READY (0 deps)");
  check(st.node_state(n("a")) == NodeState::Blocked, "a is BLOCKED (1 dep)");
  check(st.node_state(n("b")) == NodeState::Blocked, "b is BLOCKED (1 dep)");
  check(st.node_state(n("c")) == NodeState::Blocked, "c is BLOCKED (1 dep)");
  check(st.remaining_deps(n("a")) == 1, "a has 1 remaining dep");
  check(st.remaining_deps(n("b")) == 1, "b has 1 remaining dep");
  check(st.remaining_deps(n("c")) == 1, "c has 1 remaining dep");
  check(st.remaining_deps(n("start")) == 0, "start has 0 remaining deps");

  auto ready = st.ready_nodes();
  check(ready.size() == 1 && ready[0].value == "start", "only start is ready");
}

void test_diamond_dependency() {
  std::cout << "diamond fan-in: join waits for both predecessors\n";
  auto dag = evo::Dag::build(
      {trigger("start"), action("left"), action("right"), action("join")},
      {edge("start", "left"), edge("start", "right"),
       edge("left", "join"), edge("right", "join")});
  check(dag.ok(), "dag builds");

  evo::SchedulerState st(std::move(*dag.dag));
  st.start_run();

  // Complete start and capture newly-ready nodes
  st.dispatch_node(n("start"));
  st.start_node(n("start"));
  auto newly = st.complete_node(n("start"), evo::TaskResult{true, "ok"});
  check(newly.size() == 2, "both left and right become ready after start completes");

  // Complete left only; join should still be BLOCKED (needs right too)
  execute_success(st, n("left"));
  check(st.node_state(n("join")) == NodeState::Blocked,
        "join still BLOCKED after only left completes");
  check(st.remaining_deps(n("join")) == 1, "join has 1 dep remaining");

  // Complete right; join should become READY
  execute_success(st, n("right"));
  check(st.node_state(n("join")) == NodeState::Ready,
        "join becomes READY after both predecessors complete");
  check(st.remaining_deps(n("join")) == 0, "join has 0 deps remaining");
}

void test_idempotent_completion() {
  std::cout << "duplicate completion is idempotent\n";
  auto dag = evo::Dag::build(
      {trigger("start"), action("a"), action("b")},
      {edge("start", "a"), edge("a", "b")});
  check(dag.ok(), "dag builds");

  evo::SchedulerState st(std::move(*dag.dag));
  st.start_run();
  execute_success(st, n("start"));
  // Complete 'a' for the first time — should unlock 'b'
  st.dispatch_node(n("a"));
  st.start_node(n("a"));
  auto first = st.complete_node(n("a"), evo::TaskResult{true, "ok"});
  check(first.size() == 1 && first[0].value == "b",
        "first completion unlocks b");
  check(st.node_state(n("b")) == NodeState::Ready, "b is READY");
  check(st.remaining_deps(n("b")) == 0, "b has 0 deps");

  // Dispatch and start b so it's in a state to test duplicate completion
  st.dispatch_node(n("b"));
  st.start_node(n("b"));
  // Duplicate completion of 'a' — must be a no-op (dep counter not decremented
  // again, no new ready nodes).
  auto dup = st.complete_node(n("a"), evo::TaskResult{true, "ok"});
  check(dup.empty(), "duplicate completion is no-op (empty result)");
  check(st.remaining_deps(n("b")) == 0, "b dep count not decremented again");
  // Complete b to finish the run
  st.complete_node(n("b"), evo::TaskResult{true, "ok2"});
  check(st.all_nodes_terminal(), "all nodes terminal after completion");
}

void test_failure_propagation() {
  std::cout << "failure propagates cancellation transitively\n";
  auto dag = evo::Dag::build(
      {trigger("start"), action("a"), action("b"), action("c"), action("d")},
      {edge("start", "a"), edge("a", "b"), edge("b", "c"),
       edge("a", "d"), edge("d", "c")});
  check(dag.ok(), "dag builds");

  evo::SchedulerState st(std::move(*dag.dag));
  st.start_run();
  execute_success(st, n("start"));

  // Fail node 'a'; b and d should be canceled, and c (depends on b and d) too
  auto canceled = st.fail_node(n("a"), "boom");
  check(st.node_state(n("a")) == NodeState::Failed, "a is FAILED");
  check(std::find(canceled.begin(), canceled.end(), n("b")) != canceled.end(),
        "b canceled transitively");
  check(std::find(canceled.begin(), canceled.end(), n("d")) != canceled.end(),
        "d canceled transitively");
  check(std::find(canceled.begin(), canceled.end(), n("c")) != canceled.end(),
        "c canceled transitively (fan-in of canceled nodes)");
  check(st.all_nodes_terminal(), "all nodes terminal after failure propagation");
}

void test_failure_does_not_clobber_success() {
  std::cout << "late failure does not overwrite succeeded node\n";
  auto dag = evo::Dag::build(
      {trigger("start"), action("a")},
      {edge("start", "a")});
  check(dag.ok(), "dag builds");

  evo::SchedulerState st(std::move(*dag.dag));
  st.start_run();
  execute_success(st, n("start"));
  execute_success(st, n("a"));
  check(st.node_state(n("a")) == NodeState::Succeeded, "a succeeded");

  // Late failure message for already-completed node is a no-op
  auto canceled = st.fail_node(n("a"), "late failure");
  check(canceled.empty(), "late failure returns no cancellations");
  check(st.node_state(n("a")) == NodeState::Succeeded,
        "a still SUCCEEDED (not clobbered)");
}

void test_illegal_transitions() {
  std::cout << "illegal state transitions are rejected\n";
  auto dag = evo::Dag::build(
      {trigger("start"), action("a")},
      {edge("start", "a")});
  check(dag.ok(), "dag builds");

  evo::SchedulerState st(std::move(*dag.dag));

  // start_run when not QUEUED is no-op
  st.start_run();
  st.start_run();  // second start should be no-op
  check(st.run_state() == RunState::Running, "double start is no-op");

  // dispatch_node on a non-READY node is no-op
  st.dispatch_node(n("a"));  // a is BLOCKED, not READY
  check(st.node_state(n("a")) == NodeState::Blocked, "dispatch of BLOCKED is no-op");

  // start_node on a non-DISPATCHED node is no-op
  st.start_node(n("a"));  // a is BLOCKED, not DISPATCHED
  check(st.node_state(n("a")) == NodeState::Blocked, "start of BLOCKED is no-op");

  execute_success(st, n("start"));
  // complete_node on a non-RUNNING node is no-op
  auto result = st.complete_node(n("start"), evo::TaskResult{true, "ok"});
  check(result.empty(), "complete of non-Running is no-op");
}

void test_cancel_run() {
  std::cout << "cancel_run cancels all non-terminal nodes\n";
  auto dag = evo::Dag::build(
      {trigger("start"), action("a"), action("b")},
      {edge("start", "a"), edge("start", "b")});
  check(dag.ok(), "dag builds");

  evo::SchedulerState st(std::move(*dag.dag));
  st.start_run();
  execute_success(st, n("start"));

  auto canceled = st.cancel_run();
  check(st.run_state() == RunState::Canceled, "run is CANCELED");
  check(st.node_state(n("start")) == NodeState::Succeeded,
        "start stays SUCCEEDED");
  check(st.node_state(n("a")) == NodeState::Canceled, "a is CANCELED");
  check(st.node_state(n("b")) == NodeState::Canceled, "b is CANCELED");
  check(std::find(canceled.begin(), canceled.end(), n("a")) != canceled.end(),
        "a in canceled list");
  check(std::find(canceled.begin(), canceled.end(), n("b")) != canceled.end(),
        "b in canceled list");
  check(std::find(canceled.begin(), canceled.end(), n("start")) == canceled.end(),
        "start not in canceled list (already terminal)");
}

void test_wide_fan() {
  std::cout << "wide fan-out/fan-in state tracking\n";
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

  evo::SchedulerState st(std::move(*dag.dag));
  st.start_run();
  check(st.remaining_deps(n("sink")) == 8, "sink has 8 deps");
  check(st.remaining_deps(n("start")) == 0, "start has 0 deps");

  execute_success(st, n("start"));
  // After start completes, all 8 w-nodes become READY
  int ready_count = 0;
  for (int i = 0; i < 8; ++i) {
    std::string id = "w" + std::to_string(i);
    check(st.node_state(n(id)) == NodeState::Ready, id + " is READY");
    ++ready_count;
  }
  check(ready_count == 8, "8 nodes became ready");

  // Complete 7 of 8; sink should still be BLOCKED
  for (int i = 0; i < 7; ++i) {
    std::string id = "w" + std::to_string(i);
    execute_success(st, n(id));
  }
  check(st.node_state(n("sink")) == NodeState::Blocked,
        "sink BLOCKED after 7/8 predecessors");
  check(st.remaining_deps(n("sink")) == 1, "sink has 1 dep remaining");

  // Complete last one
  execute_success(st, n("w7"));
  check(st.node_state(n("sink")) == NodeState::Ready,
        "sink READY after all 8 predecessors");
  check(st.remaining_deps(n("sink")) == 0, "sink 0 deps");
  check(st.all_nodes_terminal() == false, "not all terminal (sink not run yet)");

  execute_success(st, n("sink"));
  check(st.all_nodes_terminal(), "all terminal after sink completes");
}

void test_complete_run_all_success() {
  std::cout << "complete run reaches all-succeeded\n";
  auto dag = evo::Dag::build(
      {trigger("start"), action("a"), action("b"), action("c")},
      {edge("start", "a"), edge("a", "b"), edge("a", "c")});
  check(dag.ok(), "dag builds");

  evo::SchedulerState st(std::move(*dag.dag));
  st.start_run();

  for (const auto& id : std::vector<std::string>{"start", "a", "b", "c"}) {
    execute_success(st, n(id));
  }

  check(st.node_state(n("start")) == NodeState::Succeeded, "start SUCCEEDED");
  check(st.node_state(n("a")) == NodeState::Succeeded, "a SUCCEEDED");
  check(st.node_state(n("b")) == NodeState::Succeeded, "b SUCCEEDED");
  check(st.node_state(n("c")) == NodeState::Succeeded, "c SUCCEEDED");
  check(st.all_nodes_terminal(), "all nodes terminal");
}

void test_failure_via_complete_node() {
  std::cout << "failure via complete_node propagates cancellation\n";
  auto dag = evo::Dag::build(
      {trigger("start"), action("a"), action("b")},
      {edge("start", "a"), edge("a", "b")});
  check(dag.ok(), "dag builds");

  evo::SchedulerState st(std::move(*dag.dag));
  st.start_run();
  execute_success(st, n("start"));

  // Dispatch and start 'a', then complete it with a failure
  st.dispatch_node(n("a"));
  st.start_node(n("a"));
  auto canceled = st.complete_node(n("a"), evo::TaskResult{false, "task crashed"});
  check(st.node_state(n("a")) == NodeState::Failed, "a is FAILED");
  check(std::find(canceled.begin(), canceled.end(), n("b")) != canceled.end(),
        "b canceled via complete_node failure path");
  check(st.failure_reason(n("a")) == "task crashed", "failure reason recorded");
}

}  // namespace

int main() {
  test_initial_state();
  test_diamond_dependency();
  test_idempotent_completion();
  test_failure_propagation();
  test_failure_does_not_clobber_success();
  test_illegal_transitions();
  test_cancel_run();
  test_wide_fan();
  test_complete_run_all_success();
  test_failure_via_complete_node();

  if (failures != 0) {
    std::cout << failures << " state-machine check(s) failed\n";
    return 1;
  }
  std::cout << "all state-machine tests passed\n";
  return 0;
}
