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

## M06 — Deterministic sequential reference scheduler

| # | Decision | Rationale | Consequences |
|---|----------|-----------|--------------|
| 6.1 | Sequential scheduler walks `topo_order()` and halts on first failure. | Establishes a deterministic correctness baseline that the concurrent scheduler (M10) must match node-for-node. | M15 compares sequential vs concurrent on identical DAGs. |
| 6.2 | `TaskResult{bool completed; std::string output}` and `TaskFn = std::function<TaskResult(const NodeSpec&)>`. | Minimal interface; no exceptions cross the scheduler boundary. | M10 extends to `ConcurrentTaskFn` adding `std::stop_token`. |
| 6.3 | `RunLog` records per-node start/finish in `steady_clock`. | In-process latency must be monotonic; wall-clock is reserved for process boundaries (M16/M17). | Metrics (M13) derive durations from steady_clock only. |

## M07 — Scheduler state machines and dependency counters

| # | Decision | Rationale | Consequences |
|---|----------|-----------|--------------|
| 7.1 | Every public `StateMachine` method takes `std::lock_guard lock(mu_)`; results returned by value. | The state machine is shared between the dispatcher and worker threads; coarse locking is correct-first, optimized later if benchmarked. | No data races under TSan; M10 reuses it safely. |
| 7.2 | Dependency counters decrement on upstream success; a node becomes READY only when its counter hits zero (fan-in invariant). | Encodes the DAG join semantics without re-scanning edges. | Fan-in nodes (diamond join) dispatch exactly once. |
| 7.3 | `finalize_run()` derives terminal RunState from node outcomes; idempotent completion. | A node completing twice (duplicate delivery) must not corrupt state. | M33 idempotency builds on this guarantee. |
| 7.4 | `mark_canceled_transitive` called with lock held; cancellation is cooperative. | Cancellation races completion; holding the lock serializes the two. | M11 wires `stop_token` through to tasks. |

## M08 — Thread-safe blocking ready queue

| # | Decision | Rationale | Consequences |
|---|----------|-----------|--------------|
| 8.1 | Blocking FIFO with `mutex` + `condition_variable_any` + `stop_token`; `pop(stop_token)` returns on close or stop. | Workers must block without busy-spin and wake cleanly on shutdown. | M09 pool drains the queue; M11 shutdown is graceful. |
| 8.2 | Bounded capacity with backpressure on `push`. | Prevents unbounded memory growth when producers outrun consumers. | M36 quota/backpressure reuses this primitive. |

## M09 — Bounded std::jthread worker pool

| # | Decision | Rationale | Consequences |
|---|----------|-----------|--------------|
| 9.1 | Fixed-size pool of `std::jthread` workers; `submit`/`drain`/`stop`; exceptions captured into a queue, never thrown across the boundary. | jthread guarantees join on destruction; captured exceptions keep the pool alive. | `take_exceptions()` surfaces failures to tests/M13 metrics. |
| 9.2 | No detached threads; pool destructor joins all workers. | Engine contract §8 forbids detached threads. | Clean shutdown verified by TSan. |

## M10 — Local concurrent dependency-aware DAG scheduler

| # | Decision | Rationale | Consequences |
|---|----------|-----------|--------------|
| 10.1 | `ConcurrentScheduler` composes StateMachine (M07) + ReadyQueue (M08) + ThreadPool (M09); dispatcher loop moves READY→DISPATCHED→RUNNING. | Reuses proven thread-safe primitives instead of a monolithic lock. | Each component is independently tested. |
| 10.2 | `ConcurrentTaskFn = std::function<TaskResult(const NodeSpec&, std::stop_token)>`. | Tasks must observe cancellation cooperatively. | M11 cooperative variants plug in directly. |
| 10.3 | `ConcurrentConfig{num_workers, ready_queue_capacity, run_id}`. | Deterministic, configurable concurrency for benchmarks. | M15 sweeps worker counts. |

## M11 — Cooperative cancellation and graceful shutdown

| # | Decision | Rationale | Consequences |
|---|----------|-----------|--------------|
| 11.1 | Cancellation propagates via `std::stop_token` checked inside tasks (`sleep_task_cooperative`/`burn_task_cooperative`). | Cooperative cancellation is the only safe model; forced kill would leak browser sessions. | Long tasks must poll the token. |
| 11.2 | `cancel()` requests stop; in-flight tasks finish their current step; queued nodes become CANCELED. | Matches the run-level cancel semantics the UI expects (M30). | CancelRun (M17) maps to this. |

## M12 — Resource classes and browser affinity policy

| # | Decision | Rationale | Consequences |
|---|----------|-----------|--------------|
| 12.1 | Browser affinity = capacity-1 resource group keyed by affinity key; same-session browser nodes never run in parallel. | Browser-mutating nodes sharing one session must serialize (decision 3.6). | M25 pins a browser resource group to the owning worker. |
| 12.2 | Resource class is a first-class envelope field, not inferred from node type. | Keeps scheduling policy data-driven and testable. | Proto (M16) carries resource_class/affinity_key. |

## M13 — Evidence-grade timestamps and counters

| # | Decision | Rationale | Consequences |
|---|----------|-----------|--------------|
| 13.1 | In-process scheduling latency uses `steady_clock`; wall-clock UTC only at process boundaries. | steady_clock is monotonic and immune to NTP jumps; wall-clock is for cross-process correlation. | Proto Timestamps (M16) are wall-clock; internal metrics are steady. |
| 13.2 | Metrics are counters/histograms keyed by the §14 definitions, not ad-hoc timers. | Evidence-grade, comparable numbers across milestones. | M39 reports use these exact keys. |

## M14 — Sanitizers and concurrency stress

| # | Decision | Rationale | Consequences |
|---|----------|-----------|--------------|
| 14.1 | Maintain three build trees: Release, ASan+UBSan (`build-asan`), TSan (`build-tsan`). | Races/UB must be caught before they reach distributed code. | CI gate (M38) runs all three. |
| 14.2 | Stress test hammers fan-in/fan-out + cancel races. | The hardest correctness cases are concurrency races. | TSan 12/12 green is a standing gate. |

## M15 — First benchmark corpus and sequential-vs-concurrent evidence

