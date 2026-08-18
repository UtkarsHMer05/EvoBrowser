#pragma once

// Canonical DAG model for the Evo engine (Milestone 05).
//
// This model is deliberately independent of React Flow, Browserbase, Next.js,
// and Redis. It represents only what the scheduler needs: strongly typed node
// ids, node metadata (kind + opaque type), directed edges, predecessor /
// successor adjacency, deterministic topological order, reachability, and
// structural validation.
//
// Thread-safety contract: a Dag is immutable once built. All accessors are
// const and safe to call from any thread; ownership is shared by const
// reference / shared_ptr. Mutation happens only through Dag::build, which
// returns a new instance.

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

#include "evo/json.hpp"

namespace evo {

// Strongly typed node identifier. Wrapping the string prevents accidental
// mixing of node ids with run ids, attempt ids, or tenant ids later.
struct NodeId {
  std::string value;

  bool operator==(const NodeId& other) const { return value == other.value; }
  bool operator<(const NodeId& other) const { return value < other.value; }
};

// Node role in the scheduler contract. Trigger nodes have no predecessors and
// act as run entry points; action nodes execute work.
enum class NodeKind {
  Trigger,
  Action,
};

struct NodeSpec {
  NodeId id;
  NodeKind kind = NodeKind::Action;
  // Opaque to the engine core (e.g. "open-url", "act", "send-email", or a
  // synthetic benchmark type). Interpreted only by executors/workers.
  std::string type;
};

struct Edge {
  NodeId from;
  NodeId to;

  bool operator==(const Edge& other) const {
    return from == other.from && to == other.to;
  }
};

// Structural validation errors. Each carries a stable code (for tests and
// later machine-readable diagnostics) plus a human-readable message.
struct GraphError {
  enum class Code {
    EmptyNodeId,
    DuplicateNodeId,
    MissingEdgeEndpoint,
    SelfLoop,
    DuplicateEdge,
    CycleDetected,
    MalformedDocument,
  };

  Code code;
  std::string message;
  // The offending node when applicable (empty value otherwise).
  NodeId node;
};

const char* to_string(GraphError::Code code);

class Dag;
struct BuildResult;

class Dag {
 public:
  // Validates and constructs a Dag. Never throws on bad input; all problems
  // are reported through BuildResult::errors.
  static BuildResult build(std::vector<NodeSpec> nodes,
                         std::vector<Edge> edges);

  // Deterministic node order (insertion order from build input).
  const std::vector<NodeId>& node_ids() const { return node_order_; }
  std::size_t node_count() const { return node_order_.size(); }
  std::size_t edge_count() const { return edges_.size(); }

  // Node lookup; nullptr when unknown.
  const NodeSpec* node(const NodeId& id) const;

  // Adjacency. Vectors are sorted by NodeId for determinism. Empty span for
  // unknown ids.
  const std::vector<NodeId>& predecessors(const NodeId& id) const;
  const std::vector<NodeId>& successors(const NodeId& id) const;

  // Deterministic topological order (Kahn's algorithm; lexicographic
  // tie-break among ready nodes). Valid because build() rejects cycles.
  const std::vector<NodeId>& topo_order() const { return topo_order_; }

  // All nodes reachable from `start` following edge direction (inclusive).
  // Deterministic (sorted). Unknown start id yields an empty result.
  std::vector<NodeId> reachable_from(const NodeId& start) const;
  bool is_reachable(const NodeId& from, const NodeId& to) const;

  // Scheduler-contract checks beyond structure (see docs/phase2
  // ARCHITECTURE.md §6.4 and the Phase-1 comparison note in PROGRESS.md):
  //   - exactly one trigger node,
  //   - every node reachable from that trigger.
  // Returns human-readable problems; empty means executable.
  std::vector<std::string> execution_problems() const;

  // Canonical JSON shape (minimal — no React Flow fields):
  //   {"nodes":[{"id":...,"kind":"trigger"|"action","type":...}],
  //    "edges":[{"from":...,"to":...}]}
  // Nodes are serialized sorted by id and edges sorted by (from, to) so the
  // same logical graph always produces byte-identical JSON.
  json::Value to_json() const;
  std::string to_json_string() const { return json::serialize(to_json()); }

  // Inverse of to_json. Reports MalformedDocument errors through BuildResult.
  static BuildResult from_json(const json::Value& doc);
  static BuildResult from_json_string(const std::string& text);

 private:
  std::vector<NodeId> node_order_;
  std::vector<NodeSpec> specs_;        // parallel to node_order_
  std::vector<Edge> edges_;
  std::vector<NodeId> topo_order_;
  // Adjacency parallel to node_order_ (sorted neighbor lists).
  std::vector<std::vector<NodeId>> preds_;
  std::vector<std::vector<NodeId>> succs_;

  std::optional<std::size_t> index_of(const NodeId& id) const;
};

// Result of Dag::build / Dag::from_json*. `dag` is engaged only when the graph
// passed structural validation; `errors` is non-empty otherwise, and `ok()` is
// the one-shot validity check. Defined after Dag so std::optional<Dag> has a
// complete type at this point (a forward declaration is insufficient).
struct BuildResult {
  std::vector<GraphError> errors;
  std::optional<Dag> dag;

  bool ok() const { return errors.empty(); }
};

}  // namespace evo
