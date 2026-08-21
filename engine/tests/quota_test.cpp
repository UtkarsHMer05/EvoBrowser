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

#include <cstdio>
#include <string>

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

  if (failures == 0) {
    printf("\nALL M36 QUOTA GATE TESTS PASSED!\n");
    return 0;
  }
  printf("\n%d FAILURE(S)\n", failures);
  return 1;
}