| # | Decision | Rationale | Consequences |
|---|----------|-----------|--------------|
| 15.1 | Synthetic workloads `bench:sleep`/`bench:burn` generated by seed (`generate_workload`); never added to the TS node registry. | Benchmarks must be reproducible and must not pollute the product node catalog. | Results are labeled synthetic, not production. |
| 15.2 | Results identify workload, build mode, hardware, sample count, and commit before being evidence-grade. | Master prompt performance-evidence rule. | Raw data stored under `engine/benchmarks/results/` (gitignored). |

## M16 — Shared Protobuf/gRPC execution contract

| # | Decision | Rationale | Consequences |
|---|----------|-----------|--------------|
| 16.1 | Proto package `evo.execution.v1`; ControlService = SubmitRun/CancelRun/GetRun/Health. | Versioned namespace allows future breaking changes under v2. | TS/C++ bindings generated from one source of truth. |
| 16.2 | `node_payload_json` is opaque to transport and carries no secrets by contract; resource class/affinity are first-class fields. | Transport must not need to understand node internals; secrets never cross the wire in payloads. | M24 workers parse payloads; M36 enforces no-secret rule. |
| 16.3 | Generated code in a separate `evo_proto` target with protobuf headers as SYSTEM includes. | `-Werror` on project code must not flag third-party generated warnings. | No `-Werror` on third-party headers. |
| 16.4 | TS bindings use the already-installed `protobufjs` runtime; typed codegen deferred. | Avoids a new dependency before it is needed. | M17+ may add `ts-proto` if typed stubs are required. |

## M17 — C++ scheduler service over gRPC

| # | Decision | Rationale | Consequences |
|---|----------|-----------|--------------|
| 17.1 | Heap-stable `std::map<std::string, std::unique_ptr<ActiveRun>>` for active runs. | `unordered_map` rehash would dangle pointers held by the runner thread. | No use-after-free under concurrent submit/complete. |
| 17.2 | Runner thread publishes log/outcome/done under `runs_mu_`. | Serializes writer (runner) and readers (GetRun) on ActiveRun state. | No data race on ActiveRun::log (TSan clean). |
| 17.3 | `sigwait`-based graceful shutdown thread calling `server->Shutdown()`. | Signal handlers must not run gRPC code inline; a dedicated thread does the shutdown. | SIGTERM drains cleanly. |
| 17.4 | Default bind `127.0.0.1:50051` (not `0.0.0.0`). | The scheduler service must not be exposed off-machine by default. | Production exposure is an explicit later decision. |
| 17.5 | Manual `Timestamp` conversion (protobuf 35 removed `TimePointToTimestamp`); explicit `duration_cast<system_clock::duration>` in `to_wall`. | Homebrew protobuf 35 dropped the helper; libc++ steady/system clock arithmetic needs an explicit cast. | Compiles clean under `-Werror`. |
| 17.6 | gRPC linked via pkg-config `grpc++` for the complete absl link set; `find_package(PkgConfig)` added before gRPC detection. | Homebrew grpc 1.83 requires the full absl transitive set; a bare `find_package(gRPC)` under-links `_grpc_slice_*`. | Link succeeds on arm64 macOS. |

## M18 — Isolated local Redis + PostgreSQL infrastructure

| # | Decision | Rationale | Consequences |
|---|----------|-----------|--------------|
| 18.1 | Compose project `evo-phase2` with loopback-only port bindings (`127.0.0.1:6390` redis, `127.0.0.1:5433` postgres). | The dev stack must never be reachable off-machine and must never collide with the app's Neon connection or a local default Postgres on 5432. | Integration tests connect to fixed local endpoints; no accidental remote exposure. |
| 18.2 | Credentials are documented non-secret local defaults, overridable via `EVO_PHASE2_*` env vars. | M18 requires committed non-secret dev defaults; a separate namespace guarantees no overlap with `DATABASE_URL`/`DATABASE_URL_UNPOOLED`. | Reset scripts can never be pointed at Neon by construction. |
| 18.3 | `reset.sh` destroys only volumes owned by the `evo-phase2` compose project. | Destructive operations must be scoped to local throwaway data. | Wipe is reproducible and safe; `down.sh` preserves volumes. |
| 18.4 | Redis 7.4-alpine + Postgres 16-alpine pinned by tag. | Reproducible infra across machines; alpine keeps images small. | Version bumps are explicit compose edits. |
| 18.5 | Phase-2 schema will be portable SQL (no local-only extensions) so the same additive Drizzle migrations apply to local Postgres now and Neon later. | M19+ must run against this local stack, then Neon after explicit human approval. | No divergence between local and remote schema. |

## M19 — Additive Phase-2 Drizzle schema and migrations

| # | Decision | Rationale | Consequences |
|---|----------|-----------|--------------|
| 19.1 | Five additive tables: `workflow_versions`, `workflow_runs`, `node_runs`, `task_attempts`, `idempotency_records`; Phase-1 tables untouched. | Durable engine-neutral audit state without breaking Phase 1 (M19 objective). | M20+ builds on these; Phase-1 app never reads them yet. |
| 19.2 | `engine` discriminator on `workflow_runs` (`legacy` default, `evo` opt-in). | Legacy Trigger.dev and Evo runs must coexist in one audit table. | M27 engine abstraction filters by this column. |
| 19.3 | Unique constraints as invariant guards: `(workflow_id, version_number)`, `(run_id, node_id)`, `(node_run_id, attempt_number)`, idempotency `key` PK. | Duplicate delivery must be rejected by the DB, not by application luck. | M33 idempotency resolves conflicts by reading the existing row. |
| 19.4 | Committed `0000_phase1_baseline.sql` even though Phase-1 was push-only. | Migration history must be honest and replayable from scratch on the local stack. | Fresh local Postgres reproduces the full schema. |
| 19.5 | Local migrations applied via `scripts/phase2/migrate-local.sh` (psql + `--single-transaction`), not drizzle-kit migrate. | drizzle-kit's migrator uses `@neondatabase/serverless`, which cannot open plain TCP to local Postgres. | Same committed SQL files are the source of truth for both local and (later, human-approved) Neon application. |
| 19.6 | All Phase-2 timestamps are wall-clock UTC (`now()`); steady_clock stays in-process. | Consistent with M13/M16 clock policy. | Cross-process correlation uses one clock domain. |

