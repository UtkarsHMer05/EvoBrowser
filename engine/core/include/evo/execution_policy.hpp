#pragma once

// Execution resource classes and browser-affinity policy (Milestone 12).
//
// Classifies nodes into resource classes so the concurrent scheduler can gate
// dispatch on resource availability *independent of dependency readiness*. The
// default mapping is keyed by the existing Phase-1 node types and does NOT
// change planner output or any product behavior:
//
//   start        -> Internal       (no external resource)
//   open-url/act/extract/observe/agent -> Browser  (affinity = run id, cap 1)
//   send-email   -> ExternalIo     (affinity "email", unbounded)
//   <anything>   -> Internal       (fallback; safe default)
//
// All Phase-1 browser nodes in one run share a single browser-affinity key
// derived from the run id, so they serialize on one browser session/worker
// (capacity 1). Two independent affinity keys (e.g. two different runs) may
// progress concurrently when worker capacity permits.

#include <map>
#include <string>

#include "evo/dag.hpp"

namespace evo {

// Broad resource class a node consumes while executing.
enum class ResourceClass {
  Internal,    // no external/locked resource (e.g. start, synthetic CPU)
  Browser,     // shares a single browser session per affinity key (cap 1)
  ExternalIo,  // external side-effect work (e.g. send-email), unbounded
};

const char* to_string(ResourceClass rc);

// Resolved policy for one node: which resource it needs, the affinity key that
// identifies a shared instance of that resource, and the max concurrent tasks
// allowed on that (class, key) pair.
struct ResourcePolicy {
  ResourceClass klass = ResourceClass::Internal;
  std::string affinity_key;  // empty for unbounded/internal resources
  int capacity = kUnbounded;

  static constexpr int kUnbounded = 1 << 20;
};

class ExecutionPolicy {
 public:
  // `run_id` seeds the default browser-affinity key so every browser node in
  // this run maps to the same capacity-1 resource.
  explicit ExecutionPolicy(std::string run_id = "default-run");

  // Resolve the policy for a node. Pure read of configuration; safe to call
  // from multiple scheduler threads concurrently (no mutation after setup).
  ResourcePolicy policy_for(const NodeSpec& spec) const;

  // --- Test/override hooks (not part of the default product mapping) ---
  // These let tests simulate independent affinity keys or custom capacities.
  // They must be called before run() begins (single-threaded setup phase).
  void set_node_affinity(const NodeId& id, std::string key);
  void set_type_affinity(const std::string& type, std::string key);
  void set_capacity(ResourceClass klass, const std::string& key, int cap);

 private:
  bool is_browser_type(const std::string& type) const;
  int capacity_for(ResourceClass klass, const std::string& key) const;

  std::string run_id_;
  std::map<NodeId, std::string> node_affinity_;
  std::map<std::string, std::string> type_affinity_;
  std::map<std::string, int> capacity_overrides_;  // keyed "class:key"
};

}  // namespace evo