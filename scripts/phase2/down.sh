#!/usr/bin/env bash
# Stop the isolated Phase-2 local infra cleanly (containers removed, volumes
# preserved). Milestone 18. Use reset.sh to also wipe data.

source "$(dirname "${BASH_SOURCE[0]}")/lib.sh"

require_docker

echo "==> Stopping Phase-2 local infra (clean shutdown, volumes preserved)"
compose down

echo "==> Phase-2 local infra stopped."