## M20 — Immutable workflow versions and optimistic concurrency

| # | Decision | Rationale | Consequences |
|---|----------|-----------|--------------|
| 20.1 | Version snapshot created at run-submission, after the canonical save, fail-open for legacy. | Phase-2 tables may not exist on Neon yet (migration needs human approval); a snapshot failure must never block a Phase-1 run. | Once Neon is migrated, snapshots become durable for every run. |
| 20.2 | Concurrency guard = unique `(workflow_id, version_number)` + bounded retry, not transactions. | The pooled neon-http driver offers no multi-statement transactions. | Safe under concurrent snapshot creation; verified by the 3-way race test. |
| 20.3 | Identical graphs dedupe by canonical sha256 hash (nodes/edges sorted). | Rerun-without-edit should reference the same immutable snapshot, not create duplicates. | `graph_hash` is the dedupe key; canonical ordering makes it stable. |
| 20.4 | Optimistic save rejects `expectedVersion` behind current with a clear conflict error; omitting the token keeps Phase-1 behavior. | Detect stale updates instead of silently overwriting; keep the editor usable with a clear conflict path. | M27 UI can surface the conflict; legacy callers unaffected. |
| 20.5 | Versioning functions accept an injectable `db` (default `getDb()`). | Integration tests must run against local Postgres via node-postgres, since neon-http cannot open plain TCP. | Added `pg` dependency (also needed by M23+ TS workers). |
| 20.6 | Version rows are never mutated after creation. | Runs must replay the exact approved graph (M20 no-go). | Re-runs reference old version ids; edits create new versions. |

## M21 — Redis Streams transport in the C++ scheduler

| # | Decision | Rationale | Consequences |
|---|----------|-----------|--------------|
| 21.1 | `TaskTransport` abstraction + `InMemoryTransport` fake in scheduler-core; `RedisTransport` in a separate `evo_redis_transport` target. | Scheduler-core tests must not need live Redis (M21 step 1); keeps the core dependency-free. | Fake mirrors Redis semantics; production swaps in RedisTransport. |
| 21.2 | hiredis synchronous API, single mutex-guarded `redisContext`, bounded exponential-backoff reconnect (50ms base, 2s cap, 5 retries/op). | hiredis contexts are not thread-safe; bounded backoff satisfies "retryable connection with bounded backoff". | Exhausted retries surface as failure; caller applies its own policy. |
| 21.3 | Self-contained arm64 static hiredis via `engine/third_party/build-hiredis.sh` (gitignored prefix). | Machine `brew` on PATH is the Intel prefix (x86_64) but the engine builds arm64 against `/opt/homebrew`; ARM brew was blocked by an unrelated untrusted-tap policy. | No system/brew-state changes; reproducible rebuild script committed. |
| 21.4 | At-least-once delivery: XREADGROUP marks pending; only the worker XACKs; late/duplicate ack harmless. | M21 no-go: scheduler must not ack on behalf of workers; Redis is not exactly-once (§2.3). | Duplicate delivery expected; app-level dedupe is M33. |
| 21.5 | Transport does NOT dedupe duplicate payloads; duplicate publish gets a new stream id. | Dedupe is by envelope identity (run_id,node_id,attempt), which is application logic, not transport logic. | M33 idempotency keys handle duplicates. |
| 21.6 | Stream keys namespaced via `task/result/control_stream_key(env_prefix)`. | Multiple stacks can share one Redis without collision (M21 step 3). | Callers pass an explicit env/project prefix. |
| 21.7 | hiredis headers SYSTEM + `-Wno-c99-extensions` (sds.h flexible arrays). | Third-party C99 headers trip `-Wpedantic -Werror` in C++ mode. | Project code stays `-Werror`; only third-party noise suppressed. |

## M22 — Task/result envelope semantics and event transport

| # | Decision | Rationale | Consequences |
|---|----------|-----------|--------------|
| 22.1 | Identity = (run_id,node_id) for a task, (run_id,node_id,attempt) for an attempt; transport message ids are NOT identity. | Dedupe/late-result rules must key off logical identity, not Redis stream ids. | M33 idempotency and M31 leases use these keys. |
| 22.2 | Additive proto: `ErrorClass` enum + ResultEnvelope fields 11–14 (`error_class`, `optional retryable`, `worker_id`, `started_at`). No field numbers reused. | Protobuf additive-evolution rule; `optional` gives explicit presence for the retry hint. | Old decoders ignore new fields; wire-compatible. |
| 22.3 | `retryable` is an advisory HINT; `should_consider_retry` applies a policy floor (permanent/canceled never retry; transient/exhausted default retry unless explicitly false). | The worker advises; the scheduler's retry policy (M32) decides. | Prevents a worker from forcing retries on permanent errors. |
| 22.4 | Late results are IGNORED (older attempt, or node already terminal) — never overwrite newer logical state. | A slow/crashed worker's stale result must not corrupt a newer attempt. | `is_late_result` is the single guard. |
| 22.5 | `kMaxEnvelopeBytes` = 256 KiB; oversized/malformed envelopes rejected before durable mutation. | Bounds stream/worker memory; validates at the trust boundary. | Callers quarantine/log rejected payloads. |
| 22.6 | Cross-language compatibility proven via committed golden byte fixtures: C++ generator + TS protobufjs decode + byte-identical re-encode. | Proves C++ <-> TS encoders agree on the wire format, not just field names. | Fixtures regenerated on proto change; byte-stability self-checked. |
| 22.7 | `evo_envelope` is a separate target linking evo_proto (not transport). | Semantics are testable without Redis; keeps scheduler-core dependency-free. | Clean layering: transport (M21) carries, envelope (M22) enforces. |

## M23 — TypeScript distributed worker service

