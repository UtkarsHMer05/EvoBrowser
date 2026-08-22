# Phase 2 — Security & Threat Model

Milestone 38. This document describes the trust boundaries, the security
controls implemented for the distributed engine, and the threat-model notes
that justify them. It documents **current, implemented behavior** — not plans.

## 1. Trust boundaries

The Phase-2 runtime introduces four process/network boundaries. Each is listed
with who may cross it and what protects it.

| Boundary | From → To | Who may cross | Protection |
| :--- | :--- | :--- | :--- |
| Browser → app | User browser → Next.js | Authenticated end users | Clerk auth + org checks, server-side on every route/action (Phase-1 baseline, unchanged) |
| App → scheduler | Next.js server → gRPC `ControlService` | The authenticated server process only | Loopback bind + optional engine token (M38) |
| Scheduler ↔ transport | Engine ↔ Redis Streams | Engine + workers only | Loopback bind; no public exposure |
| Scheduler/worker ↔ store | Engine/workers ↔ Postgres | Engine + workers only | Loopback bind; local dev credentials |

The browser **never** talks to the scheduler, Redis, or Postgres directly. The
only path from a browser to execution is the authenticated Next.js server,
which resolves the org from Clerk and submits to the scheduler server-side.

## 2. Implemented controls (M38)

### 2.1 Service-to-service engine-token auth

- **Server** (`engine/app/grpc_service.cpp`): when `EVO_ENGINE_TOKEN` is set
  (non-empty), every RPC (`SubmitRun`/`CancelRun`/`GetRun`/`Health`) must carry
  `authorization: Bearer <token>` metadata matching it, else the call is
  rejected with `UNAUTHENTICATED`. When unset/empty, auth is disabled — the
  backwards-compatible default, safe because the server binds loopback only.
- **Client** (`features/workflows/lib/evo-scheduler-client.ts`): the
  server-only gRPC client reads `EVO_ENGINE_TOKEN` from its own environment and
  attaches it as call metadata. The token is server-side only.
- **Comparison is constant-time** (`engine/core/src/auth_token.cpp`):
  `constant_time_equals` scans every byte regardless of where a mismatch occurs,
  so a wrong token does not leak how many leading bytes matched via timing.
- **Bearer extraction is tolerant** (`extract_bearer`): accepts the `Bearer`
  scheme case-insensitively and trims surrounding whitespace.
- Verified by `engine/tests/auth_token_test.cpp` (17 unit tests) and
  `engine/tests/auth_integration_test.cpp` (spawns the real server with/without
  the token; asserts `UNAUTHENTICATED` on missing/wrong token, `OK` on correct
  token, and backwards-compatible acceptance when no token is configured).

### 2.2 Input size limits + identifier validation

At the gRPC trust boundary, before any durable state is mutated
(`engine/app/grpc_service.cpp` → `engine/core/src/input_limits.cpp`):

- Identifiers (`run_id`, `org_id`, `workflow_id`) must be non-empty, at most
  `kMaxIdLength` (256) bytes, and contain only `[A-Za-z0-9._-]`. This rejects
  control characters, whitespace, and path/JSON metacharacters that could
  confuse logs, Redis stream keys, or downstream stores.
- The DAG JSON payload must be at most `kMaxDagJsonBytes` (8 MiB). The gRPC
  server allows larger messages, but bounding the DAG here prevents a single
  oversized submission from consuming memory before parse.
- Validation runs **before** quota admission and before any durable write
  (trust-boundary rule: validate before mutating state).
- Verified by `engine/tests/input_limits_test.cpp` (18 tests).

### 2.3 Structured logging with secret redaction

- **C++** (`engine/core/src/log.cpp`): structured JSON logs to stderr. Any field
  whose **key** matches a secret-like pattern (`password`, `secret`, `token`,
  `credential`, `authorization`, `api_key`/`apikey`, `private_key`) has its
  **value** replaced with `[REDACTED]` before serialization. Correlation fields
  (run/node/attempt/org/worker/trace ids) are identifiers, never secret, and
  pass through untouched.
- **TypeScript** (`worker/src/logger.ts`): the worker logger promotes `key=value`
  tokens to JSON fields and redacts the same secret-like keys, plus `Bearer`
  tokens and embedded credentials in URLs.
- Redaction is **defense in depth** — callers must not pass secrets in the first
  place. Verified by `engine/tests/log_test.cpp` and `worker/src/logger.test.ts`.

### 2.4 Loopback-only network exposure

- The gRPC `ControlService` binds `127.0.0.1:50051` by default
  (`EVO_SCHEDULER_ADDR` override). It never binds `0.0.0.0` by default.
- The Prometheus `/metrics` endpoint (`engine/app/metrics_http.hpp`) binds
  `INADDR_LOOPBACK` only (`EVO_METRICS_PORT`, default 9090; `0` disables). It is
  a minimal read-only HTTP server returning the rendered metrics text.
- The Phase-2 Redis + Postgres containers bind their published ports to
  `127.0.0.1` only (`infra/phase2/docker-compose.yml`). They are never reachable
  off-machine.

