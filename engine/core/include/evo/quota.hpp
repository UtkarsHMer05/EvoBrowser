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

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <mutex>
#include <string>

#include "evo/execution_policy.hpp"

namespace evo {

// Wall-clock UTC milliseconds since the Unix epoch (same function as
// run_store.hpp; redeclared here so the gate needs no run_store dependency).
std::int64_t now_wall_ms();

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

  // --- Fair scheduling (Milestone 37) ---
  // When true AND a resource class has a finite global capacity, grants of that
  // class's slots are ordered by WEIGHTED FAIR scheduling across the orgs with
  // current demand, instead of first-come-first-served. This prevents a large
  // tenant's backlog from indefinitely starving a small tenant (M37 step 5).
  // Default false = M36 first-come-first-served (backwards compatible; fairness
  // is an explicit opt-in, M37 step 13).
  //
  // Algorithm (documented tradeoff, M37 step 1): least-normalized-service-first,
  // the pull-based analog of weighted round robin / deficit round robin. Each
  // grant to org O for class C increments served_[C][O]. When a class slot is
  // free and several orgs have fresh demand, the slot goes to the org with the
  // smallest served/weight (ties broken by org id for determinism). An org that
  // is not the current recipient is DEFERRED (its node stays READY and its run
  // loop re-polls, refreshing its demand). Because a waiting org's served count
  // stays constant while recipients' counts grow, every live waiting org becomes
  // the minimum within bounded time => no starvation. Chosen over strict WRR
  // because a fixed rotation needs a central dispatcher; this architecture is
  // pull-based (each run loop dispatches its own org's nodes), so the decision
  // must be a pure function of shared state that any caller can evaluate.
  bool fair_scheduling = false;
  // Explicit per-org weights (M37 step 4: equal weights first; non-equal
  // weights must be explicit configuration). An org absent from the map has
  // weight 1. Weight w entitles the org to w-times the service of a weight-1
  // org before it yields (via the served/weight normalization).
  std::map<std::string, int> org_weights;
  // Demand freshness window (wall-clock ms). An org counts as having demand for
  // a class only if it refreshed that demand within this window (it refreshes
  // every time its run loop re-polls a deferred node). Entries that go stale —
  // e.g. the node was canceled but the run continues with other classes — are
  // ignored and dropped, so an absent org can never hold a class slot hostage.
  // 0 => use the default (5000ms).
  std::int64_t fair_demand_timeout_ms = 0;
};

// Observable counters (M36 step 6): queue depth and rejected/deferred totals.
struct QuotaCounters {
  std::size_t admitted_runs = 0;     // runs admitted past the per-org/global cap
  std::size_t rejected_runs = 0;     // submissions rejected (resource exhausted)
  std::size_t acquired_tasks = 0;    // task slots granted at dispatch
  std::size_t deferred_tasks = 0;    // dispatches deferred due to capacity
  std::size_t released_tasks = 0;    // task slots returned
  // M37: dispatches deferred specifically because it was not the caller's turn
  // in the weighted round robin (a subset of deferred_tasks, only when fair
  // scheduling is on). Distinguishes fairness ordering from raw capacity.
  std::size_t fair_order_deferrals = 0;
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

  // --- Fair scheduling observability (M37 step 6) ---
  // Number of class slots granted to this org since construction (the fairness
  // accounting used to pick the least-served recipient).
  std::int64_t served_count(const std::string& org_id, ResourceClass klass) const;
  // Number of orgs with FRESH demand for a class right now (diagnostic/test).
  int demand_count(ResourceClass klass) const;
  // The org's configured weight (1 when absent from org_weights).
  int weight_for(const std::string& org_id) const;

  // Structured JSON snapshot of counters + per-org depth (for the Health RPC).
  std::string to_json_string() const;

 private:
  // M37: pick the fair recipient for a class among orgs with fresh demand —
  // the org with the smallest served/weight (cross-multiplied to stay integral),
  // ties broken by org id for determinism. Returns "" when no org has fresh
  // demand. Caller must hold mu_.
  std::string fair_recipient_locked(ResourceClass klass, std::int64_t now_ms);

  QuotaConfig cfg_;
  mutable std::mutex mu_;
  std::map<std::string, int> active_runs_;       // org -> active run count
  std::map<std::string, int> inflight_tasks_;    // org -> in-flight task count
  std::map<ResourceClass, int> inflight_class_;  // class -> global in-flight
  QuotaCounters counters_;
  int total_active_runs_ = 0;
  // M37 fairness state (guarded by mu_).
  std::map<ResourceClass, std::map<std::string, std::int64_t>> served_;  // grants
  std::map<ResourceClass, std::map<std::string, std::int64_t>> demand_;  // last refresh wall ms
};

}  // namespace evo
