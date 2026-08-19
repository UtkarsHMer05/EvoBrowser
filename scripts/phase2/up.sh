#!/usr/bin/env bash
# Start the isolated Phase-2 local infra (Redis + PostgreSQL) and wait for
# both services to become healthy. Milestone 18.
#
# This stack is for Phase-2 integration tests only. It never touches the
# app's Neon database (DATABASE_URL / DATABASE_URL_UNPOOLED).

source "$(dirname "${BASH_SOURCE[0]}")/lib.sh"

require_docker

echo "==> Starting Phase-2 local infra (redis :${EVO_PHASE2_REDIS_PORT}, postgres :${EVO_PHASE2_PG_PORT})"
compose up -d

echo "==> Waiting for health checks..."
compose up -d --wait --wait-timeout 60

echo "==> Phase-2 local infra is up."
echo "    Redis:    redis://127.0.0.1:${EVO_PHASE2_REDIS_PORT}"
echo "    Postgres: $(phase2_pg_url | sed 's/:[^:@]*@/:****@/')"