### 2.5 Secret scanning in CI

`scripts/secret-scan.sh` scans every git-tracked file for secret-looking
content (AWS access key ids, private-key blocks, `*_KEY=`/`*_TOKEN=`/`*_SECRET=`
assignments with real-looking values, Bearer tokens) and fails the build on a
finding. An allowlist covers documented placeholders (`.env.example`, the
Phase-2 local docker-compose defaults, and this scan script). It runs as the
`secret-scan` CI job. It is a heuristic tuned to this repo, not a substitute for
a dedicated scanner.

### 2.6 CI requires no paid/external keys

Ordinary PR CI (`.github/workflows/ci.yml`) runs entirely on stock GitHub
runners with open-source system packages + the isolated Phase-2 Redis/Postgres
stack. It never requires Browserbase, Resend, Clerk, Neon, or any paid key:

- The Node suites skip cleanly when live services are absent; the production
  build succeeds without `SENTRY_AUTH_TOKEN` / `.env.local`.
- The distributed integration uses the synthetic `bench:*` executor in the TS
  workers — never a real browser.

## 3. Threat-model notes

Assets, threats, and the control that mitigates each.

| # | Asset / concern | Threat | Mitigation (implemented) |
| :--- | :--- | :--- | :--- |
| T1 | Scheduler control plane | An unauthenticated or misrouted client submits/cancels runs | Loopback bind by default; optional engine token rejects `UNAUTHENTICATED` (2.1) |
| T2 | Engine token | Timing side-channel leaks the token byte-by-byte | Constant-time comparison (2.1) |
| T3 | Engine token | Token leaked to a browser or into logs | Token is server-side only; never sent to the client bundle; redacted from logs (2.3) |
| T4 | Multi-tenant isolation | One org's submission interferes with / reads another's | Org id resolved server-side from Clerk (never from the browser); per-org quota admission (M36); identifiers validated (2.2) |
| T5 | Scheduler memory/CPU | Oversized or malformed DAG payload exhausts resources before parse | DAG size limit + identifier validation before any state mutation (2.2) |
| T6 | Logs / observability | Secrets (API keys, tokens, credentials) written to logs | Key-based redaction in both C++ and TS loggers (2.3) |
| T7 | Redis / Postgres | Data stores reachable off-machine | Loopback-only port bindings in the local stack (2.4) |
| T8 | Metrics endpoint | Metrics scraped by an unauthenticated remote party | Loopback-only bind; read-only; no secrets in metric labels (2.4) |
| T9 | Repository | A secret committed to git | `secret-scan.sh` CI gate + `.env.local` never committed (2.5) |
| T10 | CI | Paid/external keys required (cost + leak surface) | CI uses only open-source packages + synthetic executors (2.6) |

## 4. Explicit no-go items (binding)

- **Do not expose the engine token to the browser.** It is server/worker only.
- **Do not publish private Redis/Postgres ports** in production defaults. The
  local stack binds loopback only; production must keep them private.
- **Do not commit secrets** or `.env.local` values. Use `.env.example` for
  documented placeholders.
- **Do not print secret values in logs.** Redaction is defense in depth; callers
  must not pass secrets to the logger.
- **Do not ask a user to paste a secret into chat.** Reference the env-var name
  and where to set it instead.

## 5. What is NOT covered (explicit, honest)

These are known limitations, not implemented controls. They are acceptable for
the local/dev Phase-2 stack and must be addressed before any non-loopback
production deployment:

- **Redis has no auth by default.** The local stack relies on loopback binding.
  A production deployment must enable `requirepass`/ACLs and TLS.
- **Postgres uses local dev credentials.** The committed docker-compose
  credentials are NON-secret local defaults bound to 127.0.0.1. Production must
  use a real secret manager and per-service roles.
- **The engine token is a single shared secret**, not per-org. Org isolation is
  enforced by the app's Clerk auth + per-org quota, not by the engine token. The
  token authenticates the *service*, not the *tenant*.
- **No TLS on the gRPC channel.** Loopback today; production needs mTLS or a
  service mesh before any non-loopback exposure.
- **Secret scanning is heuristic.** It catches the high-signal patterns for this
  repo but is not a dedicated secret scanner.
- **Metrics carry no auth.** Acceptable on loopback; a production scrape target
  must be firewalled or authenticated.

## 6. Verification summary

| Control | Evidence |
| :--- | :--- |
| Engine-token auth (unit) | `engine/tests/auth_token_test.cpp` — 17 tests pass |
| Engine-token auth (integration) | `engine/tests/auth_integration_test.cpp` — negative + positive + backwards-compat pass |
| Input limits / id validation | `engine/tests/input_limits_test.cpp` — 18 tests pass |
| Log redaction (C++) | `engine/tests/log_test.cpp` — pass |
| Log redaction (TS) | `worker/src/logger.test.ts` — 6 tests pass |
| Secret scan | `scripts/secret-scan.sh` — clean on tree; patterns fire on known-bad samples |
| CI gates | `.github/workflows/ci.yml` — secret-scan, node, gcc/clang core, asan, tsan, distributed, bench-smoke |
