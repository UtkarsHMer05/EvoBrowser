#pragma once

// Multi-tenant quotas, global resource capacities, and backpressure
// (Milestone 36).
//
// Prevents one organization from exhausting the scheduler's global resources.
// The gate is the single cross-run authority for two distinct limits:
//
//   1. Run-level ADMISSION (per org): how many concurrently ACTIVE
//      (non-terminal) runs one org may hold. A new submission beyond the limit
//      is REJECTED with a resource-exhausted outcome (fail closed), never
//      silently queued without bound.
//   2. Task-level CAPACITY (cross run): how many concurrently IN-FLIGHT logical
//      tasks one org may hold (the noisy-tenant throttle), and how many
//      in-flight tasks a GLOBAL resource class (especially Browser sessions)
//      may hold across ALL orgs. When either is full, a ready node is DEFERRED
//      (left READY, re-examined next iteration) rather than dispatched — this
//      is backpressure, not rejection.
//
// Backpressure is NOT fairness (M36 no-go): deferring a node does not reorder
// tenants. Fair scheduling is Milestone 37. This module only bounds usage.
//
// Ownership/threading: ONE TenantQuotaGate instance is owned by the scheduler
// service and shared by every run loop in the process. Every method locks the
// internal mutex, so concurrent run loops (one thread each) may admit/acquire/
// release safely. The gate is in-process state: global capacity is enforced per
// scheduler process, not cluster-wide (a durable cluster-wide counter is later
// work; see PROGRESS.md known limitations).
//
// Timestamps: the gate keeps counters only; it emits no timestamps.

#include <cstddef>
#include <map>
#include <mutex>
#include <string>

#include "evo/execution_policy.hpp"

namespace evo {

// Configurable limits. A value of 0 means "unlimited" (backwards compatible:
// an unconfigured gate admits everything).
struct QuotaConfig {
  // Per-org cap on concurrently ACTIVE (non-terminal) runs. 0 => unlimited.
  int max_active_runs_per_org = 0;
  // Global cap on concurrently ACTIVE runs across ALL orgs. Bounds total
  // internal queue growth (M36 step 5). 0 => unlimited.
  int max_active_runs_global = 0;
  // Per-org cap on concurrently IN-FLIGHT logical tasks (dispatched nodes
  // awaiting a result). The noisy-tenant throttle. 0 => unlimited.
  int max_inflight_tasks_per_org = 0;
  // Global in-flight capacity per resource class, across ALL orgs/runs.
  // Absent key or 0 => unlimited. Browser is the constrained class in
  // production; ExternalIo (side effects) is tracked separately (M36 step 8).
  std::map<ResourceClass, int> global_class_capacity;
};

// Observable counters (M36 step 6): queue depth and rejected/deferred totals.
struct QuotaCounters {
  std::size_t admitted_runs = 0;     // runs admitted past the per-org/global cap
  std::size_t rejected_runs = 0;     // submissions rejected (resource exhausted)
  std::size_t acquired_tasks = 0;    // task slots granted at dispatch
  std::size_t deferred_tasks = 0;    // dispatches deferred due to capacity
  std::size_t released_tasks = 0;    // task slots returned
  int active_runs_now = 0;           // current active runs across all orgs
  int max_active_runs = 0;           // high-water mark of active runs
};

class TenantQuotaGate {
 public:
  explicit TenantQuotaGate(QuotaConfig cfg = {});

  TenantQuotaGate(const TenantQuotaGate&) = delete;
  TenantQuotaGate& operator=(const TenantQuotaGate&) = delete;

  // --- Run-level admission (M36 steps 2, 4) ---
  // Try to admit a NEW run for `org_id`. Returns true (and counts the run
  // active) when both the per-org and global active-run caps permit; false
  // when either is at/over the limit, in which case the caller must reject the
  // submission with a resource-exhausted status. Never partially mutates on
  // rejection.
  bool admit_run(const std::string& org_id);

  // Record an ALREADY-ACTIVE run without checking caps (M35 restart recovery:
  // a resumed run is existing durable work, not a new submission, so it must be
  // re-counted but never rejected). Idempotent per call — call once per resumed
  // run.
  void readmit_run(const std::string& org_id);

  // Release a run slot when the run reaches a terminal state. Clamps at zero
  // (a double-release is a harmless no-op, never negative).
  void release_run(const std::string& org_id);

  // --- Task-level capacity (M36 steps 3, 4) ---
  // Try to acquire ONE in-flight task slot for (`org_id`, `klass`). Returns
  // true (and increments both the org's in-flight task count and the class's
  // global in-flight count) when BOTH the per-org task cap and the class's
  // global capacity permit; false otherwise (the caller must DEFER the node,
  // not dispatch it). Atomic: on rejection neither counter changes.
  bool acquire_task(const std::string& org_id, ResourceClass klass);

  // Release ONE in-flight task slot (terminal result, lease reap, or retry
  // park). Clamps each counter at zero.
  void release_task(const std::string& org_id, ResourceClass klass);

  // Re-count an ALREADY-IN-FLIGHT task without checking caps (M35 resume: an
  // in-flight node restored from durable state must be re-counted so capacity
  // accounting survives a restart, and must never be rejected). Call once per
  // restored in-flight node.
  void reacquire_task(const std::string& org_id, ResourceClass klass);

  // --- Observability (M36 step 6) ---
  QuotaCounters counters() const;
  int active_runs(const std::string& org_id) const;
  int inflight_tasks(const std::string& org_id) const;
  int inflight_class(ResourceClass klass) const;
  const QuotaConfig& config() const { return cfg_; }

  // Structured JSON snapshot of counters + per-org depth (for the Health RPC).
  std::string to_json_string() const;

 private:
  QuotaConfig cfg_;
  mutable std::mutex mu_;
  std::map<std::string, int> active_runs_;       // org -> active run count
  std::map<std::string, int> inflight_tasks_;    // org -> in-flight task count
  std::map<ResourceClass, int> inflight_class_;  // class -> global in-flight
  QuotaCounters counters_;
  int total_active_runs_ = 0;
};

}  // namespace evo
