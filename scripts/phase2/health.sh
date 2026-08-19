#!/usr/bin/env bash
# Verify the Phase-2 local infra is healthy: container health, Redis PING,
# and Postgres connectivity. Milestone 18. Exits non-zero on any failure.

source "$(dirname "${BASH_SOURCE[0]}")/lib.sh"

require_docker

fail=0

echo "==> Container status"
compose ps

echo "==> Redis PING"
if docker exec evo-phase2-redis redis-cli ping | grep -q PONG; then
  echo "    redis: PONG"
else
  echo "    redis: FAILED" >&2
  fail=1
fi

echo "==> Postgres connectivity"
if docker exec evo-phase2-postgres pg_isready \
    -U "${EVO_PHASE2_PG_USER}" -d "${EVO_PHASE2_PG_DB}" >/dev/null 2>&1; then
  version="$(docker exec evo-phase2-postgres psql \
    -U "${EVO_PHASE2_PG_USER}" -d "${EVO_PHASE2_PG_DB}" \
    -tAc 'SELECT version();' 2>/dev/null | head -1)"
  echo "    postgres: ok (${version})"
else
  echo "    postgres: FAILED" >&2
  fail=1
fi

if [ "${fail}" -ne 0 ]; then
  echo "==> Phase-2 infra health: FAILED" >&2
  exit 1
fi
echo "==> Phase-2 infra health: OK"
