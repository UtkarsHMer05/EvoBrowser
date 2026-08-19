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

  if (failures == 0) {
    printf("\nALL M26 PG RUN STORE TESTS PASSED!\n");
    return 0;
  }
  printf("\n%d FAILURE(S)\n", failures);
  return 1;
}