| # | Decision | Rationale | Consequences |
|---|----------|-----------|--------------|
| 23.1 | Standalone worker process in `worker/`, separate from Next.js and Trigger.dev. | M23 step 1; workers must scale independently of the web app. | Compose `--scale worker=N` scales workers without touching the app. |
| 23.2 | Durable-handoff rule: publish ResultEnvelope BEFORE acking the task; if publish fails, leave the task unacked for redelivery. | M23 step 7; prevents losing a completed result by acking too early. | Duplicate results possible; deduped scheduler-side by attempt id (M22). |
| 23.3 | Graceful shutdown drains in-flight tasks up to `drainTimeoutMs`; unfinished tasks stay pending (unacked) for redelivery. | M23 step 4 lease semantics; a slow worker is not silently abandoned. | M31 adds explicit lease tracking on top. |
| 23.4 | Pluggable `TaskExecutor` interface; M23 ships a synthetic executor, M24 wires real node executors behind the same interface. | Smallest interface that satisfies M23 and leaves room for M24. | Synthetic types never enter the product node registry. |
| 23.5 | Malformed envelopes are quarantined (logged + acked) rather than left pending forever. | A poison message must not block the consumer group. | Quarantine is observable via logs; result is never published. |
| 23.6 | ioredis 6.0.0 as the TS Redis client; `RedisStreamsClient` mirrors the C++ RedisTransport command-for-command. | One wire protocol across C++ scheduler and TS workers. | Cross-language behavior parity is testable. |
| 23.7 | Worker compose service is profile-gated (`--profile worker`); default stack unchanged. | M18 stack behavior must not regress; workers are opt-in. | `up.sh`/`down.sh`/`reset.sh` unaffected. |

## M24 — Reuse existing node executors in distributed workers

| # | Decision | Rationale | Consequences |
|---|----------|-----------|--------------|
| 24.1 | Worker-side adapter wraps the EXISTING `nodeExecutors` + `interpolate()`; no reimplementation. | Master prompt: do not duplicate executors; reuse Phase-1 logic. | One executor implementation across legacy and Evo paths. |
| 24.2 | Version snapshot + predecessor outputs come from injectable loaders. | M24 tests use in-memory fakes; M26 plugs durable Postgres loaders. | Clean seam; adapter is testable without a DB. |
| 24.3 | Email side effect mocked via `createEmailTestSink` (records calls, returns real `{id}` shape). | Automated distributed tests must never send real email (M24 step 7). | Interpolation parity holds because output shape matches. |
| 24.4 | Output-compatibility test asserts byte-identical output between legacy and worker paths for deterministic nodes. | Proves the adapter preserves Phase-1 execution semantics (M24 step 9). | Regression guard for executor reuse. |
| 24.5 | Secrets (RESEND_API_KEY, BROWSERBASE_API_KEY) read from worker env by executors; never carried in the envelope. | M22 payload rule; server/worker-only credentials. | Envelopes stay secret-free across the wire. |
| 24.6 | Trigger nodes complete with `{skipped:true}` and no work (legacy parity). | run-workflow.ts marks trigger nodes done without an executor. | Consistent run semantics across engines. |

## M25 — Distributed browser session ownership and live-view parity

| # | Decision | Rationale | Consequences |
|---|----------|-----------|--------------|
| 25.1 | Worker-local `BrowserSessionManager` keyed by affinity key (default `run:<runId>`), one Stagehand session per key, opened lazily and reused by every same-key task. | M25 steps 1–2, 7: the run's recording must span the whole flow and same-key tasks must stay on the owning worker while the session is live. | Cross-worker pinning comes from the M12 capacity-1 affinity policy; the manager enforces one-session-per-key locally. |
| 25.2 | Session id published via `onSessionOpened` callback as soon as it exists; durable engine-neutral event write deferred to M26/M27. | M25 step 3 without implementing later milestones early; callback is the smallest interface that leaves room. | Session id is observable in worker logs today; durable state lands with the result-persistence loop. |
| 25.3 | Live-view handshake reuses the Phase-1 pattern: poll `isLiveViewConnected` (injectable) up to `liveViewWaitMs` before the first browser action, once per session, then proceed anyway. | M25 step 4; an unwatched run must never hang (legacy LIVE_VIEW_WAIT_MS semantics). | Handshake is testable with a fake; wall-clock `Date.now()` deadline matches legacy. |
| 25.4 | Final screenshot (jpeg q70 → base64) captured before close and surfaced via `onFinalScreenshot`; close is idempotent. | M25 step 6 + legacy close semantics; results popup parity. | Persistence of the artifact is M26's durable write; M25 only produces it. |
| 25.5 | DOM highlight helpers reused unchanged — executors already call them on the session's pages. | M25 step 5; highlight-element.ts is executor-level, session-agnostic. | No duplication; live-view highlights work identically on worker sessions. |
| 25.6 | Worker `onShutdown` hook closes all live sessions after the in-flight drain; errors logged, never fatal. | M25 step 8; graceful shutdown must complete; Browserbase idle timeout is the backstop. | Sessions never leak on clean shutdown. |
| 25.7 | Worker-crash semantics documented, NOT implemented: a crashed worker's Browserbase session leaks until Browserbase idle timeout; recovery is M34. | M25 step 9: do not pretend the session survives yet. | Explicit known limitation; no false recovery claims. |
| 25.8 | `getStagehand` adapter option takes the `TaskEnvelopeView` so the manager keys by the task's affinity key; missing session provider fails the node permanently with a clear error. | Enables per-task affinity routing; a browser node must fail fast, not hang, when no session is available. | Adapter stays testable; browser-less workers reject browser tasks cleanly. |
| 25.9 | `BROWSERBASE_API_KEY` read from worker process env only; missing key fails browser tasks with a clear error instead of crashing the worker. | Credentials stay server/worker-only and never travel in envelopes (M22 payload rule). | No secrets on the wire; fail-closed for browser work. |

## M26 — Persist results and unlock dependencies through the distributed loop

