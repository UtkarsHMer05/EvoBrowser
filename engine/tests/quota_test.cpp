// M36 unit tests: TenantQuotaGate (multi-tenant quotas + backpressure).
//
// Pure scheduler-core: no transport, no store, no threads. Exercises the
// gate's admission, task-capacity, release, and counter semantics directly.
//
// Covers:
//   1. Unconfigured gate (all limits 0) admits/acquires everything.
//   2. Per-org active-run cap: admit up to the limit, reject beyond.
//   3. Global active-run cap across orgs.
//   4. release_run frees a slot so a later admit succeeds.
//   5. readmit_run re-counts without checking caps (restart recovery).
//   6. Per-org in-flight task cap: acquire up to the limit, defer beyond.
//   7. Global resource-class capacity (browser vs external-io separately).
//   8. release_task frees a slot so a later acquire succeeds.
//   9. reacquire_task re-counts without checking caps (resume).
//  10. Counters track admitted/rejected/acquired/deferred/released.
//  11. Double-release clamps at zero (never negative).
//  12. to_json_string emits a parseable snapshot.
//
// M37 (fair scheduling):
//  13. Fairness off => first-come-first-served (backwards compatible).
//  14. Fairness on, equal weights => least-served org wins a free slot.
//  15. Weighted fairness => a weight-2 org earns 2 grants per 1 for weight-1.
//  16. Starvation resistance => a waiting org is served within bounded grants.
//  17. Demand cleared on grant + stale demand dropped (no hostage slot).
//  18. Fairness applies only to capped classes (uncapped unaffected).

#include <chrono>
#include <cstdio>
#include <string>
#include <thread>

#include "evo/json.hpp"
#include "evo/quota.hpp"

using evo::QuotaConfig;
using evo::ResourceClass;
using evo::TenantQuotaGate;

namespace {

int failures = 0;
void check(bool cond, const char* label) {
  if (cond) {
    printf("  ok   %s\n", label);
  } else {
    printf("  FAIL %s\n", label);
    ++failures;
  }
}

}  // namespace

