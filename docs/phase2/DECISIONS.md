# Phase 2 — Decision Log

Decisions that shape the Phase-2 implementation. Each entry records the decision,
the rationale, and its consequences. This is NOT a plan — it records what was
decided, why, and what depends on it. Entries are grouped by milestone; new
decisions are appended below existing ones (chronological within a milestone).

All decisions below were made while reconciling the master implementation
prompt against the actual checked-out repository at the recorded SHAs.

---

## M01 — Reconcile the real Phase-1 source and archive

| # | Decision | Rationale | Consequences |
|---|----------|-----------|--------------|
| 1.1 | Treat the checked-out working tree as the source of truth over the master prompt's described archive. | The prompt itself states the working tree is source of truth and the archive may differ. | `phase2` baseline is `5005768` (main), not the archive SHA. All M02+ gates measured from there. |
| 1.2 | Phase-2 work lives on a dedicated `phase2` branch created only after the baseline is green. | Keeps Phase-1 history untouched; lets Phase-2 land as one logical branch. | Single `phase2(mNN)` commit per milestone; no rewrite of Phase-1 history. |
| 1.3 | Document the exact environment gap (Docker not installed) rather than installing globals with `sudo`. | AGENTS / prompt forbid silent global installs; Docker is only needed at M18. | M01–M17 proceed without it. M18 will be blocked until Docker (or a Postgres alternative) is available. |

## M02 — Certify and freeze the Phase-1 behavioral baseline

| # | Decision | Rationale | Consequences |
|---|----------|-----------|--------------|
| 2.1 | Freeze Phase-1 as the immutable regression gate: `npm test` 28/28, typecheck/lint/build green. | The prompt makes Phase-1 a behavioral compatibility baseline, not a rewrite target. | Every TS-touching milestone must re-run these gates; a regression blocks the milestone. |
| 2.2 | Keep the legacy Trigger.dev engine as the default execution path; Evo is opt-in only. | "Legacy Trigger.dev mode must remain functional until the final compatibility decision." | The Evo path must not disturb the existing run workflow; integration happens behind a fail-closed feature flag (see 3.1). |
| 2.3 | Record manual-only behaviors (final screenshot, live-view gate, replay) in `PHASE1_BASELINE.md`. | The lifecycle test suite covers them, but the doc pins the exact invariant for future comparison. | M10/M25 can reference these as parity targets. |

## M03 — Architecture, invariants, and progress scaffold

| # | Decision | Rationale | Consequences |
|---|----------|-----------|--------------|
| 3.1 | Evo engine is gated behind a fail-closed feature flag; default remains legacy. | Prevents a partially-built engine from changing production behavior. | UI/engine wiring (M27) must check the flag; `false` → legacy path. |
| 3.2 | C++ orchestrator owns dependency-aware scheduling, concurrency, fairness, cancellation, and metrics. | C++ justifies the hard problems (scheduling, sync, accounting) per §2.1. | Node *execution* stays in TypeScript; only orchestration is C++. |
| 3.3 | Workers reuse the existing TS node executors; no C++ reimplementation of Stagehand/Resend. | "Do not rewrite Stagehand in C++ — only orchestration." | M24 wires workers to existing executors, not a C++ port. |
| 3.4 | Redis Streams is the task transport (consumer groups, pending entries, explicit ACK). | Provides durable append-only task queues with redelivery primitives; project still owns leases/retries/idempotency. | No claim of exactly-once; project implements app-level idempotency (M33). |
| 3.5 | Postgres/Neon remains the durable audit DB; Drizzle is the schema/migration authority. | Phase-2 tables are additive migrations; C++ reads/writes via parameterized SQL. | M20 schema is additive; no destructive production migrations. |
| 3.6 | Browser affinity = capacity-1 resource group per affinity key; same-session browser nodes never run in parallel. | "Browser-mutating nodes that share one workflow browser session must not execute concurrently against the same page/context." | M12 encodes affinity as a resource class; M25 pins a browser resource group to the owning worker. |
| 3.7 | No dependency manager for the pure-stdlib core (M04–M15); defer vcpkg/FetchContent to when gRPC/Redis/Postgres deps land (M16+). | vcpkg is not installed; core needs only the standard library. | Avoids over-fetching a dependency tree early; gRPC/Redis/Postgres decisions deferred. |
| 3.8 | Adopt the prompt's state machines verbatim (run/node/attempt) and the metric definitions (§14). | Ensures evidence-grade, comparable numbers across milestones. | M13 instrumentation keys off these definitions; M39 reports use them. |

## M04 — Bootstrap the reproducible C++20 toolchain

| # | Decision | Rationale | Consequences |
|---|----------|-----------|--------------|
| 4.1 | CMake 3.25+ required; C++20 required; `-Wall -Wextra -Wpedantic -Werror` on project code. | Strong-warning policy from the C++ engineering contract. | Every commit must compile clean under `-Werror`. |
| 4.2 | Bake `git rev-parse --short HEAD` into the binary via `EVO_BUILD_COMMIT`. | Evidence-grade builds must be attributable to a SHA. | `evo-smoke` prints the commit; benchmarks record it. |
| 4.3 | Use `std::jthread`/`std::stop_token` + `std::chrono::steady_clock`; never detach threads; no busy-spin. | Engine contract §8. | Worker pool (M09) and cancellation (M11) build directly on these. |

## M05 — Implement the canonical C++ DAG model

| # | Decision | Rationale | Consequences |
|---|----------|-----------|--------------|
| 5.1 | Define `BuildResult` *after* `class Dag` so `std::optional<Dag>` is a complete type. | A forward declaration is insufficient for `std::optional<T>` whose destructor accesses `T`. The prior session's forward-declaration caused the build break. | Build compiles under `-Werror`; `result.ok()`/`result.dag`/`result.errors` are usable. |
| 5.2 | Make the built `Dag` immutable and shareable by const reference across threads. | The engine is concurrent; immutable graphs need no locking to read. | M06/M09 can hand `const Dag*` to worker threads safely. |
| 5.3 | Stricter validation than Phase-1 `validateGraph`: reject empty/duplicate ids, missing endpoints, self-loops, duplicate edges, and unreachable-from-trigger nodes. | Phase-1 silently skips disconnected nodes at run time; the engine needs a deterministic "executability" contract for scheduling. | The TS `validate-graph.ts` is NOT modified (Phase-1 preservation checklist). The Evo path simply won't schedule graphs that violate the contract. |
| 5.4 | Canonical JSON shape (no React Flow fields) with deterministic node/edge ordering. | Scheduler core must be independent of UI field names; deterministic serialization enables byte-identical round-trip tests and reproducible benchmarks. | `to_json`/`from_json_string` sorted by id; M15 benchmark workloads serialize graphs deterministically. |
| 5.5 | Deterministic topo order: Kahn's algorithm with lexicographic tie-break. | Tests/benchmarks must be reproducible; nondeterminism would make M15 speedup claims uninterpretable. | `topo_order()` is stable across runs given identical input. |
| 5.6 | Keep M04–M15 purely standard-library; no gRPC/Redis/Postgres here. | M05 is the graph model, not transport; adding deps early violates the milestone no-go list. | JSON parser is hand-written (no third-party JSON lib). |
