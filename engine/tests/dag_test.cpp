// Milestone 05 unit tests for the canonical DAG model.
// Covers: linear, diamond, wide fan-out/fan-in, disconnected, duplicate id,
// missing edge endpoint, self-loop, duplicate edge, cyclic graphs, canonical
// JSON round-trip, malformed payloads, deterministic topo order, reachability,
// and the scheduler-contract execution checks.

#include <iostream>
#include <set>
#include <string>
#include <vector>

#include "evo/dag.hpp"

namespace {

int failures = 0;

void check(bool ok, const std::string& what) {
  std::cout << (ok ? "  ok   " : "  FAIL ") << what << '\n';
  if (!ok) ++failures;
}

bool has_code(const std::vector<evo::GraphError>& errors,
              evo::GraphError::Code code) {
  for (const auto& e : errors) {
    if (e.code == code) return true;
  }
  return false;
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

std::vector<std::string> topo_strings(const evo::Dag& dag) {
  std::vector<std::string> out;
  for (const auto& id : dag.topo_order()) out.push_back(id.value);
  return out;
}

void test_linear() {
  std::cout << "linear chain\n";
  auto result = evo::Dag::build(
      {trigger("start"), action("a"), action("b"), action("c")},
      {edge("start", "a"), edge("a", "b"), edge("b", "c")});
  check(result.ok(), "builds");
  check(result.dag->node_count() == 4 && result.dag->edge_count() == 3,
        "counts");
  const auto order = topo_strings(*result.dag);
  check(order == std::vector<std::string>({"start", "a", "b", "c"}),
        "deterministic topo order");
  check(result.dag->execution_problems().empty(), "executable");
  check(result.dag->predecessors(evo::NodeId{"b"}).size() == 1, "preds(b)");
  check(result.dag->successors(evo::NodeId{"b"}).size() == 1, "succs(b)");
}

void test_diamond() {
  std::cout << "diamond (fan-out then fan-in)\n";
  auto result = evo::Dag::build(
      {trigger("start"), action("left"), action("right"), action("join")},
      {edge("start", "left"), edge("start", "right"), edge("left", "join"),
       edge("right", "join")});
  check(result.ok(), "builds");
  check(result.dag->predecessors(evo::NodeId{"join"}).size() == 2,
        "join has two predecessors (fan-in invariant input)");
  const auto order = topo_strings(*result.dag);
  check(order.front() == "start" && order.back() == "join",
        "start first, join last");
  // Lexicographic tie-break: left before right.
  check(order[1] == "left" && order[2] == "right", "deterministic tie-break");
  check(result.dag->execution_problems().empty(), "executable");
}

void test_wide_fan() {
  std::cout << "wide fan-out/fan-in (1 -> 32 -> 1)\n";
  std::vector<evo::NodeSpec> nodes{trigger("start"), action("sink")};
  std::vector<evo::Edge> edges;
  for (int i = 0; i < 32; ++i) {
    const std::string id =
        std::string("w") + (i < 10 ? "0" : "") + std::to_string(i);  // w00..w31 lex order
    nodes.push_back(action(id));
    edges.push_back(edge("start", id));
    edges.push_back(edge(id, "sink"));
  }
  auto result = evo::Dag::build(std::move(nodes), std::move(edges));
  check(result.ok(), "builds");
  check(result.dag->node_count() == 34, "34 nodes");
  check(result.dag->predecessors(evo::NodeId{"sink"}).size() == 32,
        "sink has 32 predecessors");
  check(result.dag->successors(evo::NodeId{"start"}).size() == 32,
        "start has 32 successors");
  const auto order = topo_strings(*result.dag);
  check(order.front() == "start" && order.back() == "sink", "bounds");
  check(result.dag->execution_problems().empty(), "executable");
}

void test_disconnected() {
  std::cout << "disconnected node\n";
  auto result =
      evo::Dag::build({trigger("start"), action("a"), action("orphan")},
                      {edge("start", "a")});
  check(result.ok(), "structure builds (disconnection is not structural)");
  const auto problems = result.dag->execution_problems();
  check(problems.size() == 1, "one execution problem");
  check(!problems.empty() &&
            problems[0].find("orphan") != std::string::npos,
        "names the unreachable node");
  check(!result.dag->is_reachable(evo::NodeId{"start"}, evo::NodeId{"orphan"}),
        "orphan not reachable");
}

void test_duplicate_id() {
  std::cout << "duplicate node id\n";
  auto result = evo::Dag::build({trigger("start"), action("a"), action("a")},
                                {edge("start", "a")});
  check(!result.ok(), "rejected");
  check(has_code(result.errors, evo::GraphError::Code::DuplicateNodeId),
        "duplicate_node_id reported");
}

void test_empty_id() {
  std::cout << "empty node id\n";
  auto result = evo::Dag::build({trigger("start"), action("")}, {});
  check(!result.ok(), "rejected");
  check(has_code(result.errors, evo::GraphError::Code::EmptyNodeId),
        "empty_node_id reported");
}

void test_missing_endpoint() {
  std::cout << "edge with missing endpoint\n";
  auto result = evo::Dag::build({trigger("start"), action("a")},
                                {edge("start", "ghost")});
  check(!result.ok(), "rejected");
  check(has_code(result.errors, evo::GraphError::Code::MissingEdgeEndpoint),
        "missing_edge_endpoint reported");
}

void test_self_loop_and_duplicate_edge() {
  std::cout << "self-loop and duplicate edge\n";
  auto loop = evo::Dag::build({trigger("start"), action("a")},
                              {edge("start", "a"), edge("a", "a")});
  check(!loop.ok() &&
            has_code(loop.errors, evo::GraphError::Code::SelfLoop),
        "self-loop rejected");
  auto dup = evo::Dag::build({trigger("start"), action("a")},
                             {edge("start", "a"), edge("start", "a")});
  check(!dup.ok() &&
            has_code(dup.errors, evo::GraphError::Code::DuplicateEdge),
        "duplicate edge rejected");
}

void test_cycle() {
  std::cout << "cycle detection\n";
  auto result = evo::Dag::build(
      {trigger("start"), action("a"), action("b"), action("c")},
      {edge("start", "a"), edge("a", "b"), edge("b", "c"), edge("c", "a")});
  check(!result.ok(), "rejected");
  check(has_code(result.errors, evo::GraphError::Code::CycleDetected),
        "cycle_detected reported");
  // Every node on/after the cycle is named.
  std::set<std::string> named;
  for (const auto& e : result.errors) {
    if (e.code == evo::GraphError::Code::CycleDetected) {
      named.insert(e.node.value);
    }
  }
  check(named.count("a") && named.count("b") && named.count("c"),
        "all cycle members named");
}

void test_json_round_trip() {
  std::cout << "canonical JSON round-trip\n";
  auto first = evo::Dag::build(
      {trigger("start"), action("right"), action("left"), action("join")},
      {edge("start", "right"), edge("start", "left"), edge("right", "join"),
       edge("left", "join")});
  check(first.ok(), "original builds");
  const std::string text = first.dag->to_json_string();

  auto second = evo::Dag::from_json_string(text);
  check(second.ok(), "rebuilds from JSON");
  check(second.dag->to_json_string() == text,
        "byte-identical canonical serialization");
  check(second.dag->topo_order() == first.dag->topo_order(),
        "same deterministic topo order");
  const auto* left = second.dag->node(evo::NodeId{"left"});
  check(left && left->kind == evo::NodeKind::Action && left->type == "act",
        "metadata survives round-trip");
}

void test_malformed_json() {
  std::cout << "malformed payloads\n";
  check(!evo::Dag::from_json_string("{not json").ok(), "syntax error");
  check(!evo::Dag::from_json_string("{}").ok(), "missing arrays");
  auto empty = evo::Dag::from_json_string(R"({"nodes":[],"edges":[]})");
  check(empty.ok() && empty.dag->node_count() == 0,
        "empty graph builds (execution check would flag missing trigger)");
  check(!evo::Dag::from_json_string(
             R"({"nodes":[{"id":"a","kind":"bogus"}],"edges":[]})")
             .ok(),
        "unknown kind rejected");
  check(!evo::Dag::from_json_string(
             R"({"nodes":[{"id":"a"}],"edges":[{"from":"a"}]})")
             .ok(),
        "edge missing 'to' rejected");
}

void test_execution_problems() {
  std::cout << "execution contract checks\n";
  auto no_trigger =
      evo::Dag::build({action("a"), action("b")}, {edge("a", "b")});
  check(no_trigger.ok(), "no-trigger graph is structurally valid");
  check(no_trigger.dag->execution_problems().size() == 1,
        "flags missing trigger");

  auto two_triggers = evo::Dag::build(
      {trigger("t1"), trigger("t2"), action("a")},
      {edge("t1", "a"), edge("t2", "a")});
  check(two_triggers.ok(), "two-trigger graph is structurally valid");
  check(two_triggers.dag->execution_problems().size() == 1,
        "flags two triggers");
}

}  // namespace

int main() {
  test_linear();
  test_diamond();
  test_wide_fan();
  test_disconnected();
  test_duplicate_id();
  test_empty_id();
  test_missing_endpoint();
  test_self_loop_and_duplicate_edge();
  test_cycle();
  test_json_round_trip();
  test_malformed_json();
  test_execution_problems();

  if (failures != 0) {
    std::cout << failures << " DAG check(s) failed\n";
    return 1;
  }
  std::cout << "all DAG model tests passed\n";
  return 0;
}
