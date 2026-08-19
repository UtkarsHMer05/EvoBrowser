#!/usr/bin/env bash
# Apply the committed Drizzle migrations (lib/db/migrations/*.sql) to the LOCAL
# Phase-2 Postgres container ONLY. Milestone 19.
#
# drizzle-kit's built-in migrator uses the @neondatabase/serverless driver,
# which cannot open a plain TCP connection to local Postgres — so this script
# applies the exact same committed SQL files with psql inside transactions.
#
# SAFETY: targets only the evo-phase2-postgres container. It never reads
# .env.local, DATABASE_URL, or DATABASE_URL_UNPOOLED, and can never reach Neon.
# Applied files are tracked in `phase2_migrations_applied`; re-running is a
# no-op for already-applied migrations.

source "$(dirname "${BASH_SOURCE[0]}")/lib.sh"

require_docker

MIGRATIONS_DIR="${REPO_ROOT}/lib/db/migrations"

if ! docker exec evo-phase2-postgres pg_isready \
    -U "${EVO_PHASE2_PG_USER}" -d "${EVO_PHASE2_PG_DB}" >/dev/null 2>&1; then
  echo "ERROR: evo-phase2-postgres is not running. Run scripts/phase2/up.sh first." >&2
  exit 1
fi

psql_local() {
  docker exec -i evo-phase2-postgres psql \
    -U "${EVO_PHASE2_PG_USER}" -d "${EVO_PHASE2_PG_DB}" \
    -v ON_ERROR_STOP=1 "$@"
}

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

echo "==> Done: ${applied_count} migration(s) applied to local Phase-2 Postgres."
psql_local -tAc "SELECT tablename FROM pg_tables WHERE schemaname = 'public' ORDER BY 1" \
  | sed 's/^/    table: /'
