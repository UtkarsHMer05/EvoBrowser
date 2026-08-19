#!/usr/bin/env bash
# Shared helpers for Phase-2 local infra scripts (Milestone 18).
# Sourced by up.sh / down.sh / reset.sh / health.sh — not run directly.

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
COMPOSE_FILE="${REPO_ROOT}/infra/phase2/docker-compose.yml"

# Non-secret local development defaults. Overridable via environment.
export EVO_PHASE2_REDIS_PORT="${EVO_PHASE2_REDIS_PORT:-6390}"
export EVO_PHASE2_PG_PORT="${EVO_PHASE2_PG_PORT:-5433}"
export EVO_PHASE2_PG_USER="${EVO_PHASE2_PG_USER:-evo}"
export EVO_PHASE2_PG_PASSWORD="${EVO_PHASE2_PG_PASSWORD:-evo_dev_password}"
export EVO_PHASE2_PG_DB="${EVO_PHASE2_PG_DB:-evo_phase2}"

compose() {
  docker compose -f "${COMPOSE_FILE}" "$@"
}

require_docker() {
  if ! command -v docker >/dev/null 2>&1; then
    echo "ERROR: docker CLI not found." >&2
    echo "Install Docker Desktop: brew install --cask docker, then start it." >&2
    exit 1
  fi
  if ! docker info >/dev/null 2>&1; then
    echo "ERROR: Docker daemon is not running." >&2
    echo "Start Docker Desktop (or your Docker engine) and retry." >&2
    exit 1
  fi
}

# Connection string for the LOCAL Phase-2 Postgres only. Never derived from,
# and never applied to, DATABASE_URL / DATABASE_URL_UNPOOLED (Neon).
phase2_pg_url() {
  echo "postgresql://${EVO_PHASE2_PG_USER}:${EVO_PHASE2_PG_PASSWORD}@127.0.0.1:${EVO_PHASE2_PG_PORT}/${EVO_PHASE2_PG_DB}"
}
