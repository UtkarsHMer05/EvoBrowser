#!/usr/bin/env bash
# Schema smoke test for the Phase-2 run schema (Milestone 19). Runs against the
# LOCAL Phase-2 Postgres container only. Asserts that the additive tables exist
# and that their identity/unique constraints actually reject duplicates.
#
# Exits non-zero on any failed assertion.

source "$(dirname "${BASH_SOURCE[0]}")/lib.sh"

require_docker

psql_local() {
  docker exec -i evo-phase2-postgres psql \
    -U "${EVO_PHASE2_PG_USER}" -d "${EVO_PHASE2_PG_DB}" \
    -v ON_ERROR_STOP=1 "$@"
}

fail=0
pass() { echo "    PASS: $1"; }
failmsg() { echo "    FAIL: $1" >&2; fail=1; }

# expect_ok <label> <sql>: statement must succeed.
expect_ok() {
  if psql_local -q -c "$2" >/dev/null 2>&1; then pass "$1"; else failmsg "$1"; fi
}

# expect_violation <label> <sql>: statement must fail (constraint violation).
expect_violation() {
  if psql_local -q -c "$2" >/dev/null 2>&1; then failmsg "$1 (expected rejection)"; else pass "$1"; fi
}

echo "==> Tables exist"
for t in workflow_versions workflow_runs node_runs task_attempts idempotency_records; do
  n="$(psql_local -tAc "SELECT count(*) FROM information_schema.tables WHERE table_schema='public' AND table_name='${t}'")"
  if [ "${n}" = "1" ]; then pass "table ${t}"; else failmsg "table ${t} missing"; fi
done

echo "==> Seed fixture rows"
expect_ok "insert workflow" \
  "INSERT INTO workflows (id, org_id, name, graph) VALUES ('11111111-1111-1111-1111-111111111111','org_smoke','smoke','{\"nodes\":[],\"edges\":[]}'::jsonb) ON CONFLICT (id) DO NOTHING;"
expect_ok "insert workflow_version v1" \
  "INSERT INTO workflow_versions (id, workflow_id, org_id, version_number, graph) VALUES ('22222222-2222-2222-2222-222222222222','11111111-1111-1111-1111-111111111111','org_smoke',1,'{\"nodes\":[],\"edges\":[]}'::jsonb) ON CONFLICT DO NOTHING;"
expect_ok "insert workflow_run" \
  "INSERT INTO workflow_runs (id, org_id, workflow_id, workflow_version_id, engine, status) VALUES ('run_smoke','org_smoke','11111111-1111-1111-1111-111111111111','22222222-2222-2222-2222-222222222222','evo','queued') ON CONFLICT (id) DO NOTHING;"
expect_ok "insert node_run" \
  "INSERT INTO node_runs (id, run_id, node_id, node_type, status) VALUES ('33333333-3333-3333-3333-333333333333','run_smoke','n0','bench:sleep','blocked') ON CONFLICT DO NOTHING;"
expect_ok "insert task_attempt 1" \
  "INSERT INTO task_attempts (id, node_run_id, attempt_number, status) VALUES ('44444444-4444-4444-4444-444444444444','33333333-3333-3333-3333-333333333333',1,'queued') ON CONFLICT DO NOTHING;"

echo "==> Unique constraints reject duplicates"
expect_violation "duplicate workflow version_number" \
  "INSERT INTO workflow_versions (workflow_id, org_id, version_number, graph) VALUES ('11111111-1111-1111-1111-111111111111','org_smoke',1,'{}'::jsonb);"
expect_violation "duplicate node_run (run_id,node_id)" \
  "INSERT INTO node_runs (run_id, node_id, node_type) VALUES ('run_smoke','n0','bench:sleep');"
expect_violation "duplicate task_attempt (node_run_id,attempt_number)" \
  "INSERT INTO task_attempts (node_run_id, attempt_number) VALUES ('33333333-3333-3333-3333-333333333333',1);"
expect_violation "duplicate idempotency key" \
  "INSERT INTO idempotency_records (key) VALUES ('k1'); INSERT INTO idempotency_records (key) VALUES ('k1');"

echo "==> Engine discriminator coexists"
expect_ok "legacy run alongside evo run" \
  "INSERT INTO workflow_runs (id, org_id, workflow_id, engine, status) VALUES ('run_legacy','org_smoke','11111111-1111-1111-1111-111111111111','legacy','queued') ON CONFLICT (id) DO NOTHING;"

echo "==> Cleanup fixture rows"
expect_ok "cleanup" \
  "DELETE FROM task_attempts; DELETE FROM node_runs; DELETE FROM workflow_runs; DELETE FROM workflow_versions; DELETE FROM workflows; DELETE FROM idempotency_records;"

if [ "${fail}" -ne 0 ]; then
  echo "==> Phase-2 schema smoke: FAILED" >&2
  exit 1
fi
echo "==> Phase-2 schema smoke: OK"
