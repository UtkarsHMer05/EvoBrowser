# Phase 2 — Isolated Local Infrastructure (Milestone 18)

Local Redis + PostgreSQL containers used **only** by Phase-2 integration tests
and the distributed worker runtime. The production Next.js app keeps using the
Neon database via `DATABASE_URL` / `DATABASE_URL_UNPOOLED`; nothing in this
stack reads or writes those variables.

## Layout

| Path | Purpose |
| :--- | :--- |
| `infra/phase2/docker-compose.yml` | Compose project `evo-phase2`: `redis` (7.4-alpine) + `postgres` (16-alpine) |
| `scripts/phase2/up.sh` | Start the stack and wait for health checks |
| `scripts/phase2/down.sh` | Clean shutdown (containers removed, volumes preserved) |
| `scripts/phase2/reset.sh` | Destroy local volumes and start fresh |
| `scripts/phase2/health.sh` | Verify container health, Redis PING, Postgres connectivity |
| `scripts/phase2/lib.sh` | Shared helpers (compose wrapper, defaults, guards) |

## Endpoints and credentials

All ports bind to `127.0.0.1` only. Credentials are non-secret local
development defaults committed intentionally; never reuse them against a
shared or remote database.

| Service | Default endpoint | Override |
| :--- | :--- | :--- |
| Redis | `redis://127.0.0.1:6390` | `EVO_PHASE2_REDIS_PORT` |
| PostgreSQL | `postgresql://evo:****@127.0.0.1:5433/evo_phase2` | `EVO_PHASE2_PG_PORT`, `EVO_PHASE2_PG_USER`, `EVO_PHASE2_PG_PASSWORD`, `EVO_PHASE2_PG_DB` |

## Commands

```bash
scripts/phase2/up.sh       # start + wait for healthy
scripts/phase2/health.sh   # redis PING + postgres connectivity
scripts/phase2/down.sh     # stop (volumes preserved)
scripts/phase2/reset.sh    # wipe local volumes, start fresh
```

`reset.sh` only removes volumes owned by the `evo-phase2` compose project on the
local Docker daemon. It never references `DATABASE_URL`,
`DATABASE_URL_UNPOOLED`, or any remote database.

## How the Phase-2 engine will use this later

- **PostgreSQL (M19+):** Phase-2 tables (runs, tasks, attempts, events,
  leases) are defined once in the Drizzle schema and applied additively.
  Locally they are migrated against the Phase-2 Postgres above; the same
  schema is valid against any standard Postgres, including Neon, because it
  uses only portable SQL (no local-only extensions). Migrations are additive
  only — never destructive — so the same migration set can later be applied
  to Neon after explicit human approval.
- **Redis (M21+):** Redis Streams is the task/event transport between the
  scheduler service and workers. Streams, consumer groups, and pending-entry
  recovery give at-least-once delivery; idempotency keys (M33) make duplicate
  delivery safe.
- **Separation guarantee:** the app's Neon connection strings are never read
  by Phase-2 scripts or the engine; Phase-2 connection settings use the
  `EVO_PHASE2_*` namespace exclusively.

## Prerequisite

Docker (CLI + daemon). On macOS: `brew install --cask docker`, then start
Docker Desktop. Scripts fail fast with this instruction if Docker is missing
or the daemon is down.
