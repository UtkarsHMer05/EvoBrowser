#!/usr/bin/env bash
# Apply the committed Drizzle migrations (lib/db/migrations/*.sql) to the LOCAL
# Phase-2 Postgres ONLY. Milestone 19; M38 added a direct-TCP mode for CI.
#
# drizzle-kit's built-in migrator uses the @neondatabase/serverless driver,
# which cannot open a plain TCP connection to local Postgres — so this script
# applies the exact same committed SQL files with psql inside transactions.
#
# Two connection modes (auto-detected):
#   1. docker exec — the default local path: psql runs inside the
#      evo-phase2-postgres container (scripts/phase2/up.sh).
#   2. direct TCP  — used when the container is not reachable but a local
#      `psql` client can reach Postgres at EVO_PHASE2_PG_HOST:PORT. This is the
#      CI path, where Postgres runs as a service container and the runner
#      connects over TCP. Force this mode with EVO_PHASE2_MIGRATE_DIRECT=1.
#
# SAFETY: targets only the Phase-2 local Postgres. It never reads .env.local,
# DATABASE_URL, or DATABASE_URL_UNPOOLED, and can never reach Neon. Applied
# files are tracked in `phase2_migrations_applied`; re-running is a no-op for
# already-applied migrations.

source "$(dirname "${BASH_SOURCE[0]}")/lib.sh"

MIGRATIONS_DIR="${REPO_ROOT}/lib/db/migrations"

# --- Choose connection mode -------------------------------------------------
MODE=""
if [ "${EVO_PHASE2_MIGRATE_DIRECT:-0}" = "1" ]; then
  MODE="direct"
elif command -v docker >/dev/null 2>&1 \
  && docker exec evo-phase2-postgres pg_isready \
       -U "${EVO_PHASE2_PG_USER}" -d "${EVO_PHASE2_PG_DB}" >/dev/null 2>&1; then
  MODE="docker"
elif command -v psql >/dev/null 2>&1 \
  && PGPASSWORD="${EVO_PHASE2_PG_PASSWORD}" pg_isready \
       -h "${EVO_PHASE2_PG_HOST:-127.0.0.1}" -p "${EVO_PHASE2_PG_PORT}" \
       -U "${EVO_PHASE2_PG_USER}" -d "${EVO_PHASE2_PG_DB}" >/dev/null 2>&1; then
  MODE="direct"
else
  echo "ERROR: Phase-2 Postgres is not reachable." >&2
  echo "  Local: run scripts/phase2/up.sh first." >&2
  echo "  CI: ensure the postgres service is up and psql is installed." >&2
  exit 1
fi

psql_local() {
  if [ "${MODE}" = "docker" ]; then
    docker exec -i evo-phase2-postgres psql \
      -U "${EVO_PHASE2_PG_USER}" -d "${EVO_PHASE2_PG_DB}" \
      -v ON_ERROR_STOP=1 "$@"
  else
    PGPASSWORD="${EVO_PHASE2_PG_PASSWORD}" psql \
      -h "${EVO_PHASE2_PG_HOST:-127.0.0.1}" -p "${EVO_PHASE2_PG_PORT}" \
      -U "${EVO_PHASE2_PG_USER}" -d "${EVO_PHASE2_PG_DB}" \
      -v ON_ERROR_STOP=1 "$@"
  fi
}

echo "==> Migrating Phase-2 Postgres (mode: ${MODE})"
echo "==> Ensuring migration tracking table"
psql_local -q <<'SQL'
CREATE TABLE IF NOT EXISTS phase2_migrations_applied (
  name text PRIMARY KEY,
  applied_at timestamptz NOT NULL DEFAULT now()
);
SQL

applied_count=0
for f in "${MIGRATIONS_DIR}"/*.sql; do
  name="$(basename "${f}")"
  already="$(psql_local -tAc "SELECT 1 FROM phase2_migrations_applied WHERE name = '${name}'")"
  if [ "${already}" = "1" ]; then
    echo "    skip ${name} (already applied)"
    continue
  fi
  echo "==> Applying ${name}"
  # Drizzle separates statements with `--> statement-breakpoint`; every real
  # statement already ends with `;`, so stripping the marker yields valid SQL.
  sed 's/--> statement-breakpoint//g' "${f}" | psql_local -q --single-transaction
  psql_local -q -c "INSERT INTO phase2_migrations_applied (name) VALUES ('${name}')"
  applied_count=$((applied_count + 1))
done

echo "==> Done: ${applied_count} migration(s) applied to Phase-2 Postgres."
psql_local -tAc "SELECT tablename FROM pg_tables WHERE schemaname = 'public' ORDER BY 1" \
  | sed 's/^/    table: /'
