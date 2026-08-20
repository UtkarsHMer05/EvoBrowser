// M26 integration test: PgRunStore against the LOCAL Phase-2 Postgres
// container (scripts/phase2/up.sh + migrate-local.sh). Exercises the
// parameterized persistence path and the schema-enforced invariants:
// idempotent run/node/attempt creation, at-most-once terminal completion,
// and audit readers.
//
// Skips (exit 0) when the local Postgres is unreachable or the schema is not
// migrated, so the engine CTest suite stays green on machines without the
// Phase-2 stack. Connection params come from EVO_PHASE2_PG_* env vars with
// the scripts/phase2/lib.sh defaults.

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <string>

#include "evo/pg_run_store.hpp"
#include "evo/run_store.hpp"

using evo::PgRunStore;
using evo::PgRunStoreConfig;

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

std::string env_or(const char* name, const std::string& fallback) {
  const char* v = std::getenv(name);
  return (v && *v) ? std::string(v) : fallback;
}
}  // namespace

int main() {
  PgRunStoreConfig cfg;
  cfg.host = env_or("EVO_PHASE2_PG_HOST", "127.0.0.1");
  cfg.port = std::atoi(env_or("EVO_PHASE2_PG_PORT", "5433").c_str());
  cfg.user = env_or("EVO_PHASE2_PG_USER", "evo");
  cfg.password = env_or("EVO_PHASE2_PG_PASSWORD", "evo_dev_password");
  cfg.dbname = env_or("EVO_PHASE2_PG_DB", "evo_phase2");
  cfg.max_retries = 1;  // fail fast for the reachability probe

  PgRunStore store(cfg);
  if (!store.connect()) {
    printf("SKIP: M26 pg_run_store (local Postgres unreachable at %s:%d; run "
           "scripts/phase2/up.sh + migrate-local.sh to enable)\n",
           cfg.host.c_str(), cfg.port);
    return 0;
  }
  printf("  ok   connected to Postgres at %s:%d\n", cfg.host.c_str(), cfg.port);

  // Unique run id per invocation keeps re-runs hermetic.
  const std::string run_id =
      "m26-pgtest-" +
      std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
  const std::string wf_id = "11111111-1111-1111-1111-111111111111";

  // --- FK parent + run creation --------------------------------------------
  check(store.ensure_workflow(wf_id, "org-pg", "m26 test workflow"),
        "ensure_workflow inserts parent row");
  check(store.ensure_workflow(wf_id, "org-pg", "m26 test workflow"),
        "ensure_workflow idempotent");

  evo::RunRecord run;
  run.run_id = run_id;
  run.org_id = "org-pg";
  run.workflow_id = wf_id;
  run.engine = "evo";
  run.status = evo::run_status::kRunning;
  check(store.create_run(run, evo::now_wall_ms()), "create_run inserts");
  check(store.create_run(run, evo::now_wall_ms()), "create_run idempotent");

  // --- M29: app pre-creates the run row as 'queued'; the engine's create_run
  // must promote it to 'running' (not leave it queued via DO NOTHING). -------
  const std::string pre_run_id =
      "m29-pre-" +
      std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
  evo::RunRecord pre_run;
  pre_run.run_id = pre_run_id;
  pre_run.org_id = "org-pg";
  pre_run.workflow_id = wf_id;
  pre_run.engine = "evo";
  pre_run.status = evo::run_status::kQueued;
  check(store.create_run(pre_run, evo::now_wall_ms()),
        "pre-created run row (queued)");
  pre_run.status = evo::run_status::kRunning;
  check(store.create_run(pre_run, evo::now_wall_ms()),
        "create_run promotes queued -> running");
  auto pre = store.get_run(pre_run_id);
  check(pre.has_value() && pre->status == evo::run_status::kRunning,
        "pre-created run is now running (M29 promotion)");
  // A terminal run must never be regressed by a late create_run.
  check(store.finish_run(pre_run_id, evo::run_status::kSucceeded, "succeeded",
                         evo::now_wall_ms()),
        "finish_run on promoted run");
  check(store.create_run(pre_run, evo::now_wall_ms()),
        "create_run after terminal is a no-op");
  auto pre2 = store.get_run(pre_run_id);
  check(pre2.has_value() && pre2->status == evo::run_status::kSucceeded,
        "terminal run not regressed by create_run");

  // --- M30: cancellation-request timestamp (first request wins) --------------
  const std::string cancel_run_id =
      "m30-cancel-" +
      std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
  evo::RunRecord cancel_run;
  cancel_run.run_id = cancel_run_id;
  cancel_run.org_id = "org-pg";
  cancel_run.workflow_id = wf_id;
  cancel_run.engine = "evo";
  cancel_run.status = evo::run_status::kRunning;
  check(store.create_run(cancel_run, evo::now_wall_ms()),
        "m30: run row created");
  const std::int64_t first_ts = evo::now_wall_ms();
  check(store.mark_cancel_requested(cancel_run_id, "user requested stop",
                                    first_ts),
        "m30: mark_cancel_requested stamps first request");
  auto cr = store.get_run(cancel_run_id);
  check(cr.has_value() && cr->cancel_requested_at > 0 &&
            cr->cancel_reason == "user requested stop",
        "m30: cancel_requested_at + reason round-trip");
  // Repeated request: idempotent, first timestamp/reason preserved.
  check(store.mark_cancel_requested(cancel_run_id, "second request",
                                    first_ts + 60000),
        "m30: repeated mark_cancel_requested reports success");
  auto cr2 = store.get_run(cancel_run_id);
  check(cr2.has_value() && cr2->cancel_reason == "user requested stop" &&
            cr2->cancel_requested_at == cr->cancel_requested_at,
        "m30: first request wins (timestamp + reason not overwritten)");
  // Unknown run: reports false.
  check(!store.mark_cancel_requested("m30-no-such-run", "x", first_ts),
        "m30: mark_cancel_requested on unknown run returns false");

  // --- Node runs ------------------------------------------------------------
  check(store.create_node_run(run_id, "a", "bench:echo"), "create_node_run a");
  check(store.create_node_run(run_id, "a", "bench:echo"),
        "create_node_run idempotent (unique run+node)");
  check(store.create_node_run(run_id, "b", "bench:echo"), "create_node_run b");

  // --- Attempts: idempotent per (node, attempt) -----------------------------
  check(store.record_attempt(run_id, "a", 1, "worker-1", evo::now_wall_ms()),
        "record_attempt creates row");
  check(!store.record_attempt(run_id, "a", 1, "worker-2", evo::now_wall_ms()),
        "duplicate attempt number rejected (no second row)");
  check(store.attempt_row_count(run_id, "a") == 1,
        "exactly one attempt row after duplicate");

  // --- At-most-once terminal completion -------------------------------------
  check(store.complete_node_run(run_id, "a", evo::node_status::kSucceeded,
                                "{\"ok\":true}", "", evo::now_wall_ms()),
        "complete_node_run applies success");
  check(!store.complete_node_run(run_id, "a", evo::node_status::kSucceeded,
                                 "{\"dup\":1}", "", evo::now_wall_ms()),
        "duplicate success rejected (at-most-once)");
  check(!store.complete_node_run(run_id, "a", evo::node_status::kFailed, "",
                                 "late failure", evo::now_wall_ms()),
        "late failure cannot overwrite terminal success");

  auto na = store.get_node_run(run_id, "a");
  // jsonb normalizes whitespace ({"ok":true} -> {"ok": true}), so compare
  // semantically rather than byte-for-byte.
  const bool output_ok = na.has_value() &&
                         na->output_json.find("\"ok\"") != std::string::npos &&
                         na->output_json.find("true") != std::string::npos;
  check(na.has_value() && na->status == evo::node_status::kSucceeded &&
            output_ok && na->attempt_count == 1,
        "node a audit row: succeeded + output + attempt_count=1");

  // --- Failure persistence ---------------------------------------------------
  check(store.record_attempt(run_id, "b", 1, "worker-1", evo::now_wall_ms()),
        "record_attempt b");
  check(store.complete_node_run(run_id, "b", evo::node_status::kFailed, "",
                                "boom", evo::now_wall_ms()),
        "complete_node_run applies failure");
  auto nb = store.get_node_run(run_id, "b");
  check(nb.has_value() && nb->status == evo::node_status::kFailed &&
            nb->failure_reason == "boom",
        "node b audit row: failed + failure_reason");

  // --- finish_attempt ---------------------------------------------------------
  check(store.finish_attempt(run_id, "a", 1, "worker-1",
                             evo::node_status::kSucceeded, "",
                             evo::now_wall_ms()),
        "finish_attempt updates attempt row");
  check(!store.finish_attempt(run_id, "a", 9, "worker-1",
                              evo::node_status::kSucceeded, "",
                              evo::now_wall_ms()),
        "finish_attempt unknown attempt rejected");

  // --- Run terminal -----------------------------------------------------------
  check(store.finish_run(run_id, evo::run_status::kFailed, "failed",
                         evo::now_wall_ms()),
        "finish_run terminal");
  auto r2 = store.get_run(run_id);
  check(r2.has_value() && r2->status == evo::run_status::kFailed &&
            r2->outcome == "failed" && r2->engine == "evo",
        "run audit row terminal failed (engine=evo)");

  // --- SQL-injection probe: values are parameters, never interpolated --------
  const std::string evil_node = "x'; DROP TABLE node_runs; --";
  check(store.create_node_run(run_id, evil_node, "bench:echo"),
        "hostile node_id accepted as data");
  auto evil = store.get_node_run(run_id, evil_node);
  check(evil.has_value() && evil->node_id == evil_node,
        "hostile node_id round-trips verbatim (parameterized)");
  check(store.get_node_run(run_id, "a").has_value(),
        "node_runs table intact after injection probe");

  // --- M31: worker registry + task-lease audit -------------------------------
  // A fresh run + node + attempt to exercise the durable lease lifecycle.
  const std::string lease_run_id =
      "m31-pgtest-" +
      std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
  evo::RunRecord lease_run;
  lease_run.run_id = lease_run_id;
  lease_run.org_id = "org-pg";
  lease_run.workflow_id = wf_id;
  lease_run.engine = "evo";
  lease_run.status = evo::run_status::kRunning;
  check(store.create_run(lease_run, evo::now_wall_ms()),
        "m31: lease run created");
  check(store.create_node_run(lease_run_id, "n1", "bench:echo"),
        "m31: lease node created");
  check(store.record_attempt(lease_run_id, "n1", 1, "", evo::now_wall_ms()),
        "m31: attempt row created (no worker yet)");

  // Worker registry: register + heartbeat upsert the liveness row.
  const std::string w1 = "m31-pg-worker-1";
  const std::int64_t hb1 = evo::now_wall_ms();
  check(store.worker_heartbeat(w1, "evo:pgtest", hb1),
        "m31: worker heartbeat registers the row");
  auto wrec = store.get_worker(w1);
  check(wrec.has_value() && wrec->status == "alive" &&
            wrec->env_prefix == "evo:pgtest" && wrec->last_heartbeat_ms > 0,
        "m31: worker registry row readable (alive + heartbeat ts)");
  check(store.worker_heartbeat(w1, "evo:pgtest", hb1 + 1000),
        "m31: heartbeat upsert idempotent");
  auto wrec2 = store.get_worker(w1);
  check(wrec2.has_value() && wrec2->last_heartbeat_ms > wrec->last_heartbeat_ms,
        "m31: heartbeat refreshes last_heartbeat_at");
  check(!store.get_worker("m31-no-such-worker").has_value(),
        "m31: unknown worker -> nullopt");

  // Lease init (scheduler side): stamps the queue-wait deadline only.
  const std::int64_t t0 = evo::now_wall_ms();
  check(store.init_attempt_lease(lease_run_id, "n1", 1, t0 + 30000),
        "m31: init_attempt_lease stamps queue-wait deadline");
  auto l0 = store.get_attempt_lease(lease_run_id, "n1", 1);
  check(l0.has_value() && l0->status == evo::attempt_status::kRunning &&
            l0->worker_id.empty() && l0->expires_ms > 0 &&
            l0->acquired_ms == 0,
        "m31: initialized lease has expiry but no worker");

  // Acquire: worker-1 takes over the queue-wait lease.
  check(store.acquire_attempt_lease(lease_run_id, "n1", 1, w1, t0 + 10,
                                    t0 + 30010),
        "m31: worker-1 acquires the lease");
  auto l1 = store.get_attempt_lease(lease_run_id, "n1", 1);
  check(l1.has_value() && l1->worker_id == w1 && l1->acquired_ms > 0 &&
            l1->renewed_ms > 0 && l1->expires_ms > 0,
        "m31: lease evidence (worker/acquired/renewed/expires) recorded");

  // Steal guard: a DIFFERENT worker cannot take an unexpired lease.
  check(!store.acquire_attempt_lease(lease_run_id, "n1", 1, "m31-pg-worker-2",
                                     t0 + 20, t0 + 30020),
        "m31: unexpired lease cannot be stolen by another worker");
  auto l1b = store.get_attempt_lease(lease_run_id, "n1", 1);
  check(l1b.has_value() && l1b->worker_id == w1,
        "m31: lease still held by worker-1 after steal attempt");

  // Renew: only the holder may renew, while running.
  check(store.renew_attempt_lease(lease_run_id, "n1", 1, w1, t0 + 100,
                                  t0 + 30100),
        "m31: holder renews the lease");
  auto l2 = store.get_attempt_lease(lease_run_id, "n1", 1);
  check(l2.has_value() && l2->renewed_ms > l1->renewed_ms &&
            l2->expires_ms > l1->expires_ms,
        "m31: renewal advances renewed_at + expires_at");
  check(!store.renew_attempt_lease(lease_run_id, "n1", 1, "m31-pg-worker-2",
                                   t0 + 110, t0 + 30110),
        "m31: non-holder cannot renew the lease");

  // Scan: nothing expired yet (expiry is 30s out).
  check(store.scan_expired_attempt_leases(lease_run_id, t0 + 200).empty(),
        "m31: scan finds nothing before expiry");
  // Scan at a now past the expiry finds the running attempt.
  auto expired = store.scan_expired_attempt_leases(lease_run_id, t0 + 40000);
  check(expired.size() == 1 && expired[0].node_id == "n1" &&
            expired[0].attempt_number == 1 && expired[0].worker_id == w1,
        "m31: scan finds the expired running attempt");

  // Reap: at-most-once transition to lease_expired.
  check(store.mark_attempt_lease_expired(lease_run_id, "n1", 1, w1,
                                         t0 + 40001),
        "m31: mark_attempt_lease_expired applies once");
  check(!store.mark_attempt_lease_expired(lease_run_id, "n1", 1, w1,
                                          t0 + 40002),
        "m31: second reap is a no-op (at-most-once)");
  auto l3 = store.get_attempt_lease(lease_run_id, "n1", 1);
  check(l3.has_value() && l3->status == evo::attempt_status::kLeaseExpired &&
            l3->expired_ms > 0,
        "m31: attempt row durably lease_expired with evidence timestamp");
  // A reaped attempt is no longer running, so the scan never returns it.
  check(store.scan_expired_attempt_leases(lease_run_id, t0 + 50000).empty(),
        "m31: reaped attempt no longer returned by scan");

  // Completion races reap: a completed attempt can never be reaped (M31 no-go:
  // lease expiry must not double-complete the logical node).
  check(store.record_attempt(lease_run_id, "n1", 2, "", evo::now_wall_ms()),
        "m31: attempt 2 created");
  check(store.init_attempt_lease(lease_run_id, "n1", 2, t0 + 60000),
        "m31: attempt 2 lease initialized");
  check(store.acquire_attempt_lease(lease_run_id, "n1", 2, w1, t0 + 60010,
                                    t0 + 90010),
        "m31: attempt 2 acquired");
  check(store.finish_attempt(lease_run_id, "n1", 2, w1,
                             evo::node_status::kSucceeded, "", t0 + 60500),
        "m31: attempt 2 completed (succeeded)");
  check(!store.mark_attempt_lease_expired(lease_run_id, "n1", 2, w1,
                                          t0 + 95000),
        "m31: completed attempt cannot be reaped (no double-complete)");
  auto l4 = store.get_attempt_lease(lease_run_id, "n1", 2);
  check(l4.has_value() && l4->status == evo::attempt_status::kSucceeded,
        "m31: completed attempt keeps its terminal status");

  check(store.finish_run(lease_run_id, evo::run_status::kSucceeded,
                         "succeeded", evo::now_wall_ms()),
        "m31: lease run finalized");

  // --- M31: queue-wait lease reap (never-claimed attempt) --------------------
  // An attempt that was dispatched but never claimed has worker_id NULL; its
  // queue-wait lease must still be reapable (NULL = '' is never true in SQL,
  // so the reap matches an empty worker_id argument against NULL explicitly).
  const std::string qw_run_id =
      "m31-pgtest-qw-" +
      std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
  evo::RunRecord qw_run;
  qw_run.run_id = qw_run_id;
  qw_run.org_id = "org-pg";
  qw_run.workflow_id = wf_id;
  qw_run.engine = "evo";
  qw_run.status = evo::run_status::kRunning;
  check(store.create_run(qw_run, evo::now_wall_ms()), "m31: qw run created");
  check(store.create_node_run(qw_run_id, "q1", "bench:echo"),
        "m31: qw node created");
  check(store.record_attempt(qw_run_id, "q1", 1, "", evo::now_wall_ms()),
        "m31: qw attempt created (never claimed)");
  const std::int64_t q0 = evo::now_wall_ms();
  check(store.init_attempt_lease(qw_run_id, "q1", 1, q0 + 100),
        "m31: qw lease initialized");
  auto qw_expired = store.scan_expired_attempt_leases(qw_run_id, q0 + 5000);
  check(qw_expired.size() == 1 && qw_expired[0].worker_id.empty(),
        "m31: scan finds the never-claimed expired attempt (empty worker)");
  check(store.mark_attempt_lease_expired(qw_run_id, "q1", 1, "", q0 + 5001),
        "m31: never-claimed attempt reapable via empty worker_id");
  auto qw_lease = store.get_attempt_lease(qw_run_id, "q1", 1);
  check(qw_lease.has_value() &&
            qw_lease->status == evo::attempt_status::kLeaseExpired &&
            qw_lease->expired_ms > 0,
        "m31: qw attempt durably lease_expired");
  check(store.finish_run(qw_run_id, evo::run_status::kCanceled, "test-done",
                         evo::now_wall_ms()),
        "m31: qw run finalized");

  // --- M32: node retry persistence (retry_wait_until + retry_reason) --------
  {
    const std::string retry_run_id =
        "m32-pgtest-" +
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    evo::RunRecord retry_run;
    retry_run.run_id = retry_run_id;
    retry_run.org_id = "org-pg";
    retry_run.workflow_id = wf_id;
    retry_run.engine = "evo";
    retry_run.status = evo::run_status::kRunning;
    check(store.create_run(retry_run, evo::now_wall_ms()), "m32: retry run created");
    check(store.create_node_run(retry_run_id, "r1", "bench:echo"),
          "m32: retry node created");

    // Park the node in retry_wait with a due-time + reason.
    const std::int64_t due = evo::now_wall_ms() + 5000;
    check(store.set_node_retry_wait(retry_run_id, "r1", due,
                                    "retry after 100ms (transient)"),
          "m32: set_node_retry_wait parks the node");
    auto nr = store.get_node_run(retry_run_id, "r1");
    check(nr.has_value() && nr->status == evo::node_status::kRetryWait &&
              nr->retry_wait_until > 0 && !nr->retry_reason.empty(),
          "m32: retry_wait status + due-time + reason round-trip");
    // The due-time should be close to what we set (wall-clock, ms precision).
    check(nr.has_value() && std::llabs(nr->retry_wait_until - due) < 2000,
          "m32: retry_wait_until round-trips the due-time");

    // A terminal node can never be parked for retry.
    check(store.complete_node_run(retry_run_id, "r1",
                                  evo::node_status::kFailed, "", "boom",
                                  evo::now_wall_ms()),
          "m32: node failed (terminal)");
    check(!store.set_node_retry_wait(retry_run_id, "r1", due, "too late"),
          "m32: terminal node cannot be parked for retry");
    auto nr2 = store.get_node_run(retry_run_id, "r1");
    check(nr2.has_value() && nr2->status == evo::node_status::kFailed,
          "m32: terminal status preserved after rejected retry_wait");

    check(store.finish_run(retry_run_id, evo::run_status::kFailed, "failed",
                           evo::now_wall_ms()),
          "m32: retry run finalized");
  }

  if (failures == 0) {
    printf("\nALL M26 PG RUN STORE TESTS PASSED!\n");
    return 0;
  }
  printf("\n%d FAILURE(S)\n", failures);
  return 1;
}