int main() {
  // --- 1. Unconfigured gate admits/acquires everything ----------------------
  {
    TenantQuotaGate gate;  // all limits 0 => unlimited
    check(gate.admit_run("org-a"), "unconfigured: admit_run ok");
    check(gate.admit_run("org-a"), "unconfigured: second admit ok");
    check(gate.acquire_task("org-a", ResourceClass::Browser),
          "unconfigured: acquire_task ok");
    check(gate.acquire_task("org-b", ResourceClass::ExternalIo),
          "unconfigured: acquire_task other org/class ok");
    gate.release_run("org-a");
    gate.release_task("org-a", ResourceClass::Browser);
  }

  // --- 2. Per-org active-run cap -------------------------------------------
  {
    QuotaConfig cfg;
    cfg.max_active_runs_per_org = 2;
    TenantQuotaGate gate(cfg);
    check(gate.admit_run("org-a"), "per-org cap: 1st admit ok");
    check(gate.admit_run("org-a"), "per-org cap: 2nd admit ok");
    check(!gate.admit_run("org-a"), "per-org cap: 3rd admit REJECTED");
    // A different org is unaffected by org-a's cap.
    check(gate.admit_run("org-b"), "per-org cap: other org still admits");
    check(gate.active_runs("org-a") == 2, "per-org cap: org-a depth == 2");
    check(gate.counters().rejected_runs == 1, "per-org cap: 1 rejection counted");
  }

  // --- 3. Global active-run cap across orgs --------------------------------
  {
    QuotaConfig cfg;
    cfg.max_active_runs_global = 2;
    TenantQuotaGate gate(cfg);
    check(gate.admit_run("org-a"), "global cap: org-a 1st ok");
    check(gate.admit_run("org-b"), "global cap: org-b 1st ok");
    check(!gate.admit_run("org-c"), "global cap: 3rd across orgs REJECTED");
    check(gate.counters().active_runs_now == 2, "global cap: depth == 2");
  }

  // --- 4. release_run frees a slot -----------------------------------------
  {
    QuotaConfig cfg;
    cfg.max_active_runs_per_org = 1;
    TenantQuotaGate gate(cfg);
    check(gate.admit_run("org-a"), "release: admit ok");
    check(!gate.admit_run("org-a"), "release: at cap, reject");
    gate.release_run("org-a");
    check(gate.admit_run("org-a"), "release: after release, admit ok again");
  }

  // --- 5. readmit_run re-counts without checking caps ----------------------
  {
    QuotaConfig cfg;
    cfg.max_active_runs_per_org = 1;
    TenantQuotaGate gate(cfg);
    gate.readmit_run("org-a");  // resumed run: never rejected
    gate.readmit_run("org-a");  // second resumed run: still re-counted
    check(gate.active_runs("org-a") == 2,
          "readmit: re-counted past the cap (restart recovery)");
    check(gate.counters().rejected_runs == 0, "readmit: no rejection counted");
  }

  // --- 6. Per-org in-flight task cap ---------------------------------------
  {
    QuotaConfig cfg;
    cfg.max_inflight_tasks_per_org = 2;
    TenantQuotaGate gate(cfg);
    check(gate.acquire_task("org-a", ResourceClass::Internal),
          "task cap: 1st acquire ok");
    check(gate.acquire_task("org-a", ResourceClass::Internal),
          "task cap: 2nd acquire ok");
    check(!gate.acquire_task("org-a", ResourceClass::Internal),
          "task cap: 3rd acquire DEFERRED");
    check(gate.acquire_task("org-b", ResourceClass::Internal),
          "task cap: other org still acquires");
    check(gate.inflight_tasks("org-a") == 2, "task cap: org-a in-flight == 2");
    check(gate.counters().deferred_tasks == 1, "task cap: 1 deferral counted");
  }

  // --- 7. Global resource-class capacity (browser vs external-io) ----------
  {
    QuotaConfig cfg;
    cfg.global_class_capacity[ResourceClass::Browser] = 1;
    cfg.global_class_capacity[ResourceClass::ExternalIo] = 2;
    TenantQuotaGate gate(cfg);
    // Browser capacity 1, shared across orgs.
    check(gate.acquire_task("org-a", ResourceClass::Browser),
          "class cap: browser 1st ok");
    check(!gate.acquire_task("org-b", ResourceClass::Browser),
          "class cap: browser 2nd (other org) DEFERRED");
    // ExternalIo capacity 2 is a SEPARATE pool — unaffected by browser usage.
    check(gate.acquire_task("org-b", ResourceClass::ExternalIo),
          "class cap: external-io 1st ok (separate pool)");
    check(gate.acquire_task("org-a", ResourceClass::ExternalIo),
          "class cap: external-io 2nd ok");
    check(!gate.acquire_task("org-a", ResourceClass::ExternalIo),
          "class cap: external-io 3rd DEFERRED");
    // Internal has no configured capacity => unlimited.
    check(gate.acquire_task("org-a", ResourceClass::Internal),
          "class cap: internal unlimited (no configured cap)");
    check(gate.inflight_class(ResourceClass::Browser) == 1,
          "class cap: browser in-flight == 1");
    check(gate.inflight_class(ResourceClass::ExternalIo) == 2,
          "class cap: external-io in-flight == 2");
  }

  // --- 8. release_task frees a slot ----------------------------------------
  {
    QuotaConfig cfg;
    cfg.global_class_capacity[ResourceClass::Browser] = 1;
    TenantQuotaGate gate(cfg);
    check(gate.acquire_task("org-a", ResourceClass::Browser),
          "release task: acquire ok");
    check(!gate.acquire_task("org-b", ResourceClass::Browser),
          "release task: at cap, defer");
    gate.release_task("org-a", ResourceClass::Browser);
    check(gate.acquire_task("org-b", ResourceClass::Browser),
          "release task: after release, acquire ok");
  }

  // --- 9. reacquire_task re-counts without checking caps -------------------
  {
    QuotaConfig cfg;
    cfg.global_class_capacity[ResourceClass::Browser] = 1;
    TenantQuotaGate gate(cfg);
    gate.reacquire_task("org-a", ResourceClass::Browser);  // resumed in-flight
    check(gate.inflight_class(ResourceClass::Browser) == 1,
          "reacquire: re-counted (resume)");
    check(!gate.acquire_task("org-b", ResourceClass::Browser),
          "reacquire: new acquire still respects the re-counted cap");
  }

  // --- 10. Counters track the full lifecycle -------------------------------
  {
    QuotaConfig cfg;
    cfg.max_active_runs_per_org = 1;
    cfg.max_inflight_tasks_per_org = 1;
    TenantQuotaGate gate(cfg);
    gate.admit_run("org-a");          // admitted=1
    gate.admit_run("org-a");          // rejected=1
    gate.acquire_task("org-a", ResourceClass::Internal);  // acquired=1
    gate.acquire_task("org-a", ResourceClass::Internal);  // deferred=1
    gate.release_task("org-a", ResourceClass::Internal);  // released=1
    gate.release_run("org-a");
    auto c = gate.counters();
    check(c.admitted_runs == 1, "counters: admitted_runs == 1");
    check(c.rejected_runs == 1, "counters: rejected_runs == 1");
    check(c.acquired_tasks == 1, "counters: acquired_tasks == 1");
    check(c.deferred_tasks == 1, "counters: deferred_tasks == 1");
    check(c.released_tasks == 1, "counters: released_tasks == 1");
    check(c.active_runs_now == 0, "counters: active_runs_now == 0 after release");
    check(c.max_active_runs == 1, "counters: max_active_runs high-water == 1");
  }

  // --- 11. Double-release clamps at zero -----------------------------------
  {
    TenantQuotaGate gate;
    gate.admit_run("org-a");
    gate.release_run("org-a");
    gate.release_run("org-a");  // double release: harmless
    check(gate.active_runs("org-a") == 0, "double release_run: clamped at 0");
    gate.acquire_task("org-a", ResourceClass::Browser);
    gate.release_task("org-a", ResourceClass::Browser);
    gate.release_task("org-a", ResourceClass::Browser);  // double release
    check(gate.inflight_tasks("org-a") == 0,
          "double release_task: org count clamped at 0");
    check(gate.inflight_class(ResourceClass::Browser) == 0,
          "double release_task: class count clamped at 0");
  }

  // --- 12. to_json_string emits a parseable snapshot -----------------------
  {
    QuotaConfig cfg;
    cfg.max_active_runs_per_org = 5;
    TenantQuotaGate gate(cfg);
    gate.admit_run("org-a");
    gate.acquire_task("org-a", ResourceClass::Browser);
    const std::string js = gate.to_json_string();
    auto parsed = evo::json::parse(js);
    check(parsed.has_value(), "json snapshot parses");
    if (parsed.has_value()) {
      const evo::json::Value* quota = parsed->find("quota");
      check(quota != nullptr, "json snapshot has 'quota' object");
      if (quota) {
        const evo::json::Value* orgs = quota->find("orgs");
        check(orgs != nullptr && orgs->find("org-a") != nullptr,
              "json snapshot tracks per-org depth");
      }
    }
  }

  // ===========================================================================
  // M37: fair scheduling (weighted least-served-first over fresh demand).
  // All scenarios are deterministic: single-threaded, no sleeps except where a
  // demand-timeout must elapse (test 17).
  // ===========================================================================

  // --- 13. Fairness OFF => first-come-first-served (backwards compatible) ---
  {
    QuotaConfig cfg;
    cfg.global_class_capacity[ResourceClass::Browser] = 1;
    cfg.fair_scheduling = false;  // default M36 behavior
    TenantQuotaGate gate(cfg);
    check(gate.acquire_task("org-a", ResourceClass::Browser),
          "fair off: org-a acquires the only slot");
    check(!gate.acquire_task("org-b", ResourceClass::Browser),
          "fair off: org-b deferred (class full)");
    gate.release_task("org-a", ResourceClass::Browser);
    check(gate.acquire_task("org-b", ResourceClass::Browser),
          "fair off: org-b acquires after release (FCFS)");
    check(gate.counters().fair_order_deferrals == 0,
          "fair off: no fair-order deferrals counted");
  }

  // --- 14. Fairness ON, equal weights => least-served org wins --------------
  {
    QuotaConfig cfg;
    cfg.global_class_capacity[ResourceClass::Browser] = 1;
    cfg.fair_scheduling = true;
    TenantQuotaGate gate(cfg);
    // org-a takes the slot first (it is the only demand => recipient).
    check(gate.acquire_task("org-a", ResourceClass::Browser),
          "fair eq: org-a acquires (only demand)");
    // org-b registers demand while the class is full (deferred, tracked).
    check(!gate.acquire_task("org-b", ResourceClass::Browser),
          "fair eq: org-b deferred while full (demand registered)");
    check(gate.demand_count(ResourceClass::Browser) == 1,
          "fair eq: org-b demand tracked (org-a cleared on grant)");
    // org-a releases and re-polls (re-registers demand). Now both wait; org-b
    // is least-served (0 vs 1) so it must win the free slot.
    gate.release_task("org-a", ResourceClass::Browser);
    check(!gate.acquire_task("org-a", ResourceClass::Browser),
          "fair eq: org-a deferred in favor of least-served org-b");
    check(gate.acquire_task("org-b", ResourceClass::Browser),
          "fair eq: least-served org-b wins the free slot");
    check(gate.counters().fair_order_deferrals == 1,
          "fair eq: one fair-order deferral counted");
  }

  // --- 15. Weighted fairness => weight-2 earns 2 grants per 1 for weight-1 --
  {
    QuotaConfig cfg;
    cfg.global_class_capacity[ResourceClass::Browser] = 1;
    cfg.fair_scheduling = true;
    cfg.org_weights["org-a"] = 2;  // explicit non-equal weight (M37 step 4)
    cfg.org_weights["org-b"] = 1;
    TenantQuotaGate gate(cfg);
    check(gate.weight_for("org-a") == 2 && gate.weight_for("org-b") == 1,
          "weighted: explicit weights read back");
    check(gate.weight_for("org-c") == 1, "weighted: absent org defaults to 1");

    // Both orgs register demand; org-a (weight 2) should earn the first two
    // grants, org-b the third (served/weight: a=0/2, b=0/1 -> tie, a wins by
    // id; then a=1/2 < b=0/1? 1*1 < 0*2 false -> b wins; then a=1/2 vs b=1/1
    // -> 1*1 < 1*2 true -> a wins). Sequence: a, b, a.
    check(gate.acquire_task("org-a", ResourceClass::Browser),
          "weighted: grant 1 -> org-a");
    check(!gate.acquire_task("org-b", ResourceClass::Browser),
          "weighted: org-b registers demand (deferred, full)");
    gate.release_task("org-a", ResourceClass::Browser);
    // Re-poll both: org-a served=1 weight=2 (0.5), org-b served=0 weight=1 (0).
    // org-b is least-served => wins grant 2.
    check(!gate.acquire_task("org-a", ResourceClass::Browser),
          "weighted: grant 2 defers org-a (org-b least-served)");
    check(gate.acquire_task("org-b", ResourceClass::Browser),
          "weighted: grant 2 -> org-b");
    gate.release_task("org-b", ResourceClass::Browser);
    // Re-poll both: org-a served=1 weight=2 (0.5), org-b served=1 weight=1 (1).
    // org-a is least-served => wins grant 3.
    check(gate.acquire_task("org-a", ResourceClass::Browser),
          "weighted: grant 3 -> org-a (0.5 < 1.0)");
    check(gate.served_count("org-a", ResourceClass::Browser) == 2 &&
              gate.served_count("org-b", ResourceClass::Browser) == 1,
          "weighted: served counts a=2 b=1 over 3 grants");
  }

  // --- 16. Starvation resistance: waiting org served within bounded grants --
  // A large tenant (org-a) with a deep backlog must not indefinitely starve a
  // small tenant (org-b). With equal weights and capacity 1, org-b must be
  // served at least once within the first 2 grants after it registers demand.
  {
    QuotaConfig cfg;
    cfg.global_class_capacity[ResourceClass::Browser] = 1;
    cfg.fair_scheduling = true;
    TenantQuotaGate gate(cfg);
    // org-a burns through several grants alone (deep backlog).
    for (int i = 0; i < 5; ++i) {
      check(gate.acquire_task("org-a", ResourceClass::Browser),
            "starve: org-a acquires while alone");
      gate.release_task("org-a", ResourceClass::Browser);
    }
    check(gate.served_count("org-a", ResourceClass::Browser) == 5,
          "starve: org-a served 5 times before org-b arrives");
    // org-b registers demand (deferred: class full from org-a's next acquire).
    check(gate.acquire_task("org-a", ResourceClass::Browser),
          "starve: org-a holds the slot");
    check(!gate.acquire_task("org-b", ResourceClass::Browser),
          "starve: org-b registers demand (deferred)");
    gate.release_task("org-a", ResourceClass::Browser);
    // org-a re-polls, but org-b (served 0) is now least-served vs org-a (5).
    check(!gate.acquire_task("org-a", ResourceClass::Browser),
          "starve: org-a deferred despite its backlog");
    check(gate.acquire_task("org-b", ResourceClass::Browser),
          "starve: org-b served within bounded grants (no starvation)");
  }

  // --- 17. Demand cleared on grant + stale demand dropped -------------------
  {
    QuotaConfig cfg;
    cfg.global_class_capacity[ResourceClass::Browser] = 1;
    cfg.fair_scheduling = true;
    cfg.fair_demand_timeout_ms = 1;  // tiny window so staleness elapses fast
    TenantQuotaGate gate(cfg);
    check(gate.acquire_task("org-a", ResourceClass::Browser),
          "demand: org-a acquires (demand cleared on grant)");
    check(gate.demand_count(ResourceClass::Browser) == 0,
          "demand: no waiting demand after a grant");
    // org-b registers demand, then goes silent (stops polling). After the
    // timeout its demand is stale and must be dropped, so org-a (re-polling)
    // is not deferred in favor of the absent org-b.
    check(!gate.acquire_task("org-b", ResourceClass::Browser),
          "demand: org-b registers demand (deferred, full)");
    std::this_thread::sleep_for(std::chrono::milliseconds(5));  // > 1ms timeout
    gate.release_task("org-a", ResourceClass::Browser);
    check(gate.acquire_task("org-a", ResourceClass::Browser),
          "demand: stale org-b demand dropped; org-a not held hostage");
    check(gate.demand_count(ResourceClass::Browser) == 0,
          "demand: stale entry removed from the demand map");
  }

  // --- 18. Fairness applies only to CAPPED classes --------------------------
  {
    QuotaConfig cfg;
    cfg.global_class_capacity[ResourceClass::Browser] = 1;  // capped
    // ExternalIo intentionally uncapped.
    cfg.fair_scheduling = true;
    TenantQuotaGate gate(cfg);
    // Uncapped class: both orgs acquire freely (no starvation possible).
    check(gate.acquire_task("org-a", ResourceClass::ExternalIo),
          "capped-only: org-a acquires uncapped ExternalIo");
    check(gate.acquire_task("org-b", ResourceClass::ExternalIo),
          "capped-only: org-b acquires uncapped ExternalIo (no deferral)");
    check(gate.counters().fair_order_deferrals == 0,
          "capped-only: no fair-order deferrals on uncapped class");
  }

  if (failures == 0) {
    printf("\nALL M36 QUOTA GATE TESTS PASSED!\n");
    return 0;
  }
  printf("\n%d FAILURE(S)\n", failures);
  return 1;
}
