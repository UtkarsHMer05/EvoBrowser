#include "evo/execution_policy.hpp"

#include <string>

namespace evo {

const char* to_string(ResourceClass rc) {
  switch (rc) {
    case ResourceClass::Internal: return "INTERNAL";
    case ResourceClass::Browser: return "BROWSER";
    case ResourceClass::ExternalIo: return "EXTERNAL_IO";
  }
  return "UNKNOWN";
}

ExecutionPolicy::ExecutionPolicy(std::string run_id) : run_id_(std::move(run_id)) {}

bool ExecutionPolicy::is_browser_type(const std::string& type) const {
  // Phase-1 browser node catalog (ARCHITECTURE.md §3 / node-registry).
  return type == "open-url" || type == "act" || type == "extract" ||
         type == "observe" || type == "agent";
}

ResourcePolicy ExecutionPolicy::policy_for(const NodeSpec& spec) const {
  ResourcePolicy rp;

  // Per-node override takes precedence (tests only).
  auto nit = node_affinity_.find(spec.id);
  if (nit != node_affinity_.end()) {
    rp.klass = ResourceClass::Browser;
    rp.affinity_key = nit->second;
    rp.capacity = capacity_for(rp.klass, rp.affinity_key);
    return rp;
  }

  if (spec.type == "start") {
    rp.klass = ResourceClass::Internal;
    rp.affinity_key = "";
    rp.capacity = ResourcePolicy::kUnbounded;
    return rp;
  }

  if (spec.type == "send-email") {
    rp.klass = ResourceClass::ExternalIo;
    rp.affinity_key = "email";
    rp.capacity = capacity_for(rp.klass, rp.affinity_key);
    return rp;
  }

  // Per-type override (tests only).
  auto tit = type_affinity_.find(spec.type);
  if (tit != type_affinity_.end()) {
    rp.klass = ResourceClass::Browser;
    rp.affinity_key = tit->second;
    rp.capacity = capacity_for(rp.klass, rp.affinity_key);
    return rp;
  }

  if (is_browser_type(spec.type)) {
    // Default browser affinity: one capacity-1 session per run.
    rp.klass = ResourceClass::Browser;
    rp.affinity_key = run_id_;
    rp.capacity = capacity_for(rp.klass, rp.affinity_key);
    return rp;
  }

  // Fallback: treat unknown types as internal/unbounded (never invent a
  // browser session for an unrecognized node type).
  rp.klass = ResourceClass::Internal;
  rp.affinity_key = "";
  rp.capacity = ResourcePolicy::kUnbounded;
  return rp;
}

void ExecutionPolicy::set_node_affinity(const NodeId& id, std::string key) {
  node_affinity_[id] = std::move(key);
}

void ExecutionPolicy::set_type_affinity(const std::string& type, std::string key) {
  type_affinity_[type] = std::move(key);
}

void ExecutionPolicy::set_capacity(ResourceClass klass, const std::string& key,
                                   int cap) {
  capacity_overrides_[std::to_string(static_cast<int>(klass)) + ":" + key] = cap;
}

int ExecutionPolicy::capacity_for(ResourceClass klass,
                                  const std::string& key) const {
  auto it =
      capacity_overrides_.find(std::to_string(static_cast<int>(klass)) + ":" + key);
  if (it != capacity_overrides_.end()) {
    return it->second;
  }
  // Browser affinity defaults to capacity 1 (one session per key).
  if (klass == ResourceClass::Browser) return 1;
  return ResourcePolicy::kUnbounded;
}

}  // namespace evo