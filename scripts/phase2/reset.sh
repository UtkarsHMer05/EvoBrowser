#!/usr/bin/env bash
# Reset the isolated Phase-2 local infra: stop containers AND delete their
# named volumes (redis-data, postgres-data), then start fresh. Milestone 18.
#
# SAFETY: this only ever removes volumes owned by the `evo-phase2` compose
# project on the local Docker daemon. It never references DATABASE_URL,
# DATABASE_URL_UNPOOLED, or any remote/Neon database.

source "$(dirname "${BASH_SOURCE[0]}")/lib.sh"

require_docker

echo "==> Resetting Phase-2 local infra (destroys LOCAL volumes only)"
compose down --volumes --remove-orphans

echo "==> Starting fresh stack..."
compose up -d
compose up -d --wait --wait-timeout 60

echo "==> Phase-2 local infra reset complete."
