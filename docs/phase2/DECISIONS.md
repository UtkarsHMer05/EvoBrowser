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
