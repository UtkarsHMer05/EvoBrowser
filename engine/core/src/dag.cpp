#include "evo/dag.hpp"

#include <algorithm>
#include <map>
#include <set>
#include <tuple>

namespace evo {

namespace {

const char* kind_to_string(NodeKind kind) {
  return kind == NodeKind::Trigger ? "trigger" : "action";
}

std::optional<NodeKind> kind_from_string(const std::string& s) {
  if (s == "trigger") return NodeKind::Trigger;
  if (s == "action") return NodeKind::Action;
  return std::nullopt;
}

}  // namespace

const char* to_string(GraphError::Code code) {
  switch (code) {
    case GraphError::Code::EmptyNodeId:
      return "empty_node_id";
    case GraphError::Code::DuplicateNodeId:
      return "duplicate_node_id";
    case GraphError::Code::MissingEdgeEndpoint:
      return "missing_edge_endpoint";
    case GraphError::Code::SelfLoop:
      return "self_loop";
    case GraphError::Code::DuplicateEdge:
      return "duplicate_edge";
    case GraphError::Code::CycleDetected:
      return "cycle_detected";
    case GraphError::Code::MalformedDocument:
      return "malformed_document";
  }
  return "unknown";
}

BuildResult Dag::build(std::vector<NodeSpec> nodes, std::vector<Edge> edges) {
  BuildResult result;

  // --- Structural validation (collects every problem, never stops early) ---
  std::set<NodeId> seen_ids;
  for (const auto& spec : nodes) {
    if (spec.id.value.empty()) {
      result.errors.push_back(
          {GraphError::Code::EmptyNodeId, "node id must not be empty", {}});
      continue;
    }
    if (!seen_ids.insert(spec.id).second) {
      result.errors.push_back({GraphError::Code::DuplicateNodeId,
                               "duplicate node id: " + spec.id.value,
                               spec.id});
    }
  }

  std::set<std::pair<NodeId, NodeId>> seen_edges;
  for (const auto& edge : edges) {
    const bool from_known = seen_ids.count(edge.from) > 0;
    const bool to_known = seen_ids.count(edge.to) > 0;
    if (!from_known || !to_known) {
      result.errors.push_back(
          {GraphError::Code::MissingEdgeEndpoint,
           "edge references unknown node: " +
               (from_known ? edge.to.value : edge.from.value),
           from_known ? edge.to : edge.from});
      continue;
    }
    if (edge.from == edge.to) {
      result.errors.push_back({GraphError::Code::SelfLoop,
                               "self-loop on node: " + edge.from.value,
                               edge.from});
      continue;
    }
    if (!seen_edges.emplace(edge.from, edge.to).second) {
      result.errors.push_back({GraphError::Code::DuplicateEdge,
                               "duplicate edge: " + edge.from.value + " -> " +
                                   edge.to.value,
                               {}});
    }
  }

  if (!result.errors.empty()) return result;

  // --- Construction ---
  Dag dag;
  dag.node_order_.reserve(nodes.size());
  dag.specs_ = std::move(nodes);
  for (const auto& spec : dag.specs_) {
    dag.node_order_.push_back(spec.id);
  }

  std::map<NodeId, std::size_t> index;
  for (std::size_t i = 0; i < dag.node_order_.size(); ++i) {
    index.emplace(dag.node_order_[i], i);
  }

  dag.edges_ = std::move(edges);
  std::sort(dag.edges_.begin(), dag.edges_.end(),
            [](const Edge& a, const Edge& b) {
              return std::tie(a.from, a.to) < std::tie(b.from, b.to);
            });

  dag.preds_.resize(dag.node_order_.size());
  dag.succs_.resize(dag.node_order_.size());
  for (const auto& edge : dag.edges_) {
    dag.succs_[index[edge.from]].push_back(edge.to);
    dag.preds_[index[edge.to]].push_back(edge.from);
  }
  for (auto& list : dag.preds_) {
    std::sort(list.begin(), list.end());
  }
  for (auto& list : dag.succs_) {
    std::sort(list.begin(), list.end());
  }

  // --- Kahn's algorithm with lexicographic tie-break (deterministic) ---
  std::map<NodeId, std::size_t> indegree;
  for (const auto& id : dag.node_order_) {
    indegree[id] = 0;
  }
  for (const auto& edge : dag.edges_) {
    ++indegree[edge.to];
  }
  std::set<NodeId> ready;  // sorted => deterministic pop order
  for (const auto& [id, degree] : indegree) {
    if (degree == 0) ready.insert(id);
  }
  while (!ready.empty()) {
    const NodeId next = *ready.begin();
    ready.erase(ready.begin());
    dag.topo_order_.push_back(next);
    for (const auto& succ : dag.succs_[index[next]]) {
      if (--indegree[succ] == 0) ready.insert(succ);
    }
  }

  if (dag.topo_order_.size() != dag.node_order_.size()) {
    // Nodes left with unsatisfied indegree form (or hang off) a cycle.
    std::set<NodeId> ordered(dag.topo_order_.begin(), dag.topo_order_.end());
    for (const auto& id : dag.node_order_) {
      if (!ordered.count(id)) {
        result.errors.push_back({GraphError::Code::CycleDetected,
                                 "cycle involves node: " + id.value, id});
      }
    }
    return result;
  }

  result.dag = std::move(dag);
  return result;
}

const NodeSpec* Dag::node(const NodeId& id) const {
  const auto idx = index_of(id);
  return idx ? &specs_[*idx] : nullptr;
}

const std::vector<NodeId>& Dag::predecessors(const NodeId& id) const {
  static const std::vector<NodeId> kEmpty;
  const auto idx = index_of(id);
  return idx ? preds_[*idx] : kEmpty;
}

const std::vector<NodeId>& Dag::successors(const NodeId& id) const {
  static const std::vector<NodeId> kEmpty;
  const auto idx = index_of(id);
  return idx ? succs_[*idx] : kEmpty;
}

std::vector<NodeId> Dag::reachable_from(const NodeId& start) const {
  std::set<NodeId> visited;
  if (!index_of(start)) return {};
  std::vector<NodeId> stack{start};
  while (!stack.empty()) {
    const NodeId current = stack.back();
    stack.pop_back();
    if (!visited.insert(current).second) continue;
    for (const auto& succ : successors(current)) {
      stack.push_back(succ);
    }
  }
  return {visited.begin(), visited.end()};  // sorted => deterministic
}

bool Dag::is_reachable(const NodeId& from, const NodeId& to) const {
  const auto reached = reachable_from(from);
  return std::binary_search(reached.begin(), reached.end(), to);
}

std::vector<std::string> Dag::execution_problems() const {
  std::vector<std::string> problems;

  std::vector<NodeId> triggers;
  for (const auto& spec : specs_) {
    if (spec.kind == NodeKind::Trigger) triggers.push_back(spec.id);
  }
  if (triggers.size() != 1) {
    problems.push_back("workflow needs exactly one trigger node (found " +
                       std::to_string(triggers.size()) + ")");
    return problems;  // reachability is undefined without a single entry
  }

  const auto reached = reachable_from(triggers.front());
  std::set<NodeId> reachable(reached.begin(), reached.end());
  for (const auto& id : node_order_) {
    if (!reachable.count(id)) {
      problems.push_back("node not reachable from trigger: " + id.value);
    }
  }
  return problems;
}

json::Value Dag::to_json() const {
  json::Array nodes;
  std::vector<const NodeSpec*> sorted;
  sorted.reserve(specs_.size());
  for (const auto& spec : specs_) sorted.push_back(&spec);
  std::sort(sorted.begin(), sorted.end(),
            [](const NodeSpec* a, const NodeSpec* b) { return a->id < b->id; });
  for (const auto* spec : sorted) {
    json::Object obj;
    obj.emplace("id", json::Value(spec->id.value));
    obj.emplace("kind", json::Value(std::string(kind_to_string(spec->kind))));
    obj.emplace("type", json::Value(spec->type));
    nodes.push_back(json::Value(std::move(obj)));
  }

  json::Array edge_array;
  for (const auto& edge : edges_) {  // already sorted by (from, to)
    json::Object obj;
    obj.emplace("from", json::Value(edge.from.value));
    obj.emplace("to", json::Value(edge.to.value));
    edge_array.push_back(json::Value(std::move(obj)));
  }

  json::Object doc;
  doc.emplace("nodes", json::Value(std::move(nodes)));
  doc.emplace("edges", json::Value(std::move(edge_array)));
  return json::Value(std::move(doc));
}

BuildResult Dag::from_json(const json::Value& doc) {
  BuildResult result;
  const auto malformed = [&](const std::string& detail) {
    result.errors.push_back(
        {GraphError::Code::MalformedDocument, detail, {}});
    return result;
  };

  const json::Value* nodes_value = doc.find("nodes");
  const json::Value* edges_value = doc.find("edges");
  if (!nodes_value || nodes_value->kind() != json::Value::Kind::Array) {
    return malformed("missing or invalid 'nodes' array");
  }
  if (!edges_value || edges_value->kind() != json::Value::Kind::Array) {
    return malformed("missing or invalid 'edges' array");
  }

  std::vector<NodeSpec> nodes;
  for (const auto& item : nodes_value->as_array()) {
    const json::Value* id = item.find("id");
    const json::Value* kind = item.find("kind");
    const json::Value* type = item.find("type");
    if (!id || id->kind() != json::Value::Kind::String) {
      return malformed("node missing string 'id'");
    }
    NodeSpec spec;
    spec.id = NodeId{id->as_string()};
    if (kind && kind->kind() == json::Value::Kind::String) {
      const auto parsed = kind_from_string(kind->as_string());
      if (!parsed) {
        return malformed("node '" + spec.id.value + "' has unknown kind");
      }
      spec.kind = *parsed;
    }
    if (type && type->kind() == json::Value::Kind::String) {
      spec.type = type->as_string();
    }
    nodes.push_back(std::move(spec));
  }

  std::vector<Edge> edges;
  for (const auto& item : edges_value->as_array()) {
    const json::Value* from = item.find("from");
    const json::Value* to = item.find("to");
    if (!from || from->kind() != json::Value::Kind::String || !to ||
        to->kind() != json::Value::Kind::String) {
      return malformed("edge missing string 'from'/'to'");
    }
    edges.push_back({NodeId{from->as_string()}, NodeId{to->as_string()}});
  }

  return build(std::move(nodes), std::move(edges));
}

BuildResult Dag::from_json_string(const std::string& text) {
  const auto parsed = json::parse(text);
  if (!parsed) {
    BuildResult result;
    result.errors.push_back(
        {GraphError::Code::MalformedDocument, "invalid JSON syntax", {}});
    return result;
  }
  return from_json(*parsed);
}

std::optional<std::size_t> Dag::index_of(const NodeId& id) const {
  for (std::size_t i = 0; i < node_order_.size(); ++i) {
    if (node_order_[i] == id) return i;
  }
  return std::nullopt;
}

}  // namespace evo