| # | Decision | Rationale | Consequences |
|---|----------|-----------|--------------|
| 26.1 | `RunStore` interface + `InMemoryRunStore` (core) + `PgRunStore` (engine/pg, libpq). | M26 step 3 needs durable persistence; the interface keeps the loop testable without a DB and lets Postgres stay the single durable authority. | One persistence contract; Drizzle migrations remain the DDL authority — the store never creates tables. |
| 26.2 | `PgRunStore` uses parameterized SQL only (PQexecParams); idempotency via `ON CONFLICT DO NOTHING`; at-most-once completion via conditional `UPDATE ... WHERE status NOT IN (terminal)` + row-count check. | M26 steps 3–4: safe persistence, duplicate-safe creation, and at-most-once terminal application enforced by the database, not just app logic. | SQL-injection probe test proves values are data; duplicate success/failure affects 0 rows and reports not-applied. |
| 26.3 | Successor dependency counters unlock ONLY after the store applied the terminal success (`complete_node_run` returned true). | M26 step 5: durable-before-unlock. A duplicate successful result can never decrement successors twice (no-go #1). | Unlock is a function of durable state, not of message arrival. |
| 26.4 | Result application order: validate identity → require node RUNNING (applicability) → attempt-id dedupe → late-result rule → persist → unlock. Dedupe key consumed only for applicable results. | Prevents a premature/duplicate result from consuming the dedupe slot before the legitimate one arrives; late results never overwrite newer state (no-go #2). | Premature results are ignored without side effects; the real result still applies. |
| 26.5 | Attempt row recorded durably BEFORE the task envelope is published; publish failure fails the node (no silent re-publish). | Crash-after-publish still leaves an audit record; M26 does not implement retry (M32), so a failed publish is explicit, not hidden. | Durable-before-dispatch ordering; retry/backoff arrives in M32. |
| 26.6 | Normalized run events (JSON) published on `<prefix>:events` + in-process callback; event stream is best-effort (store + callback are authoritative). | M26 step 7: UI consumers get a stable event contract now; M28 wires the frontend fan-out. | Transport failure on events never fails the run. |
| 26.7 | `DistributedRunLoop` is single-threaded; cancel()/stop() are the only cross-thread entry points (atomics + stop_token on blocking reads). | Simplest correct ownership model for M26; no busy-spin; shutdown honored mid-wait. | Multi-run/multi-thread orchestration is a later milestone; the loop composes per run. |
| 26.8 | Worker `readGroup` switched to ioredis `xreadgroupBuffer` (binary-safe). | The default string reply path UTF-8-transcoded payload bytes ≥ 0x80, corrupting protobuf envelopes carrying wall-clock Timestamps; M23's ASCII-only envelopes masked it, the M26 E2E exposed it. | Wire format is now byte-exact C++↔TS for all envelopes; regression-covered by the E2E. |
| 26.9 | E2E duplicate injection uses a FAITHFUL copy of the real result (same output shape + worker-prefixed id). | Tests at-most-once application + dedupe (M26 scope); a forged payload would test lease/ownership validation, which is M31/M33. | The E2E proves the M26 invariant without claiming later-milestone behavior. |
| 26.10 | `ensure_group` gained a `start_id` param ("$" default, "0" replay) on both C++ and TS transports. | The E2E replays the event stream from the beginning; workers keep "$" semantics. Additive, backwards compatible. | One transport contract across languages with explicit cursor choice. |

## M27 — Next.js execution-engine abstraction and feature flag

| # | Decision | Rationale | Consequences |
|---|----------|-----------|--------------|
| 27.1 | Engine-neutral `ExecutionEngineAdapter` interface (startRun/cancelRun/getRunStatus) + `EngineRunHandle` returned to the UI boundary. | M27 steps 1, 7: one contract both engines satisfy; the UI never sees engine internals. | New engines plug in behind the same interface; M28 adds event fan-out on top. |
| 27.2 | Server-only flag `EXECUTION_ENGINE=legacy\|evo`, fail-closed: only exact "evo" (trimmed, case-insensitive) selects Evo. | M27 step 4 + no-go "do not silently change Phase-1 default behavior": legacy stays the default; a typo can never flip production to Evo. | Opt-in Evo path; flag read server-side only. |
| 27.3 | Legacy adapter wraps the EXISTING Trigger.dev trigger/cancel calls unchanged; run id = Trigger.dev run id. | M27 step 2 + Phase-1 preservation: live-view/cancel/replay/results keep working with zero behavior change. | Legacy regression test pins the exact pre-M27 call shape. |
| 27.4 | Evo adapter converts the React Flow graph to canonical DAG JSON (sorted nodes/edges, trigger/action kinds, from/to edges) and submits a client-generated engine-neutral run id over gRPC. | M27 step 3; the C++ `Dag::from_json` contract is the trust boundary; no React Flow UI state or secrets cross it. | Canonical conversion is pure + unit-tested; gRPC client is injectable. |
| 27.5 | Real gRPC client (`@grpc/grpc-js` + `@grpc/proto-loader`) loaded lazily via dynamic import; `serverExternalPackages: ["@grpc/grpc-js"]`. | Keeps the gRPC stack out of the legacy path and unit tests; grpc-js needs native require (per bundled Next.js docs). | Production build validates the bundling config. |
| 27.6 | Engine-neutral run row (workflow_runs, `engine` discriminator) created BEFORE Evo submission; idempotent on runId (PK conflict → read existing). Legacy run row is best-effort (fail-open). | M27 step 6; Evo executes an immutable version so the durable record must exist first; legacy must never depend on Phase-2 tables. | Cancel routing resolves the owning engine via `getRunEngine` (undefined → legacy). |
| 27.7 | Evo workflow-version snapshot is REQUIRED (fail-closed); legacy snapshot stays fail-open. | Evo runs execute the immutable version — without one there is nothing safe to submit; legacy Phase-1 runs must not be blocked by Phase-2 tables. | Clear failure semantics per engine. |
| 27.8 | Clerk auth + Pro-plan gate run BEFORE engine selection in the action. | M27 step 5: neither engine is reachable without passing auth + plan gating. | No engine-specific auth bypass. |
| 27.9 | Evo submission integration test drives the real gRPC client against the real C++ server binary on a private loopback port, using SYNTHETIC node types (`start`/`bench:sleep`). | The M17 gRPC ControlService is a synthetic local executor; product node types run on TS workers via the M26 loop, not here. | Proves TS↔C++ control-plane interop without claiming product-node execution over gRPC. |
| 27.10 | Trigger.dev dependencies and the run task are retained; only the action's routing changed. | M27 step 8: do not remove the legacy engine. | Dual-engine coexistence until the M40 decision. |

## M28 — Engine-neutral Evo run events and realtime frontend transport

| # | Decision | Rationale | Consequences |
|---|----------|-----------|--------------|
| 28.1 | One engine-neutral `NormalizedRunViewModel` (status, steps, timing, browser session id, final URL, output, stats) that BOTH engines map INTO. | M28 objective: existing UI concepts consume Evo runs without pretending they are Trigger.dev runs. One shape, two mappers. | UI reads the model through shared hooks; new engines add a mapper, not a new UI path. |
| 28.2 | `triggerRunToViewModel` keeps the legacy mapping (output steps preferred, metadata fallback, live session id from metadata). | M28 step 2 + Phase-1 preservation: the Trigger provider stays the default and its behavior is unchanged. | Legacy regression tests pin the mapping; the Trigger provider is NOT deleted (no-go). |
| 28.3 | `reduceEvoEvents` folds ordered C++ RunEvents idempotently per (kind, node_id); node type from dispatch `detail`, output from success `detail`; cross-run isolation; unknown kinds ignored; cancel never overwrites a completed node. | Matches the M26 C++ `RunEvent` contract (run_id/node_id/kind/detail/wall_ms). Idempotency makes duplicate delivery safe; isolation prevents one run's events corrupting another. | Duplicate/reordered-safe fold; forward-compatible with unknown event kinds. |
| 28.4 | Server-side Redis client (`lib/evo-redis.ts`) is lazy + cached, local Phase-2 Redis, credentials from server env only. | M28 no-go: do not expose Redis credentials to the browser. The browser talks only to the authorized SSE route. | Redis host/port/password never reach the client bundle. |
| 28.5 | Event delivery = Redis Streams (durable, queryable) + authorized SSE route. Replay via XRANGE, live tail via blocking XREAD, durable Postgres snapshot fallback. | M28 step 3 + 5: a durable/queryable mechanism verified against the deployment runtime; reconnect replays missed events or reads durable state rather than losing progress. | Durable state is persisted BEFORE events are published (M26), so the snapshot is always at least as fresh as the stream. |
| 28.6 | Authorized-route core (`buildEvoRunEventsResponse`) split from the thin route handler; checks run-row existence + org ownership + engine=evo. Unknown/tenant-mismatch/legacy all return 404. | M28 step 4 (Clerk org/run ownership) + testability: authorization and reconnect behavior are unit-testable without a Clerk session. 404 (not 403) avoids leaking cross-tenant existence. | Legacy runs get 404 and stay on the Trigger provider; no engine-specific auth bypass. |
| 28.7 | SSE wire format: `run-event` (data=EvoRunEvent, id=Redis stream id), `snapshot` (data=NormalizedRunViewModel), `: ping` keep-alive (15s); 10-min connection cap. | The Redis stream id as the SSE `id` lets EventSource send Last-Event-ID on reconnect for exact resume. Keep-alive + cap prevent pinned/dead connections. | Reconnect resumes exclusively after Last-Event-ID; the idempotent reducer makes any overlap safe. |
| 28.8 | One shared `EvoRunEventsProvider` per run (single EventSource), keyed remount per runId; exports `useEvoRunView`/`useEvoRunEventsError`. | M28 step 6: do not create one unbounded subscription per tiny UI component; preserve shared-provider architecture. Keyed remount resets state without synchronous setState-in-effect (lint rule). | All components on a run share one stream; closes for good after a terminal event. |
| 28.9 | Reconnect = EventSource auto-reconnect sends Last-Event-ID → route replays after that id; if the stream has no events for the run, serve the durable snapshot. | M28 step 5: never lose run progress across a dropped connection. | Reconnect simulation test proves resume-after-id and snapshot fallback. |
| 28.10 | Timestamps: event `wall_ms` and Postgres timestamps are wall-clock UTC; no steady_clock value crosses the process boundary. | M28 step 15: consistent wall-clock at every process/network boundary. | Durations computed from wall-clock deltas in the reducer and snapshot. |

## M29 — Achieve UI parity for Evo runs

| # | Decision | Rationale | Consequences |
|---|----------|-----------|--------------|
| 29.1 | Every Evo UI path reads the LOCAL Phase-2 Postgres via `getPhase2Db()` (lazy pg.Pool + drizzle); legacy paths stay on Neon and fail-open. | The engine + workers persist to local PG, not Neon; the UI must read the same store the engine writes to. Fail-open keeps a Phase-2 outage from breaking the legacy UI. | One durable authority per engine; no cross-store joins; Neon untouched (migration still needs explicit human approval). |
| 29.2 | gRPC `SubmitRun` bridges product DAGs to `DistributedRunLoop` (Redis transport + PgRunStore) instead of the local synthetic scheduler; `bench:*`/`start` keep the M17 local path. | M29 requires app-submitted Evo runs to execute on real workers with real browser sessions, not an in-process simulation. | The scheduler server links the distributed stack behind `EVO_HAVE_DISTRIBUTED`; synthetic benchmarks remain hermetic. |
| 29.3 | Worker composite executor: `start`/`bench:*` → synthetic; product types → M24 node-executor adapter with durable loaders (version + predecessor outputs from Phase-2 PG) + worker-local browser sessions. | Reuses the proven M24 executors/interpolation (no duplication) while giving workers everything needed to run real nodes. | Real workflows execute distributed; per-task screenshot capture feeds the results dialog. |
| 29.4 | Durable `workflow_runs.browserbase_session_id` column (additive migration 0002, local PG only); worker stamps it write-once at session open; listing + snapshot carry it to the UI. | Replay/live-view/screenshot must resolve the session from durable state — the event stream is best-effort and not authoritative. | Replay points at the correct session for any selected Evo run, even after reload or stream loss. |
| 29.5 | `PgRunStore::create_run` promotes a pre-existing `queued` row to `running` (`ON CONFLICT DO UPDATE ... WHERE status='queued'`); terminal rows never regressed. Mirrored in `InMemoryRunStore`. | The app pre-creates the run row as `queued` (M27); `DO NOTHING` left it queued forever with null `started_at`, breaking live status + duration. | Live status and run duration now work for Evo; idempotent and state-monotonic (regression-tested against live PG). |
| 29.6 | Console mapping extracted to pure `run-console.ts` (`toConsoleRunFromLegacy` verbatim Phase-1, `toConsoleRunFromEvo`, `consoleRunStatusLabel`, `mergeConsoleRuns`); provider merges legacy realtime + one durable Evo poll (2s) into a newest-first de-dup `ConsoleRun[]`. | Single source of truth testable without React/SDK; one poll per canvas (not per consumer); legacy mapping preserved verbatim guarantees zero Phase-1 regression. | All exported hooks keep name/signature, so every consumer renders both engines unchanged. |
| 29.7 | Consumers use derived booleans (`isCompleted`/`isFailed`/`isCanceled`) + shared status label instead of raw status strings; `RunStep.status` gained Evo-only `"canceled"` (renders inactive). | Legacy and Evo use different raw status vocabularies; booleans + one label function make them render identically. Canceled steps must never spin. | Engine-neutral rendering; the parity suite pins the equivalence. |
| 29.8 | Screenshot + live-view GET/connected routes are engine-aware: legacy via Trigger.dev (unchanged), Evo via org-scoped Phase-2 run-row lookups (`evo-run-data.ts`, fail-open). Connected handshake writes to the Phase-2 store for Evo; worker waits on it (60s/1s, fail-open). | Evo runs have no Trigger.dev record, so ownership + session must resolve from the Phase-2 store with the same org guarantees. The worker must honor the same "hold for live view" gate as Phase-1. | Evo screenshot/live-view/replay are org-checked; unwatched Evo runs never hang; no auth bypass. |
| 29.9 | Rerun mints a fresh `evo_<uuid>` run id + fresh immutable version snapshot; worker opens a new session per run affinity key. | M29 step 7: rerun must get a new engine-neutral run id AND a new browser session (no session reuse across runs). | Parity with Phase-1 session hygiene; asserted in the lifecycle/parity suites. |
| 29.10 | Parity regression suite (`run-console-parity.test.ts`, 9 scenarios) drives the exact console-mapping code for both engines per lifecycle state; wired into `npm test`. | M29 step 8 + no-go: do not make Evo default or compare performance until semantic parity is proven by tests. | Equivalent states assert identical semantics/label/vocabulary/session resolution; guards against future divergence. |
| 29.11 | Evo console liveness via 2s durable poll (not push). | The C++ engine persists every node transition as it happens, so a short poll is a correct, simple live view; push fan-out is a later optimization. | Acceptable parity now; no new realtime infra introduced this milestone. |

## M30 — Implement end-to-end cancellation across app, scheduler, queue, worker, and browser

| # | Decision | Rationale | Consequences |
|---|----------|-----------|--------------|
| 30.1 | Durable `workflow_runs.cancel_requested_at` column (additive migration 0003, local PG only) + `RunStore::mark_cancel_requested` (first-write-wins, `WHERE cancel_requested_at IS NULL`). | M30 step 1: persist the cancellation-request timestamp in engine-neutral run state so the request survives a process death mid-cancel and is distinguishable from "cancel finalized". | Idempotent stamp in both stores; `get_run` round-trips timestamp + reason; regression-tested against live PG. |
| 30.2 | `DistributedRunLoop::cancel()` is idempotent + terminal-no-op + durable + propagated: first request wins (reason + wall-clock ts), stamps the store, publishes exactly one `CANCEL_RUN` control message, sets the flag; `finalized_` set at every terminal exit. | M30 steps 2 + 8: CancelRun must atomically prevent future dispatch, and repeated Stop / Stop-after-terminal must be no-ops. One control message avoids fan-out storms. | Repeats and late cancels never overwrite the original request or re-publish; asserted in the loop + gRPC suites. |
| 30.3 | `dispatch_ready()` re-checks the cancel flag before dispatching (in addition to the run-loop check). | M30 no-go: no new task dispatches after terminal cancellation. Dispatch can be re-reached within one iteration after a result applies, so the guard must be unconditional at the dispatch site. | Asserted: cancel-before-dispatch creates zero attempt rows. |
| 30.4 | New additive proto `ControlEnvelope { kind, run_id, reason, requested_at }` with `Kind.CANCEL_RUN`, streamed on the existing `<prefix>:control` key. | Scheduler→worker cancellation needs a typed, versioned contract (not ad-hoc JSON). Additive evolution: field numbers never reused. | The previously-unused control stream now carries cancellation; forward-compatible with future control kinds. |
| 30.5 | Worker control fan-out: each worker reads the control stream with its OWN consumer group (`control-<workerId>`, start id "0") on a DEDICATED Redis connection. | Every worker must see every control message (fan-out, not competing-consumers); start id "0" means a (re)joining worker still receives earlier cancels. A shared connection would let one blocking XREADGROUP delay the other by a full read slice. | All workers get every cancel; task-claim throughput is unaffected by control reads. |
| 30.6 | Per-run `AbortController` created lazily at task start (not only on cancel); in-flight tasks hold `AbortSignal.any([workerShutdown, runCancel])`. | A task that started before its cancel arrived must still hold a signal the later CANCEL_RUN can abort. Combining signals keeps worker-shutdown abort working unchanged. | In-flight abort works regardless of cancel/claim ordering; shutdown semantics preserved. |
| 30.7 | Queued tasks for a canceled run are short-circuited: publish a `CANCELED` result (never execute) + ack. | The run is already terminal canceled in the durable store; executing would only produce a late result the scheduler rejects. Durable-handoff rule still honored (result published before ack). | No wasted work; the attempt row reflects cancellation, not execution. |
| 30.8 | `onCancelRun` → `BrowserSessionManager.closeAllForRun(runId)`: match sessions by runId across affinity keys, skip the final screenshot for prompt close, and close sessions racing an in-flight open. | M30 step 4: close Stagehand/browser resources promptly on cancel. Matching by runId handles custom affinity keys; skipping the screenshot avoids delaying the close; handling the open-race prevents a leaked session. | Prompt browser stop; no session leak on cancel; per-task screenshot (M29) already captured the last page state. |
| 30.9 | gRPC `CancelRun`: Stop-after-terminal reports the run's ACTUAL terminal outcome (never re-cancels); repeated Stop on a running run is a no-op after the first; unknown run → NOT_FOUND. Client passes `requested_at` (wall-clock UTC ms → Timestamp). | M30 step 8 idempotency at the RPC boundary; the loop's cancel is already first-request-wins, so the RPC just avoids re-entering a terminal run. `requested_at` is correlation/observability (the scheduler records its own authoritative stamp). | Idempotent RPC semantics asserted in grpc_integration; no outcome overwrite. |
| 30.10 | Late/forged success after terminal canceled is rejected: the run loop checks cancellation before consuming results, and the node is already terminal. | M30 step 6: a late result must never overwrite newer logical state. | Regression-tested: late success does not overwrite a terminal-canceled node. |
| 30.11 | Worker abort classification: a run-cancel abort → `ERROR_CANCELED` + `ResultStatus.CANCELED` + not retryable; a worker-shutdown abort keeps the legacy transient-failure classification. | Cancellation is not a failure and must not be retried; shutdown abort is a different semantic (the task may be redelivered). | Durable attempt rows + scheduler see correct cancel semantics. |
| 30.12 | Latency collected as DIAGNOSTIC single samples only (scheduler cancel→terminal ≈ 19µs; worker control→abort ≈ 360–400ms; control→browser-stop ≈ 367ms), explicitly labeled non-benchmark. | M30 performance-evidence rule: no resume number unless benchmark methodology (workload/hardware/sample-count/commit) is satisfied. Worker latency is bounded by the blocking-read slice. | No invented numbers; real benchmarking deferred to M39. |

## M31 — Implement worker registry, leases, and heartbeats

| # | Decision | Rationale | Consequences |
|---|----------|-----------|--------------|
| 31.1 | Heartbeat cadence/expiry are defined SEPARATELY from the per-task lease duration: a heartbeat proves the PROCESS is alive (registry row); a lease proves a specific ATTEMPT is being worked. | M31 step 1 + no-go: a heartbeat alone does not prove task progress. Conflating the two would let a live-but-stuck worker hold a lease forever, or reap a healthy worker mid-task. | Two independent clocks: `heartbeatIntervalMs` (registry) vs `leaseRenewIntervalMs`/`leaseDurationMs` (per-attempt). |
| 31.2 | Durable storage: new `workers` table (registry) + 4 additive `task_attempts` lease columns (`lease_acquired_at`, `lease_renewed_at`, `lease_expires_at`, `lease_expired_at`) + `ix_task_attempts_lease_expires` index. Migration 0004, local PG only. | M31 step 2: liveness + lease ownership must survive a scheduler restart and be auditable. Additive migration keeps Phase-1 tables untouched. | Neon untouched (needs explicit human approval); the C++ `PgRunStore` and TS `PgTaskLeaseStore` share the same schema. |
| 31.3 | Two-phase lease: the scheduler stamps a queue-wait deadline at dispatch (`init_attempt_lease`, `lease_initial_duration` — deliberately generous); the worker takes over on claim (`acquire_attempt_lease`, resets to `lease_duration`) and renews while working. | M31 steps 3 + 4: the dispatch→claim window is bounded separately so a live-but-slow-to-claim worker is not reaped before it acquires; once claimed, the shorter attempt lease bounds a lost worker. | `lease_initial_duration` (0 ⇒ `lease_duration`) is now actually used in `dispatch_ready()` (it was dead code before this fix). |
| 31.4 | Lease expiry is a RECOVERY transition, not a failure: `SchedulerState::abandon_node()` moves RUNNING/DISPATCHED → READY (no successor touched, no dependency-counter change), and the node is re-dispatched as a NEW attempt. | M31 step 5: a lost worker must not permanently fail the logical node. The attempt is terminal (`lease_expired`) but the node is recoverable. | A reaped node gets attempt N+1; asserted in the loop test (exactly two attempts, second succeeds). |
| 31.5 | At-most-once reap: `mark_attempt_lease_expired` applies only when the attempt is still `running` and held by the recorded worker; a racing completion already left `running`, so it can never be double-completed. | M31 no-go: lease expiry must not double-complete the logical node. The conditional UPDATE is the invariant guard. | Duplicate reap is a no-op; completed attempts keep their terminal status (regression-tested in both stores). |
| 31.6 | Steal guard: a DIFFERENT worker may only acquire a lease once the current one has expired (or no worker holds it). Only the holder may renew. | Prevents two workers from executing the same attempt concurrently; an unexpired lease is authoritative. | A second worker seeing a redelivered task skips + acks it rather than executing (TS test asserts no execution). |
| 31.7 | `worker_id` is NULL for a never-claimed attempt; the PG reap matches an empty `worker_id` argument against NULL explicitly (`$4 = '' AND worker_id IS NULL`). | `record_attempt` stores `NULLIF(worker_id,'')`; `NULL = ''` is never true in SQL, so a queue-wait lease would otherwise be unreapable. | Never-claimed (queue-wait) attempts are reapable (regression-tested against live PG). |
| 31.8 | The expired-lease scan runs inside the single-threaded run loop, paced by `lease_scan_interval` (steady clock) but comparing durable `lease_expires_at` against wall-clock UTC. | M31 step 4: the loop already owns all scheduling state; adding a scan thread would need new synchronization. Steady clock paces cadence; wall clock is the durable comparison. | No busy-spin; no new thread boundary; disabled when `lease_duration` or `lease_scan_interval` is 0. |
| 31.9 | On reap, the abandoned node's resource slot is released immediately (no result will arrive to free it). | A browser-affinity slot held by a dead worker would otherwise leak capacity forever, blocking same-affinity nodes. | Affinity capacity is reclaimed; asserted implicitly by the recovery test completing. |
| 31.10 | Worker-side: register + heartbeat on start (own cadence, `unref`'d timer); acquire before executing; renew on an interval while running; stop renewing once the attempt finishes; skip + ack when the lease is not acquirable. | M31 step 3: the worker must prove it is legitimately working. Stopping renewal on finish makes the lease moot once the result hands off. | A renewing worker is never reaped (slow-worker test); a killed worker's lease expires and is reaped (killed-worker test). |
| 31.11 | Timestamps: all durable lease/heartbeat timestamps are wall-clock UTC ms; steady_clock is used only to pace the scan cadence and never persisted. | M31 step 16: consistent wall-clock at every durable boundary. | Lease comparisons are clock-consistent across the C++ scheduler and TS workers. |
| 31.12 | No performance numbers claimed; the killed-worker recovery is a correctness test, not a latency benchmark. | M31 performance-evidence rule: no resume number without benchmark methodology. | Recovery-latency benchmarking deferred to M39. |
