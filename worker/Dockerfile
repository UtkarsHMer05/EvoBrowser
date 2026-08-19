# Phase 2 — distributed worker image (Milestone 23).
#
# Builds the standalone TypeScript worker against the repo so it can load the
# shared proto (engine/proto/evo/execution.proto) via the envelope codec.
# Run with the synthetic executor (M23); real node executors land in M24.
#
# Build from the repo root:
#   docker build -f worker/Dockerfile -t evo-phase2-worker .
# Scale via compose:
#   docker compose -f infra/phase2/docker-compose.yml up --scale worker=4
#
# No secrets are baked in; Redis credentials come from env vars at runtime.

FROM node:20-alpine

WORKDIR /app

# Install dependencies first for layer caching.
COPY package.json package-lock.json ./
RUN npm ci --no-audit --no-fund

# Copy the worker source and the shared proto it loads at runtime.
COPY worker ./worker
COPY engine/proto ./engine/proto
COPY tsconfig.json ./

ENV NODE_ENV=production
# Default to the Phase-2 local Redis service name on the compose network.
ENV EVO_PHASE2_REDIS_HOST=redis
ENV EVO_PHASE2_REDIS_PORT=6379
ENV EVO_WORKER_ENV_PREFIX=evo:dev
ENV EVO_WORKER_GROUP=workers

CMD ["npx", "tsx", "worker/src/main.ts"]
