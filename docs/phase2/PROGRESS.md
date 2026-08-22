# Phase 2 — Progress Log

One entry per milestone. Entries record observed results, not promises.
Commit subjects follow `phase2(mNN): <description>`.

> **Concurrent-session coordination:** two agent sessions work this repo.
> Git is the shared channel — pull before starting work, and claim a
> milestone in the table below (status 🚧 IN PROGRESS + claimant) before
> editing its files. Never rewrite another session's commits.

| Milestone | Title | Status | Commit |
| :--- | :--- | :--- | :--- |
| M01 | Reconcile the real Phase-1 source and archive | ✅ DONE | `39fa8e1` |
| M02 | Certify and freeze the Phase-1 behavioral baseline | ✅ DONE | `0ef8ea8` |
| M03 | Phase-2 architecture, invariants, and progress scaffold | ✅ DONE | `a3e3210` |
| M04 | Bootstrap the reproducible C++20 toolchain | ✅ DONE | `e3da142` |
| M05 | Implement the canonical C++ DAG model | ✅ DONE | `19722f1` |
| M06 | Implement a deterministic sequential reference scheduler | ✅ DONE | `80ef0a6` |
| M07 | Implement scheduler state machines and dependency counters | ✅ DONE | `0c23c78` |
| M08 | Implement a thread-safe blocking ready queue | ✅ DONE | `78f8fc6` |
| M09 | Implement the bounded std::jthread worker pool | ✅ DONE | `00a3533` |
| M10 | Build the local concurrent dependency-aware DAG scheduler | ✅ DONE | `0f40920` |
| M11 | Add cooperative cancellation and graceful shutdown | ✅ DONE | `477497c` |
| M12 | Add execution resource classes and browser affinity policy | ✅ DONE | `cc73f30` |
| M13 | Instrument the scheduler core with evidence-grade timestamps and counters | ✅ DONE | `916ffc9` |
| M14 | Harden C++ correctness with sanitizers and concurrency stress | ✅ DONE | `b361a4e` |
| M15 | Create the first local benchmark corpus and sequential-vs-concurrent evidence | ✅ DONE | `bea4b06` |
| M16 | Define the shared Protobuf/gRPC execution contract | ✅ DONE | `dbaecf8` |
| M17 | Implement the C++ scheduler service over gRPC | ✅ DONE | `7cf04f6` |
| M18 | Create isolated local Redis + PostgreSQL infrastructure | ✅ DONE | `813da0a` |
| M19 | Add additive Phase-2 Drizzle schema and migrations | ✅ DONE | `f63d9b4` |
| M20 | Implement immutable workflow versions and optimistic concurrency | ✅ DONE | `0ea1c1c` |
| M21 | Implement Redis Streams transport in the C++ scheduler | ✅ DONE | `47e565e` |
| M22 | Finalize task/result envelope semantics and event transport | ✅ DONE | `ad88fd0` |
| M23 | Create the TypeScript distributed worker service | ✅ DONE | `dd01b19` |
| M24 | Reuse existing interpolation and node executors inside distributed workers | ✅ DONE | `e581e67` |
| M25 | Implement distributed browser session ownership and live-view parity | ✅ DONE | `3828448` |
| M26 | Persist results and unlock dependencies through the distributed loop | ✅ DONE | `2db7bb9` |
| M27 | Introduce the Next.js execution-engine abstraction and feature flag | ✅ DONE | `799a4f6` |
| M28 | Build engine-neutral Evo run events and realtime frontend transport | ✅ DONE | `c3723bc` |
| M29 | Achieve UI parity for Evo runs | ✅ DONE | `a24dd9e` |
| M30 | Implement end-to-end cancellation across app, scheduler, queue, worker, and browser | ✅ DONE | `200386a` |
| M31 | Implement worker registry, leases, and heartbeats | ✅ DONE | `4543443` |
| M32 | Implement node-level retry policy, backoff, jitter, dead-lettering | ✅ DONE | `dcad613` |
| M33 | Implement idempotency and duplicate suppression | ✅ DONE | `2887e88` |
| M34 | Implement worker crash recovery and failure injection | ✅ DONE | `b6cff19` |
| M35 | Implement scheduler restart recovery and durable reconciliation | ✅ DONE | `fb5313e` |
| M36 | Add multi-tenant quotas and backpressure | ✅ DONE | `d490b7b` |
| M37 | Implement fair scheduling and starvation resistance | ✅ DONE | `02cc98e` |
| M38 | Add observability, service security, and CI quality gates | ✅ DONE | `f9aa65e` |
| M39 | Run the final reproducible performance, scaling, and chaos campaign | ✅ DONE | `200b6ee` |
| M40 | Final Phase-2 audit, documentation, release, and resume evidence registry | ⬜ NOT STARTED | — |

---

## M01 — Reconcile the real Phase-1 source and archive

- **BASE_SHA:** `5005768` (main, clean tree, in sync with origin/main)
- **What was inspected:** `git status/log/branch/remote`, `AGENTS.md`, `README.md`,
  `docs/PHASE-1-IMPLEMENTATION-REPORT.md`, `package.json` scripts + dependency
  versions, `lib/db/schema.ts`, `features/workflows/nodes/node-registry.ts`,
  `features/workflows/tasks/run-workflow.ts`, `features/workflows/actions.ts`,
  planner/live-browser/results file inventory, host toolchain (cmake, clang++,
  ninja, redis, docker, node, npm, git identity).
- **Result:** every Phase-1 baseline claim in the master prompt verified against
  the checked-out source (see `docs/phase2/RECONCILIATION.md`). Node catalog is
  exactly the 7 expected types. DB has exactly 3 tables. Working tree clean —
  no uncommitted user work. No production code changed.
- **Environment flags:** Docker not installed (needed at M18); Redis 8.4.0
  installed and running; CMake 4.2.1 + Apple clang 21 + ninja present; vcpkg
  absent (M04 will choose dependency strategy).
- **Tests:** none run (no code changed; baseline certification is M02).
- **Human action:** none required now. Docker Desktop (or equivalent) will be
  required at Milestone 18 for isolated local PostgreSQL.
- **COMMIT:** `39fa8e1` — `phase2(m01): record verified phase1 source`

---

## M02 — Certify and freeze the Phase-1 behavioral baseline

- **BASE_SHA:** `39fa8e1`
- **Gates measured at the certified SHA:**
  - `npm test` → ✅ 28/28 (convert-plan 7/7, integration 10/10, lifecycle 11/11)
  - `npm run typecheck` → ✅ exit 0
  - `npm run lint` → ✅ exit 0
  - `npm run build` → ✅ production build succeeded, all 15 routes compiled
- **What changed:** created `docs/phase2/PHASE1_BASELINE.md` freezing the 12
  protected behaviors, the measured gate results, the current sequential
  execution model, and the manually-only-covered behaviors. No production code
  changed; no test weakened.
- **Branch:** `phase2` created from the certified baseline after all gates went
  green; this commit lands on `phase2`.
- **Human action:** none.
- **COMMIT:** `0ef8ea8` — `phase2(m02): certify immutable phase1 baseline`

---

## M03 — Create the Phase-2 architecture, invariants, and progress scaffold

- **BASE_SHA:** `0ef8ea8`
- **What changed (docs only, no production behavior change):**
  - `docs/phase2/ARCHITECTURE.md` — dual-engine strategy (legacy Trigger.dev
    stays default; Evo engine opt-in via fail-closed feature flag), component
    responsibilities (Next.js control plane, C++ orchestrator, Redis Streams,
    Postgres/Drizzle, TS workers reusing existing executors), browser session
    affinity rules (capacity-1 resource per affinity key; same-session browser
    nodes never parallelize; lost-browser rule on worker death), target state
    machines (run / node / attempt), dependency + fan-in invariants, clock
    discipline, and explicit non-goals.
  - `docs/phase2/FAILURE_MODEL.md` — transport/process/execution assumptions
    (duplicates, silent worker death, lost ACKs, transient Redis/Postgres
    errors, scheduler restart), at-least-once baseline with logical-commit
    idempotency, side-effect ambiguity windows per node type, cancellation
    races completion rule, slow≠dead rule, browser failure rules, chaos test
    surface, and explicit non-claims.
  - `docs/phase2/BENCHMARK_METHODOLOGY.md` — binding no-fabrication rules,
    metric definitions (makespan, latencies, throughput, speedup, parallel
    efficiency, recovery, cancellation, duplicate suppression, fairness,
    memory, CPU), workload-class separation, raw-sample artifact format, and
    the publication gate.
  - `docs/phase2/PROGRESS.md` — this scaffold (already created in M01).
- **Phase-1 regression:** N/A (docs-only change; no app code touched).
- **Human action:** none.
- **COMMIT:** `a3e3210` — `phase2(m03): define phase2 architecture and failure model`

---

## M04 — Bootstrap the reproducible C++20 toolchain

- **BASE_SHA:** `a3e3210`
- **What changed:** created `engine/` — `CMakeLists.txt` (C++20, no
  extensions, `-Wall -Wextra -Wpedantic -Werror` on project code, git SHA
  baked into the binary), `core/include/evo/version.hpp` +
  `core/src/version.cpp` (build metadata + compile-time jthread detection),
  `app/smoke_main.cpp` (prints metadata only), `tests/toolchain_test.cpp`
  (proves C++20 mode, jthread cooperative stop + RAII join, steady_clock
  monotonicity). Added engine build-tree ignores to `.gitignore`. Wrote
  `docs/phase2/BUILDING_ENGINE.md` with exact setup commands and sanitizer
  rules. No gRPC/Redis/Postgres deps yet (per milestone no-go list); no
  global installs; no production app code touched.
- **Dependency strategy note:** vcpkg is not installed on this machine; the
  core engine (M04–M15) needs only the C++ standard library, so no dependency
  manager is required yet. The manifest-vs-FetchContent decision for
  gRPC/Redis/Postgres is deferred to M16/M18 when those deps actually land.
- **Validation:**
  - `cmake -S engine -B engine/build -G Ninja -DCMAKE_BUILD_TYPE=Release` → ✅
  - `cmake --build engine/build` → ✅ clean under -Werror
  - `ctest --test-dir engine/build --output-on-failure` → ✅ 1/1 passed
  - `./engine/build/evo-smoke` → `evo-engine v0.1.0 (Release) commit=a3e3210 compiler=clang 21.0.0 cxx=202002 jthread=yes`
- **Phase-1 regression:** N/A (no TypeScript/app code touched; `.gitignore`
  change is additive ignores only).
- **Human action:** none.
- **COMMIT:** `e3da142` — `phase2(m04): bootstrap c++20 engine toolchain`

---

## M05 — Implement the canonical C++ DAG model

- **BASE_SHA:** `e3da142`
- **What changed:**
  - `engine/core/include/evo/json.hpp` + `core/src/json.cpp` — minimal recursive-descent JSON parser/serializer (no external dep) for the canonical DAG shape only (`evo::json::Value`: Null/Bool/Number/String/Array/Object, deterministic object ordering via `std::map`).
  - `engine/core/include/evo/dag.hpp` — strongly typed `NodeId` (wraps a string), `NodeKind` {Trigger,Action}, `NodeSpec`, `Edge`, `GraphError` with stable codes, immutable `Dag` class exposing node/edge counts, adjacency (sorted for determinism), Kahn topo order with lexicographic tie-break, reachability, and `execution_problems()` (scheduler contract). `BuildResult` holding `errors` + `std::optional<Dag>` is defined *after* `Dag` so the optional's type is complete (this was the compile break from the prior session — a forward declaration alone is insufficient).
  - `engine/core/src/dag.cpp` — validates duplicate node ids, missing edge endpoints, self-loops, duplicate edges, cycles (Kahn residual), and builds deterministic adjacency/topo order; `to_json/from_json/from_json_string` for canonical round-trips.
  - `engine/tests/dag_test.cpp` — 13 suites: linear, diamond, wide fan-out/fan-in (1→32→1), disconnected, duplicate id, empty id, missing endpoint, self-loop + duplicate edge, cycle, canonical JSON round-trip, malformed payloads, and the execution-contract checks.
  - `engine/CMakeLists.txt` — registered `json.cpp`/`dag.cpp` in `evo_scheduler_core` and added the `evo_dag_test` CTest target.
- **Bug fixed in this session:** `BuildResult` was forward-declared but never defined (build failed because `std::optional<Dag>` needs a complete type); fixed by defining the struct after `class Dag`. Also fixed a test-side string-literal concat (`"w" + ...` → `std::string("w") + ...`).
- **Phase-1 validateGraph vs engine DAG difference (Milestone 05 item 7):** Phase-1 `features/workflows/lib/validate-graph.ts` checks exactly-one-trigger, ≥1 edge (else "Connect your nodes"), and acyclicity via toposort — and *silently skips edge-less/disconnected nodes at run time* (only connected nodes are walked). The engine DAG is stricter: it also rejects empty ids, duplicate ids, missing edge endpoints, self-loops, duplicate edges, and reports any node not reachable from the single trigger as an `execution_problem` (disconnection is a *structural* build success but an *execution* failure). This preserves Phase-1 behavior (the TS validator is untouched) while giving the Evo engine a deterministic contract to depend on.
- **Thread-safety:** an immutable built `Dag` is shareable by const reference across threads; all mutation happens in `Dag::build` returning a new instance. Clock discipline: no timestamps emitted here (reserved for M13).
- **Validation:**
  - `cmake -S engine -B engine/build -G Ninja -DCMAKE_BUILD_TYPE=Release` → ✅
  - `cmake --build engine/build` → ✅ clean under `-Wall -Wextra -Wpedantic -Werror`
  - `ctest --test-dir engine/build --output-on-failure` → ✅ 2/2 (toolchain, dag)
  - `./engine/build/evo-smoke` → `evo-engine v0.1.0 (Release) commit=e3da142 compiler=clang 21.0.0 cxx=202002 jthread=yes`
  - `./engine/build/evo_dag_test` → "all DAG model tests passed"
- **Phase-1 regression:** `npm test` → ✅ 28/28 (run as extra evidence; no TS/app code was touched, so N/A by policy — included because M05's checklist lists it).
- **Human action:** none.
- **COMMIT:** `19722f1` — `phase2(m05): add canonical c++ dag model`

---

## M06 — Implement a deterministic sequential reference scheduler

- **BASE_SHA:** `19722f1`
- **What changed:**
  - `engine/core/include/evo/scheduler.hpp` — `TaskResult`, `TaskFn`
    (`std::function<TaskResult(const NodeSpec&)>`), `NodeRun` (per-node
    `std::chrono::steady_clock` start/end, sequence, result), `RunLog`
    (ordered run log with `all_ok()` + canonical `to_json_string()`), and a
    single-threaded `Scheduler` owning an immutable `Dag` + task registry that
    walks the top order and halts on the first failing node.
  - `engine/core/src/scheduler.cpp` — `Scheduler::run` dispatch + run-log
    serialization via `evo::json`.
  - `engine/core/include/evo/bench.hpp` + `core/src/bench.cpp` — benchmark-only
    synthetic tasks `sleep_task` (simulated I/O-bound via
    `std::this_thread::sleep_for`) and `burn_task` (CPU-bound deterministic
    volatile accumulator loop); a xorshift64* `Rng` (rejection-sampled
    `next(n)` to avoid modulo bias); and `generate_workload(seed,width,depth)`
    producing an acyclic, trigger-reachable `Generated{Dag,sleep_ms,burn_iters}`
    with per-node bench params captured by closure.
  - `engine/tests/scheduler_test.cpp` — 6 suites: linear ordering, diamond
    dependency ordering, deterministic rerun (byte-identical log),
    unregistered-type failure (halts after the failing node), seeded
    reproducibility (same seed ⇒ identical graph + params; different seed ⇒
    different), and a wide fan-in/fan-out bench workload completing in topo
    order.
  - `engine/CMakeLists.txt` — added `scheduler.cpp`/`bench.cpp` to
    `evo_scheduler_core` and the `evo_scheduler_test` CTest target.
- **Key decisions:**
  - `TaskFn` captures per-node bench params in its closure, so the canonical
    DAG model (M05) is NOT retconned to carry params — the graph shape stays
    stable.
  - Synthetic types are `bench:sleep`/`bench:burn` living **only** in this
    engine harness; never added to the TypeScript node registry (preserves
    M05's "Do not expose synthetic task types to the product planner/node
    registry").
  - Timing in tests is labeled `diag(...)` and explicitly NOT a performance
    claim; M15 owns evidence-grade benchmarks.
- **Thread-safety / clock:** single-threaded here; no thread boundaries crossed.
  Durations use `std::chrono::steady_clock` (monotonic).
- **Validation:**
  - `cmake -S engine -B engine/build -G Ninja -DCMAKE_BUILD_TYPE=Release` → ✅
  - `cmake --build engine/build` → ✅ clean under `-Wall -Wextra -Wpedantic -Werror`
  - `ctest --test-dir engine/build --output-on-failure` → ✅ 3/3 (toolchain, dag, scheduler)
  - `./engine/build/evo_scheduler_test` → "all scheduler tests passed"
- **Phase-1 regression:** `npm test` → ✅ 28/28 (no TS/app code touched — N/A by policy; run for evidence).
- **Human action:** none.
  - **COMMIT:** `80ef0a6` — `phase2(m06): add sequential scheduler reference`

---

## M07 — Implement scheduler state machines and dependency counters

- **BASE_SHA:** `80ef0a6`
- **What changed:**
  - `engine/core/include/evo/state_machine.hpp` — run-level state (`RunState`:
    QUEUED/RUNNING/SUCCEEDED/FAILED/CANCELED), node-level state
    (`NodeState`: BLOCKED/READY/DISPATCHED/RUNNING/SUCCEEDED/RETRY_WAIT/FAILED/
    DEAD_LETTERED/CANCELED), `NodeStatus`, and `SchedulerState` class. Implements
    the ARCHITECTURE.md §6.1–§6.5 state machines: root nodes (0 predecessors)
    become READY on `start_run`; dependency counters decrement on each
    predecessor SUCCESS; fan-in nodes become READY only when all predecessors
    succeed; duplicate completion messages are idempotent via a `completed_` set;
    failure propagation transitively CANCELs all reachable non-terminal successors.
  - `engine/core/src/state_machine.cpp` — state transitions, dependency counter
    management, idempotent completion, transitive failure cancellation, run-level
    cancel.
  - `engine/tests/state_test.cpp` — 10 suites: initial state, diamond fan-in
    dependency, idempotent completion, transitive failure propagation, late-
    failure does-not-clobber-success, illegal transitions rejected, cancel_run,
    wide fan-out/fan-in (8→sink), all-succeeded terminal, failure-via-
    complete_node path.
  - `engine/CMakeLists.txt` — added `state_machine.cpp` to `evo_scheduler_core`,
    added `evo_state_test` CTest target.
- **Key decisions:**
  - `SchedulerState` owns the immutable `Dag` by value; per-node state/deps are
    private. Single-threaded in M07; M10 adds synchronization.
  - Completion idempotency is per-node: once a node is in the `completed_` set,
    subsequent completion/failure messages are no-ops (preserves the fan-in
    invariant from §6.5: "completion events are idempotent per (predecessor,
    successor) pair").
  - Failure policy: FAILED propagates CANCELED transitively to all reachable
    non-terminal successors (never silently runs downstream work on a failed
    dependency).
- **Concurrency correctness:** single-threaded; no data races by construction.
  Thread-safe wrapper added in M10.
- **Validation:**
  - `cmake -S engine -B engine/build -G Ninja -DCMAKE_BUILD_TYPE=Release` → ✅
  - `cmake --build engine/build` → ✅ clean under `-Wall -Wextra -Wpedantic -Werror`
  - `ctest --test-dir engine/build --output-on-failure` → ✅ 4/4 (toolchain, dag, scheduler, state)
  - `./engine/build/evo_state_test` → "all state-machine tests passed"
  - ASan+UBSan debug build → ✅ 4/4 all pass, no sanitizer errors
- **Phase-1 regression:** N/A (no TypeScript/app code touched).
- **Human action:** none.
- **COMMIT:** `0c23c78` — `phase2(m07): add scheduler state machine and dependency counters`

## M08 — Implement a thread-safe blocking ready queue

- **Commit:** `78f8fc6`
- **Files:** `engine/core/include/evo/ready_queue.hpp`, `engine/core/src/ready_queue.cpp`,
  `engine/tests/ready_queue_test.cpp`, `engine/CMakeLists.txt`
- **Design:**
  - `evo::ReadyQueue` — thread-safe FIFO with `std::mutex` + `std::condition_variable_any`.
  - `max_size == 0` → unbounded; `max_size > 0` → push blocks when full (backpressure).
  - `pop(stop_token)` — honors `std::stop_token` via `std::stop_callback` that calls
    `notify_one`; drains remaining items before returning `nullopt` on stop.
  - `try_pop()` — non-blocking, returns `nullopt` if empty.
  - `close()` — idempotent; sets `closed_`, notifies all waiters on both condition
    variables; `push` returns false, `pop` blocks are woken with `nullopt`.
- **Test coverage (9 sub-tests):**
  1. FIFO ordering
  2. close while empty
  3. close wakes blocked consumer
  4. many producers (8×200) / many consumers (8) — no lost tasks
  5. stop_token interrupts blocking pop (no items delivered)
  6. stop_token still drains remaining items
  7. bounded queue backpressure (capacity-2 blocks third push until pop)
  8. spurious-wakeup robustness stress (4×500 items, 4 consumers)
  9. empty queue `try_pop` returns `nullopt`
- **Validation:**
  - Release build: `ninja -C engine/build` → ✅
  - `./engine/build/evo_ready_queue_test` → ✅ "all ready-queue tests passed"
  - ASan+UBSan debug build: `ctest --test-dir engine/build-asan` → ✅ 5/5 pass, no errors
- **Key decisions:**
  - Used `condition_variable_any` (not `condition_variable`) because `std::stop_callback`
    requires a `BasicLockable` but `condition_variable` only works with `unique_lock<mutex>`;
    `_any` is forward-compatible with future lock types.
  - Default-constructed `std::stop_token` (used by `pop()` overload without arg) is valid
    — `stop_callback` simply doesn't register since `stop_possible() == false`. No throw.
- **Concurrency correctness:** no data races — all state access is under `mu_`. ASan
  thread sanitizer not enabled in this build, but mutex discipline is straightforward.
- **Phase-1 regression:** none — no TS code touched.
- **Human action:** none.
- **COMMIT:** `78f8fc6` — `phase2(m08): implement blocking ready queue`

## M09 — Implement the bounded std::jthread worker pool

- **Commit:** `00a3533`
- **Files:** `engine/core/include/evo/thread_pool.hpp`, `engine/core/src/thread_pool.cpp`,
  `engine/tests/thread_pool_test.cpp`, `engine/CMakeLists.txt`
- **Design:**
  - `evo::ThreadPool` — fixed-size pool of `std::jthread` workers looping over a
    shared `std::queue<std::function<void()>>` protected by `std::mutex` +
    `std::condition_variable_any`.
  - `drain()` — graceful shutdown: sets `draining_ = true`, notifies all workers,
    each worker finishes current task + remaining queue, then exits. Idempotent.
  - `stop()` — immediate shutdown: sets `stopped_ = true`, calls `request_stop()`
    on all `std::jthread`s (wakes their `cv_.wait(lock, st, pred)`), joins all.
    Pending tasks are abandoned. Idempotent.
  - `submit(Task)` — returns `false` if `draining_` or `stopped_`; otherwise
    enqueues and notifies one worker. Exceptions from tasks are caught and stored
    as `std::exception_ptr` in a thread-safe queue.
  - `take_exceptions()` — drains and returns captured exceptions.
  - Metrics: `num_workers()` (constant), `active_workers()` (currently executing),
    `num_tasks_submitted()`, `num_tasks_completed()`.
- **Test coverage (12 sub-tests):**
  1. Worker counts: 1, 2, 4, 8 workers — all execute 100 tasks correctly
  2. Task exception capture — `runtime_error` and `logic_error` both captured
  3. Stop during wait — idle workers exit immediately
  4. Stop with pending tasks — some tasks abandoned, completed count matches
  5. Repeated construction/destruction — 100 pools created/destroyed without hang
  6. Submit after drain/stop rejected
  7. Active workers counter — >0 during execution, 0 after drain
  8. Zero leaked/hanging threads — 50 rapid pools × 200 tasks each
  9. Drain idempotent
  10. Stop idempotent
  11. Drain after stop no-op
  12. Exception in task during destructor drain — doesn't terminate process
- **Validation:**
  - Release build: `ninja -C engine/build` → ✅
  - `./engine/build/evo_thread_pool_test` → ✅ "all thread-pool tests passed"
  - ASan+UBSan debug build: `ctest --test-dir engine/build-asan` → ✅ 6/6 pass, no errors
- **Key decisions:**
  - `std::condition_variable_any` required for stop_token-aware wait
    (`cv_.wait(lock, st, pred)`). Regular `condition_variable` doesn't support
    `stop_token` overload.
  - `drain()` in destructor (not `stop()`) — graceful default; user calls
    `stop()` explicitly if they want to abandon tasks. Avoids accidental task loss.
  - Exception capture uses `std::current_exception()` stored in `exception_ptr`;
    caller rethrows to inspect. No exception escapes worker thread (contract
    satisfies §4.3 "No exception may escape a worker thread").
  - Active workers counter uses `std::atomic` with `memory_order_relaxed` —
    sufficient for metrics/observability; no synchronization with task queue needed.
- **Concurrency correctness:** no data races — queue state under `mu_`, counters
  atomic. ASan+TSan not run but mutex discipline is straightforward; `cv_any` with
  `stop_token` ensures no lost wakeups on shutdown.
- **Phase-1 regression:** none — no TS code touched.
- **Human action:** none.
- **COMMIT:** `00a3533` — `phase2(m09): implement bounded jthread worker pool`

---

## M10 — Build the local concurrent dependency-aware DAG scheduler

- **BASE_SHA:** `00a3533`
- **What changed:**
  - `engine/core/include/evo/concurrent_scheduler.hpp` — `ConcurrentScheduler`
    class owning a `ThreadPool`, a `ReadyQueue`, and a thread-safe
    `SchedulerState`. `ConcurrentConfig` (num_workers, ready_queue_capacity).
    `ConcurrentNodeRun` (adds `ready_at` steady_clock timestamp to the sequential
    `NodeRun` fields) and `ConcurrentRunLog` (sorted by logical completion
    `sequence`). `run()` returns the complete run log; `cancel()` is a
    cooperative stop hook (reserved for M11).
  - `engine/core/src/concurrent_scheduler.cpp` — the dispatcher loop. `run()`
    calls `state_.start_run()`, pushes initially-ready roots into the
    `ReadyQueue`, then loops: wait on a `std::condition_variable_any`
    (`dispatch_cv_`) until the queue is non-empty OR no tasks are in flight OR
    cancellation is requested; pop all available ready nodes and submit each to
    the `ThreadPool` (marking READY→DISPATCHED under the state lock, recording
    the steady_clock `ready_at`). Worker tasks execute the registered `TaskFn`,
    record `started_at`/`finished_at`, append a `ConcurrentNodeRun` to the
    log under `log_mu_`, then call `on_node_complete` which drives
    `state_.complete_node` (unlocking successors idempotently, propagating
    failure cancellation) and notifies the dispatcher. Termination fires when
    `in_flight_ == 0 && ready_queue_.size() == 0`.
  - `engine/core/include/evo/state_machine.hpp` +
    `engine/core/src/state_machine.cpp` — **pre-edited thread-safety pass for
    M10**: every public method now takes the internal `mu_` (value-returning
    accessors return copies/snapshots), enabling the concurrent scheduler to
    drive the same transitions from multiple pool threads. Added
    `finalize_run()` (derives RUNNING→SUCCEEDED/FAILED/CANCELED from node
    outcomes) used at the end of `run()`.
  - `engine/tests/concurrent_scheduler_test.cpp` — 7 suites:
    1. linear chain equivalence (order + all succeed)
    2. diamond equivalence + concurrency overlap (independent left/right
       overlap, join waits for both) + dependency invariant
    3. wide 1→8→1 fan-out overlap (peak concurrency ≥ 4 with 8 workers)
    4. seeded random DAG equivalence vs sequential reference (same node count,
       all-ok, succeeded-set, and per-node outputs) across 5 seeds
    5. stress: dependency invariant (`u.finished ≤ v.started` for every edge)
       across 20 random DAGs
    6. no logical node executes twice (exec-count == 1 each)
    7. failure propagation: a deliberately fails → b and c never succeed
       (canceled by upstream failure)
  - `engine/CMakeLists.txt` — registered `concurrent_scheduler.cpp` in
    `evo_scheduler_core` and added the `evo_concurrent_scheduler_test` CTest
    target.
- **Key decisions:**
  - **Dispatcher architecture (no busy-spin, no deadlock):** the main `run()`
    thread owns dispatching and blocks on `dispatch_cv_` (not on a raw
    `ready_queue_.pop()`), so the run terminates cleanly when the last worker
    finishes — the queue alone cannot signal "nothing left to do". Workers
    increment `in_flight_` on submit and decrement+notify on completion.
  - **Dependency correctness under concurrency** is provided by the already-
    tested `SchedulerState` (M07) idempotent dependency counters; the
    concurrent scheduler only adds the thread pool + dispatch coordination.
  - **Timestamps** use `std::chrono::steady_clock` throughout (monotonic,
    matches the sequential scheduler and the benchmark methodology §14).
  - **Equivalence** is asserted structurally (same succeeded node set + same
    per-node outputs), not by comparing wall-clock makespan — concurrency is
    allowed to reorder start times while logical results stay identical.
- **Concurrency correctness:** all shared state cross-threaded is guarded —
  `SchedulerState` under `mu_`, dispatcher coordination under `dispatch_mu_` +
  `dispatch_cv_`, `in_flight_`/`sequence_counter_`/`canceled_` are atomics, the
  run log is appended under `log_mu_`. ASan+UBSan (Debug) run is clean.
- **No-go compliance:** sleep-overlap is explicitly NOT claimed as a universal
  app speedup; equivalence to the sequential reference is the mandatory gate
  and is enforced by tests 4–5. No perf numbers invented.
- **Validation:**
  - `cmake -S engine -B engine/build -G Ninja -DCMAKE_BUILD_TYPE=Release` → ✅
  - `cmake --build engine/build` → ✅ clean under `-Wall -Wextra -Wpedantic -Werror`
  - `ctest --test-dir engine/build --output-on-failure` → ✅ 7/7 (toolchain,
    dag, scheduler, state, ready_queue, thread_pool, concurrent_scheduler)
  - `./engine/build/evo_concurrent_scheduler_test` → "all concurrent-scheduler tests passed"
  - ASan+UBSan Debug build: `ctest --test-dir engine/build-asan` → ✅ 7/7 pass, no errors
  - `npm test` (Phase-1) → ✅ 28/28 (run as evidence; no TS/app code touched)
- **Phase-1 regression:** `npm test` → ✅ 28/28 (no TS/app code touched; run
  for evidence per M10 checklist).
- **Human action:** none.
- **COMMIT:** `0f40920` — `phase2(m10): implement concurrent dag scheduler`
- **NEXT:** M11 — cooperative cancellation and graceful shutdown.

---

## M11 — Add cooperative cancellation and graceful shutdown

- **BASE_SHA:** `0f40920`
- **What changed:**
  - `engine/core/include/evo/concurrent_scheduler.hpp` —
    `ConcurrentTaskFn = std::function<TaskResult(const NodeSpec&, std::stop_token)>`
    overload for stop-aware tasks. Second `ConcurrentScheduler` constructor
    accepting `std::map<std::string, ConcurrentTaskFn>`. `cancel()` records
    `cancel_requested_at_` and `run_terminal_at_` (steady_clock), exposes
    `is_canceled()` / `cancel_requested_at()` / `run_terminal_at()`. Internal
    `std::stop_source stop_source_` shared with in-flight tasks.
  - `engine/core/src/concurrent_scheduler.cpp` — `cancel()` is now idempotent
    (CAS on `canceled_`) and: (1) commits the run to a terminal CANCELED state
    via `state_.cancel_run()` (covers pending/blocked nodes); (2) requests stop
    on `stop_source_` so running tasks observe the token; (3) wakes the
    dispatcher via `dispatch_cv_.notify_all()`. The dispatcher loop breaks on
    `canceled_` so no new work is issued. `on_node_complete` skips unlocking
    successors once canceled. `run()` honors a pre-start cancel (returns an
    empty canceled log). Workers receive `stop_source_.get_token()`.
  - `engine/core/include/evo/bench.hpp` +
    `engine/core/src/bench.cpp` — `sleep_task_cooperative` /
    `burn_task_cooperative` returning `ConcurrentTaskFn`; they poll the
    `stop_token` in small slices and abort early when cancellation is requested
    (so cancellation tests verify in-flight tasks do not run to completion).
  - `engine/tests/concurrent_scheduler_test.cpp` — 5 new cancellation suites:
    1. cancel before `run()` — no nodes run, empty log, timestamp recorded
    2. cancel during many ready tasks — sink (dependent on all canceled
       branches) never runs; `run()` returns cleanly (no hang)
    3. cancel during blocked dependencies — downstream blocked nodes never
       succeed; scheduler reports canceled
    4. cancel is idempotent — repeated `cancel()` is a safe no-op
    5. cooperative abort — `sleep_task_cooperative(500ms)` aborts well before
       500ms and reports canceled
- **Key decisions:**
  - **Cooperative, not forced:** `cancel()` does NOT call `pool_.stop()` (which
    would abandon in-flight tasks). Instead it signals the stop_source and lets
    `pool_.drain()` wait for running tasks — cooperatively-canceling tasks
    return promptly, oblivious ones run to completion. No task is left
    mid-mutation. This matches the master prompt's "document cooperative vs
    forced cleanup semantics" requirement.
  - **No dispatch after commit:** the dispatcher loop exits the moment
    `canceled_` is observed, so no node is *started* after cancellation is
    committed (downstream blocked nodes are CANCELED by the state machine, never
    dispatched).
  - **Backward compat:** the original `TaskFn`-taking constructor is retained;
    it wraps each task into a token-ignoring `ConcurrentTaskFn`, so M10 callers
    and tests are unchanged.
- **Concurrency correctness:** `stop_source_`/`canceled_` provide the
  cross-thread signal; `dispatch_cv_` + `dispatch_mu_` coordinate the
  dispatcher; `in_flight_` is atomic. Newly-canceled nodes are not re-dispatched
  because `on_node_complete` short-circuits successor unlocking when canceled.
  ASan+UBSan (Debug) run is clean.
- **No-go compliance:** does not claim "immediate cancellation for arbitrary
  external calls" — only cooperative tasks abort early; oblivious tasks run to
  completion. Documented explicitly. No perf numbers invented.
- **Validation:**
  - `cmake -S engine -B engine/build -G Ninja -DCMAKE_BUILD_TYPE=Release` → ✅
  - `cmake --build engine/build` → ✅ clean under `-Wall -Wextra -Wpedantic -Werror`
  - `ctest --test-dir engine/build --output-on-failure` → ✅ 7/7
  - `ctest --test-dir engine/build-asan --output-on-failure` → ✅ 7/7 (ASan+UBSan)
  - All 7 targets pass; Phase-1 npm 28/28 intact (run as evidence)
- **Phase-1 regression:** `npm test` → ✅ 28/28 (no TS/app code touched).
- **Human action:** none.
- **COMMIT:** `477497c` — `phase2(m11): add scheduler cancellation semantics`
- **NEXT:** M12 — execution resource classes and browser affinity policy.
---

## M12 — Add execution resource classes and browser affinity policy

- **BASE_SHA:** `477497c`
- **What changed:**
  - `engine/core/include/evo/execution_policy.hpp` +
    `engine/core/src/execution_policy.cpp` — `ResourceClass` (Internal/Browser/
    ExternalIo), `ResourcePolicy` (class + affinity_key + capacity), and
    `ExecutionPolicy` that classifies node types **without changing planner
    behavior**: `start` → Internal; `open-url`/`act`/`extract`/`observe`/
    `agent` → Browser keyed by run id (capacity 1); `send-email` → ExternalIo
    (unbounded); unknown → Internal fallback. Override hooks (`set_node_affinity`,
    `set_type_affinity`, `set_capacity`) for tests only.
  - `engine/core/include/evo/concurrent_scheduler.hpp` +
    `engine/core/src/concurrent_scheduler.cpp` — scheduler owns an
    `ExecutionPolicy`; dispatch is now gated on resource capacity **independent
    of dependency readiness**. `resource_usage_` (affinity_key → in-use) and
    `resource_blocked_` (ready-but-waiting) are guarded by `dispatch_mu_`;
    `drain_resource_blocked()` re-attempts deferred nodes when a slot frees;
    `on_node_complete` releases the held resource before unlocking successors.
    `ConcurrentConfig::run_id` seeds the default browser-affinity key.
  - `engine/tests/affinity_scheduler_test.cpp` — 5 suites: default policy
    classification; two ready browser nodes sharing an affinity key never
    overlap; independent affinity keys overlap when capacity permits; non-browser
    work overlaps browser waiting; wide fan of browser nodes all serialize.
  - `engine/tests/concurrent_scheduler_test.cpp` — the M10 overlap suites now use
    `bench:sleep` (non-browser, parallel) for the independent branches; `act`
    correctly stays a capacity-1 browser node (so its branches serialize).
  - `engine/CMakeLists.txt` — added `execution_policy.cpp` to the core lib and the
    `evo_affinity_scheduler_test` CTest target.
- **Key decisions:**
  - **Capacity-1 browser affinity = one session per run.** All Phase-1 browser
    nodes in one run share `run_id` as the affinity key, so they serialize and
    stay on one browser session/worker — preserving the Phase-1 one-session
    invariant. Independent affinity keys (e.g. two runs) parallelize.
  - **Resource gate is a second gate after dependency readiness.** A node is
    dispatched only when (a) deps satisfied AND (b) its resource has spare
    capacity. Deferred nodes are retried, never lost. This matches ARCHITECTURE.md
    §6.4 ("Resource availability controls dispatch, not dependency correctness").
  - **M10 overlap semantics preserved, made correct.** With `act` now correctly
    browser-classified, the overlap tests moved to `bench:sleep` (synthetic,
    unbounded) — so "independent branches overlap" is asserted on non-browser
    work, which is exactly where Phase 2 gains concurrency today.
- **Concurrency correctness:** `resource_usage_`/`resource_blocked_` only mutate
  under `dispatch_mu_`; `drain_resource_blocked` is called both in the dispatch
  loop and from `on_node_complete` (worker thread) under the same lock. No
  double-acquire, no lost wakeup (dispatcher re-checked via `dispatch_cv_`).
  ASan+UBSan (Debug) run is clean.
- **No-go compliance:** does not create multiple Browserbase sessions per
  Phase-1 workflow (capacity-1 enforces single session); does not modify planner
  output schema; no perf numbers invented.
- **Validation:**
  - Release build → ✅ clean under `-Wall -Wextra -Wpedantic -Werror`
  - `ctest --test-dir engine/build` → ✅ 8/8 (added affinity_scheduler)
  - `ctest --test-dir engine/build-asan` → ✅ 8/8 (ASan+UBSan)
  - `npm test` (Phase-1) → ✅ 28/28 (run as evidence; no TS/app code touched)
- **Phase-1 regression:** `npm test` → ✅ 28/28 (no TS/app code touched).
- **Human action:** none.
- **COMMIT:** `cc73f30` — `phase2(m12): add resource-aware scheduling and browser affinity`
---

## M13 — Instrument the scheduler core with evidence-grade timestamps and counters

- **BASE_SHA:** `cc73f30`
- **What changed:**
  - `engine/core/include/evo/metrics.hpp` (NEW) — `RunMetrics` struct with run-level
    counters (`dispatch_count`, `completion_count`, `max_in_flight`,
    `max_queue_depth`, `retry_count` placeholder) and steady_clock markers
    (`run_start`, `run_terminal`, `cancel_requested`). Provides `to_json_string()`
    for benchmark-harness export; durations are milliseconds derived from
    `steady_clock` (never `system_clock`, per §14). Metric definitions follow
    ARCHITECTURE.md §14 formulas (14.2 ready-to-dispatch latency, 14.4 node
    execution latency, 14.5 logical node latency; high-water marks for active
    workers and queue depth).
  - `engine/core/include/evo/concurrent_scheduler.hpp` — `ConcurrentNodeRun` now
    records `became_ready_at` (deps satisfied, when pushed to the ready queue) in
    addition to `ready_at` (popped for dispatch)/`started_at`/`finished_at`, with
    `ready_to_dispatch_latency()` accessor. Scheduler exposes `metrics()`,
    `cancel_requested_at()`, and `run_terminal_at()`. Added `ready_times_` map
    (guarded by `log_mu_`) and run-level counter fields.
  - `engine/core/src/concurrent_scheduler.cpp` — counters update on the hot path:
    `dispatch_count_`/`completion_count_` are atomic (written from worker threads);
    `max_in_flight`/`max_queue_depth` updated under `dispatch_mu_` in
    `update_high_water()`; `run_terminal_at_` is set *before* `finalize_and_collect()`
    populates `metrics_` (fixed a bug where the terminal timestamp was unset in the
    exported JSON because finalization read it too early). `run()` records
    `run_start_time_` on entry.
  - `engine/tests/metrics_test.cpp` (NEW) — 5 suites: timestamp monotonicity
    (`became_ready <= ready <= started <= finished` per node + dependency ordering),
    exact logical counters (`dispatch_count == completion_count == node count`),
    JSON export shape (all expected keys present), cancellation metrics
    (`cancel_requested` + `run_terminal` recorded, `run_terminal >= cancel_requested`
    on clean shutdown), and wide-fan metrics (counters consistent on 9-node graph).
  - `engine/CMakeLists.txt` — added the `evo_metrics_test` CTest target.
- **Measurement/presentation separation:** counters are updated inline on the dispatch
  and completion paths; structured JSON export is a separate `to_json_string()` call
  invoked by tests/harnesses post-run. No locks on the atomic counter hot path;
  high-water marks use a cheap `max` under the existing `dispatch_mu_` (never on the
  worker `log_mu_`).
- **Concurrency correctness:** `dispatch_count_`/`completion_count_` are atomic
  (multi-writer, no lock contention on the metrics hot path); `max_in_flight`/
  `max_queue_depth`/`run_terminal_at_` are set on the dispatcher thread or at
  run-terminal (single-threaded snapshot), so no data race. - **Concurrency correctness (M13 TSan fix):** the initial M13 implementation read
  `ready_times_` under `log_mu_` in `worker_task` while writing it under `dispatch_mu_`
  in `on_node_complete` — a real data race detected by ThreadSanitizer (T21 read / T22
  write on the same `std::map` node). Fixed by guarding all `ready_times_`
  read/write under `dispatch_mu_` consistently (the writer's lock); `worker_task`
  briefly acquires `dispatch_mu_` to snapshot `became_ready_at` — no deadlock since
  the worker does not re-enter the dispatcher.
- **No-go compliance:** did not add CPU/memory metrics (no collector exists yet);
  did not use `system_clock` for durations; did not fabricate performance numbers;
  did not change Phase-1 behavior.
- **Validation:**
  - Release build → ✅ clean under `-Wall -Wextra -Wpedantic -Werror`
  - `ctest --test-dir engine/build --output-on-failure` → ✅ 9/9
  - `ctest --test-dir engine/build-asan --output-on-failure` → ✅ 9/9 (ASan+UBSan,
    no sanitizer errors)
  - `npm test` (Phase-1) → ✅ 28/28 (run as evidence; no TS/app code touched)
  - `./build/evo_metrics_test` → "all metrics tests passed"
- **Phase-1 regression:** `npm test` → ✅ 28/28 (no TS/app code touched).
- **Human action:** none.
- **COMMIT:** `916ffc9` — `phase2(m13): instrument scheduler metrics`
- **NEXT:** M14 — deterministic test fixtures and property-based equivalence testing.
---

## M14 — Harden C++ correctness with sanitizers and concurrency stress

- **BASE_SHA:** `916ffc9`
- **What changed:**
  - `engine/CMakeLists.txt` — added CMake options `EVO_ENABLE_ASAN` and
    `EVO_ENABLE_TSAN` (mutually exclusive; documented). Sanitizer flags apply
    globally via `add_compile_options`/`add_link_options` so the core library and
    all test executables are instrumented consistently (avoids ASan ODR/double-
    free false positives from mismatched library/test sanitization). ASan builds
    force Debug mode for meaningful stack traces. Added the `evo_stress_test` CTest
    target.
  - `engine/tests/stress_test.cpp` (NEW) — stress suites: (1) 120 deterministic
    random-DAG runs with seed logging, asserting concurrent succeeded-set + node
    count == sequential reference each iteration; (2) 50× ThreadPool construction +
    task-burst + drain cycles; (3) 50× full scheduler construction/run/drain cycles
    (diamond graph); (4) 40× cancel-vs-completion contention with cooperative
    2ms bench:sleep tasks on an 8-leaf fan-in graph (verifies clean terminal state
    + no double-execution under cancellation race).
  - `engine/core/src/concurrent_scheduler.cpp` / `concurrent_scheduler.hpp` —
    **TSan race fix**: `ready_times_` (added in M13 for `became_ready_at`) was
    read under `log_mu_` in `worker_task` but written under `dispatch_mu_` in
    `on_node_complete`. ThreadSanitizer reported a data race (read/write on the
    same `std::map` node from concurrent pool threads). Fixed by guarding all
    `ready_times_` accesses under `dispatch_mu_` consistently; `worker_task`
    briefly snapshots `became_ready_at` under `dispatch_mu_` (no deadlock since
    the worker does not re-enter the dispatcher loop).
  - `.gitignore` — added `/Testing` (CTest-generated artifact directory).
- **Key decisions:**
  - Used `add_compile_options` (global) over per-target flags so the whole binary is
    instrumented consistently — a partially-sanitized build produces misleading
    ODR diagnostics under ASan.
  - The 120-iteration stress asserts *equivalence to the sequential reference*
    (same succeeded node set, same node count, all succeeded), not just
    concurrency overlap — catching subtle correctness regressions, not just races.
  - Cancel-vs-completion uses 2ms cooperative tasks to maximize the chance of
    cancellation racing completion, exercising the stop_token path under contention.
- **Concurrency correctness:** TSan found and the fix above resolved a real
  `ready_times_` race from M13. ASan+UBSan remains clean. All atomic counters
  unchanged. No data race remains (verified by clean TSan run on 10/10 tests).
- **No-go compliance:** did not enable ASan and TSan together (mutually exclusive
    checked at configure time); no `-Werror` on third-party headers (none used);
    no invented performance numbers; diagnostic timing in stress tests is labeled
    `diag` and explicitly not a perf claim.
- **Validation:**
  - Release: `ctest --test-dir engine/build` → ✅ 10/10 (incl. stress, ~64s)
  - ASan+UBSan (`-DEVO_ENABLE_ASAN=ON` Debug): `ctest --test-dir engine/build-asan` → ✅ 10/10, no errors
  - TSan (`-DEVO_ENABLE_TSAN=ON` Debug): `ctest --test-dir engine/build-tsan` → ✅ 10/10, no warnings (after M13 race fix)
  - `./build/evo_stress_test` → "all stress tests passed" (120 DAG + 50 pool + 50 sched + 40 cancel-race)
  - `npm test` (Phase-1) → ✅ 28/28 (no TS/app code touched)
- **Platform notes:** TSan runs successfully on macOS arm64 with Apple clang 21.
  No suppressions file needed (no real project bugs suppressed).
- **Phase-1 regression:** `npm test` → ✅ 28/28 (no TS/app code touched).
- **Human action:** none.
- **COMMIT:** `b361a4e` — `phase2(m14): harden concurrent core with sanitizers and stress`
- **NEXT:** M15 — first local benchmark corpus and sequential-vs-concurrent evidence.
---

## M15 — Create the first local benchmark corpus and sequential-vs-concurrent evidence

- **BASE_SHA:** `b361a4e`
- **What changed:**
  - `engine/app/bench_runner.cpp` (NEW) — benchmark corpus runner. Drives both
    the sequential reference (`Scheduler`, M06) and the concurrent scheduler
    (`ConcurrentScheduler`, M10) over identical DAGs and identical synthetic tasks
    (`bench:sleep` = 3ms per node). Benchmarks 4 workloads (linear, diamond, wide
    16-leaf, layered 3×6) plus 2 seeded random DAGs
    (`generate_workload(seed, w, d)`), across worker counts {1,2,4,8}, with 2
    warmup + 10 measurement trials each. Records machine/compiler/commit
    metadata and writes raw per-trial CSV + summary (p50/p95/p99, speedup vs the
    *identical* sequential reference). All timing uses `steady_clock`.
  - `engine/CMakeLists.txt` — `EVO_BUILD_COMMIT` is now PUBLIC on
    `evo_scheduler_core` so the bench runner reports the commit; added the
    `evo-bench` executable target.
  - `docs/phase2/LOCAL_SCHEDULER_BENCHMARK.md` (NEW) — methodology + measured
    results table (this is the evidence document for M15).
  - `engine/bench-results/manifest.csv` (NEW, committed artifact) — raw samples
    from the measured run (commit `6c24953`, Release, Apple M2 arm64, clang 21).
- **Key decisions:**
  - Speedup is normalized to the **sequential reference running the same tasks**
    (benchmark rule 7), never to wall-clock guesses.
  - `nw=1` is included as the no-parallelism baseline; its sub-1.0 speedup is
    reported as concurrency overhead, not hidden.
  - Slower/saturating cases (`diamond` caps at 1.5×, `random` saturates ~2.4× at
    `nw=4`) are included and explained (critical-path / leaf-count bounds).
  - Numbers are reported with full provenance (workload, build mode, hardware,
    sample count, commit) — they are NOT universal claims.
- **Measured evidence (summary, median makespan, speedup = seq/conc):**
  - `linear`: 0.96–0.97× at all worker counts (overhead only; no parallelism)
  - `diamond`: 1.45–1.47× at nw≥2 (2 independent leaves)
  - `wide`: 1.94× @2, 3.90× @4, **7.63× @8** (16 independent leaves, saturates cores)
  - `layered`: 1.98× @2, 3.52× @4, **5.77× @8** (layered parallelism + inter-layer deps)
  - `random seed=12345`: saturates 2.47× at nw≥4 (limited critical-path parallelism)
  - `random seed=99999`: saturates 2.42× at nw≥4 (same saturation shape)
- **Concurrency correctness:** no new mutable state added in M15 (benchmarks read
  the engine APIs added in M10–M13, which are already TSan-clean). No new data
  races introduced.
- **No-go compliance:** Release-only evidence (never Debug); saturation/overhead
  cases included and explained, not hidden; no fabricated numbers; no perf claims
  made on synthetic `bench:*` tasks as if they were real browser I/O; no commit of
  secrets.
- **Validation:**
  - Release: `ctest --test-dir engine/build` → ✅ 10/10 (incl. stress)
  - ASan+UBSan: `ctest --test-dir engine/build-asan` → ✅ 10/10
  - TSan: `ctest --test-dir engine/build-tsan` → ✅ 10/10 (no new races)
  - `./engine/build/evo-bench engine/bench-results` → ✅ wrote `manifest.csv`
    (commit `6c24953` recorded in artifact metadata)
  - `npm test` (Phase-1) → ✅ 28/28 (no TS/app code touched)
- **Phase-1 regression:** `npm test` → ✅ 28/28 (no TS/app code touched).
- **Human action:** none.
- **COMMIT:** `bea4b06` — `phase2(m15): benchmark local concurrent scheduler`
- **NEXT:** M16 — define shared Protobuf/gRPC execution contract.
---

## M16 — Define the shared Protobuf/gRPC execution contract

- **BASE_SHA:** `b361a4e`
- **What changed:**
  - `engine/proto/evo/execution.proto` (NEW) — versioned schema under package
    `evo.execution.v1`. Defines the message envelope model and the service
    surface:
    - Enums: `NodeState`, `RunOutcome`, `ResourceClass` (matches ARCHITECTURE.md §5
      and M12), `ResultEnvelope.StatusCode`, `RunStatus`.
    - Messages: `TaskEnvelope` (run/workflow/org/node/attempt/resource class/
      affinity key/trace/span/type/payload/became-ready timestamp),
      `ResultEnvelope` (completed/output/error/status/finished timestamp/abandoned),
      `SubmitRunRequest/Response`, `CancelRunRequest/Response`,
      `GetRunRequest/Response` (with `NodeStatus`), `HealthRequest/Response`.
    - `ControlService` with RPCs `SubmitRun`, `CancelRun`, `GetRun`, `Health`.
    gRPC transport stubs are **not** generated (no `protoc-gen-grpc` plugin
      installed — documented; wired in M17), but the service surface is defined.
  - `engine/proto/evo_gen/evo/execution.pb.cc` + `.pb.h` (generated) — C++
    protobuf bindings via `protoc` 33.2 (Homebrew). Committed as generated
    artifacts with the exact compiler version noted for reproducibility.
  - `engine/CMakeLists.txt` — `find_package(protobuf CONFIG)`, new `evo_proto`
    STATIC target (SYSTEM protobuf includes, links `protobuf::libprotobuf`);
    `EVO_BUILD_COMMIT` made PUBLIC so the bench runner reports provenance;
    `evo_contract_test` target added (strict `-Werror`; proto code isolated in
    `evo_proto` so generated warnings don't trip the core's `-Werror`).
  - `engine/tests/contract_test.cpp` (NEW, C++) — golden round-trip tests: stable
    field numbers (versioning axis), `SubmitRunRequest`/`TaskEnvelope`/
    `ResultEnvelope` encode→decode→equal, wall-clock `google.protobuf.Timestamp`
    round-trips (§7), and the `ControlService` RPC surface (4 RPCs).
  - `features/workflows/lib/contract-roundtrip.test.ts` (NEW, TS) — golden
    round-trip via the already-installed `protobufjs` 7.6.5 library (runtime load
    of the `.proto` + well-known `timestamp.proto` from Homebrew include path):
    assert field-value preservation for `SubmitRunRequest`, `TaskEnvelope`,
    `ResultEnvelope`, and the 4-RPC `ControlService` surface.
  - `package.json` — added the TS contract test to the `test` script.
  - `docs/phase2/LOCAL_SCHEDULER_BENCHMARK.md` (NEW) — methodology + measured
    evidence for M15 (M15 doc, included here as it's the evidence document).
  - `engine/bench-results/manifest.csv` (NEW, committed) — raw samples for M15.
- **Key decisions:**
  - Field numbers are the versioning axis (additive-only); a golden test pins them.
  - Timestamps crossing the process boundary are `google.protobuf.Timestamp` =
    wall-clock UTC (§7, M16 rule 16); in-process scheduling latency stays
    steady_clock (M13) and is NOT in this proto.
  - `node_payload_json` is opaque to transport and carries no secrets by contract;
    resource class/affinity key are first-class envelope fields (M12).
  - TS bindings use the already-installed `protobufjs` runtime (no new dependency
    added); typed codegen via `pbts`/`ts-proto` is deferred to M17.
  - Protobuf headers are SYSTEM includes and generated code is in a separate
    `evo_proto` target so `-Werror` on project code does not flag third-party
    generated warnings (no-go: "No -Werror on third-party headers").
- **Concurrency/distributed correctness:** the proto is static schema (no mutable
  state). Ownership/lifetime of envelopes (M18+) and duplicate-delivery idempotency
  (M33) are future work — marked as such, not implemented.
- **No-go compliance:** did not put browser API keys or auth tokens in payloads;
  did not expose React Flow coordinates to the protocol; did not generate gRPC
  stubs (documented as M17); no invented perf numbers.
- **Validation:**
  - C++ proto gen: `protoc ... --cpp_out` → ✅ `execution.pb.cc/.pb.h`
  - C++ compile: `cmake --build build` → ✅ clean
  - `./build/evo_contract_test` → ✅ "all contract tests passed" (field numbers,
    3 message round-trips incl. wall-clock timestamp, 4 RPC surface)
  - ASan+UBSan: `ctest --test-dir engine/build-asan -R contract` → ✅ pass
  - TS typecheck: `npx tsc --noEmit` → ✅ exit 0
  - TS lint: `npm run lint` (contract-roundtrip.test.ts) → ✅ 0 errors
  - TS golden: `npx tsx features/workflows/lib/contract-roundtrip.test.ts` → ✅ 4/4
  - `npm test` (full) → ✅ 32/32 suites pass (28 Phase-1 + 4 contract)
  - `ctest --test-dir engine/build` → ✅ 11/11 (incl. contract + stress)
- **Phase-1 regression:** `npm test` → ✅ 32/32 (28 Phase-1 + 4 new contract suites;
  Phase-1 tests unchanged and green).
- **Human action:** none.
- **COMMIT:** `dbaecf8` — `phase2(m16): define versioned grpc protocol`
- **Human action:** `brew install grpc` (now complete) — installed grpc 1.83.0.
- **Validation:**
  - C++ compile (Release, arm64): `cmake --build build` → ✅ clean (system
    grpc 1.83.0; pkg-config `grpc++` for complete absl link set).
  - `./build/evo-scheduler-server` starts and serves `Health` → ✅ `ok: true`.
  - Integration (`tests/grpc_integration_test.cpp`, spawns server subprocess):
    Health ✅, SubmitRun(diamond DAG) accepted ✅, GetRun polled to terminal
    RUN_SUCCEEDED with n0/n1/n2 all NODE_STATE_SUCCEEDED ✅, CancelRun ok ✅.
  - `ctest --test-dir engine/build -R grpc_integration` → ✅ pass (1.5s).
  - Release CTest → ✅ 12/12 (M10–M17, incl. stress + grpc_integration).
  - ASan+UBSan → ✅ 11/11 (incl. grpc_integration + stress).
  - TSan → ✅ 12/12 (no data races; incl. grpc_integration + stress).
  - Phase-1 regression: `npm test` → ✅ 32/32 (28 Phase-1 + 4 contract).
  - TS contract regression: `npx tsc --noEmit` + `npm run lint` → ✅ exit 0.
- **COMMIT:** `7cf04f6` — `M17: gRPC ControlService (SubmitRun/CancelRun/GetRun/Health)`
- **NEXT:** M18 — Create isolated local Redis + PostgreSQL infrastructure.

---

## M18 — Create isolated local Redis + PostgreSQL infrastructure

- **BASE_SHA:** `f2e1e88`
- **What was inspected:** master prompt M18 spec, `AGENTS.md`, `.env.example`,
  `.gitignore`, `drizzle.config.ts` (Neon connection convention), host toolchain
  (Docker 29.7.2 aarch64 + Compose v5.4.0, Redis 8.4.0 local, Homebrew Postgres).
- **What changed:**
  - `infra/phase2/docker-compose.yml` — compose project `evo-phase2`:
    `redis:7.4-alpine` + `postgres:16-alpine`, loopback-only port bindings
    (`127.0.0.1:6390`, `127.0.0.1:5433`), health checks, named volumes,
    env-overridable ports/credentials (`EVO_PHASE2_*`).
  - `scripts/phase2/{lib,up,down,reset,health}.sh` — start/stop/reset/verify
    with fail-fast Docker guards. `reset.sh` only removes volumes owned by
    the `evo-phase2` compose project; it never references `DATABASE_URL` /
    `DATABASE_URL_UNPOOLED` / Neon.
  - `docs/phase2/LOCAL_INFRA.md` — layout, endpoints, commands, and how the
    Phase-2 engine will later use the same schema against standard
    Postgres/Neon (additive Drizzle migrations, portable SQL only).
  - `.env.example` — documented the optional `EVO_PHASE2_*` local defaults
    (non-secret, loopback-only).
  - `docs/phase2/DECISIONS.md` — caught up M06–M17 decision entries; added M18.
- **Concurrency/distributed correctness:** this milestone introduces no mutable
  application state — only container lifecycle. Ownership: Docker daemon owns
  container/volume state; scripts are idempotent (`up` on a running stack is a
  no-op; `down` on a stopped stack is a no-op). Duplicate invocation is safe.
  Crash/shutdown semantics: `down` preserves volumes; `reset` is the only
  destructive path and is local-only by construction.
- **No-go compliance:** reset never points at a remote DB; no secrets committed
  (credentials are documented non-secret local defaults); no perf numbers
  invented; Phase-1 default behavior untouched (app still uses Neon).
- **Validation:**
  - `docker compose -f infra/phase2/docker-compose.yml config` → ✅ valid
  - `scripts/phase2/up.sh` → ✅ both containers Healthy (redis, postgres)
  - `scripts/phase2/health.sh` → ✅ redis PONG; postgres ok
    (PostgreSQL 16.15 on aarch64-unknown-linux-musl)
  - Port binding check → ✅ `127.0.0.1:6390->6379`, `127.0.0.1:5433->5432`
    (loopback only, not exposed off-machine)
  - `scripts/phase2/down.sh` → ✅ clean shutdown (network removed, volumes kept)
  - `scripts/phase2/reset.sh` → ✅ volumes wiped, fresh stack Healthy
  - Phase-1 regression: `npm test` → ✅ 32/32 (28 Phase-1 + 4 contract)
  - Engine: `ctest --test-dir engine/build` → ✅ 12/12 (incl. grpc_integration)
- **Human action:** Docker Desktop (ARM) installed and running — complete.
- **COMMIT:** `813da0a` — `phase2(m18): add isolated redis postgres dev stack`
- **NEXT:** M19 — Add additive Phase-2 Drizzle schema and migrations.

---

## M19 — Add additive Phase-2 Drizzle schema and migrations

- **BASE_SHA:** `e2e1906`
- **What was inspected:** master prompt M19 spec, `lib/db/schema.ts` (Phase-1:
  3 tables), `lib/db/index.ts` (neon-http pooled client), `drizzle.config.ts`,
  `lib/db/migrations/` (empty — Phase-1 was applied via `db:push`, no committed
  history), drizzle-orm 0.45.2 / drizzle-kit 0.31.10 pg-core API surface.
- **What changed:**
  - `lib/db/schema.ts` — five ADDITIVE tables (Phase-1 tables untouched):
    - `workflow_versions` — immutable graph snapshots; unique
      `(workflow_id, version_number)`; org index.
    - `workflow_runs` — engine-neutral runs with `engine` discriminator
      (`legacy` default | `evo`), status/outcome, wall-clock UTC timestamps;
      indexes on `(org_id, created_at)`, `workflow_id`, `status`.
    - `node_runs` — unique `(run_id, node_id)` identity; FK → workflow_runs.
    - `task_attempts` — unique `(node_run_id, attempt_number)`; FK → node_runs.
    - `idempotency_records` — `key` primary key for duplicate-request dedup (M33).
  - `lib/db/migrations/0000_phase1_baseline.sql` — honest baseline generated
    from the Phase-1-only schema (Phase-1 was previously push-only).
  - `lib/db/migrations/0001_phase2_run_schema.sql` — purely additive: 5 CREATE
    TABLE + FKs + indexes; zero ALTERs on Phase-1 tables.
  - `scripts/phase2/migrate-local.sh` — applies committed migrations to the
    local container only (drizzle-kit's migrator uses the neon-serverless
    driver which cannot open plain TCP to local Postgres); tracks applied files
    in `phase2_migrations_applied`; idempotent re-runs.
  - `scripts/phase2/schema-smoke.sh` — 16 assertions: tables exist, fixture
    inserts, unique constraints reject duplicates (version number, node
    identity, attempt number, idempotency key), legacy/evo coexistence, cleanup.
- **Concurrency/distributed correctness:** Postgres owns all mutable state;
  unique constraints are the invariant guards (duplicate delivery → constraint
  violation, caller resolves by reading existing row). Crash between two
  durable writes: each migration applies in one transaction
  (`--single-transaction`); app-level two-write atomicity is M26's job.
  Timestamps are wall-clock UTC (`now()`); steady_clock never persisted here.
- **No-go compliance:** migration NOT applied to Neon (local Docker only);
  no columns/tables removed; no secrets committed; Phase-1 behavior unchanged
  (nothing in the app imports the new tables yet).
- **Validation:**
  - `npx drizzle-kit generate --name phase1_baseline` → ✅ 3 tables
  - `npx drizzle-kit generate --name phase2_run_schema` → ✅ 8 tables, additive diff
  - `scripts/phase2/migrate-local.sh` → ✅ 2 migrations applied to local Postgres
  - `scripts/phase2/schema-smoke.sh` → ✅ 16/16 assertions pass
  - `npm run typecheck` → ✅ exit 0
  - `npm run lint` → ✅ exit 0
  - Phase-1 regression: `npm test` → ✅ 32/32 (28 Phase-1 + 4 contract)
- **Human action:** none (local Postgres only; Neon migration requires explicit
  approval and is deferred).
- **COMMIT:** `f63d9b4` — `phase2(m19): add durable phase2 run schema`
- **NEXT:** M20 — Implement immutable workflow versions and optimistic concurrency.

---

## M20 — Implement immutable workflow versions and optimistic concurrency

- **BASE_SHA:** `2f29b97`
- **What was inspected:** master prompt M20 spec, `features/workflows/actions.ts`
  (runWorkflowAction), `features/workflows/data.ts` (saveWorkflowGraph),
  `features/workflows/tasks/run-workflow.ts` (graph read path),
  `features/workflows/nodes/node-registry.ts` (StepNodeType), test conventions
  (tsx + node:assert, no DB in Phase-1 tests), drizzle-orm 0.45.2
  node-postgres adapter, tsx `@/` alias resolution.
- **What changed:**
  - `features/workflows/lib/workflow-versions.ts` — new module:
    - `canonicalGraphHash()` — deterministic sha256 over nodes (sorted by id)
      and edges (sorted by source→target).
    - `createWorkflowVersion()` — creates an immutable snapshot; dedupes
      identical graphs by hash (rerun-without-edit reuses the version);
      concurrent writers race on the unique `(workflow_id, version_number)`
      constraint and retry with a fresh candidate (bounded, 5 attempts).
    - `getWorkflowVersion()` / `listWorkflowVersions()` — tenant-scoped reads.
    - `saveWorkflowGraphOptimistic()` — rejects a save whose `expectedVersion`
      is behind the current version with `WorkflowVersionConflictError`;
      omitting `expectedVersion` keeps Phase-1 behavior. `saveFn` injectable
      for tests.
    - All functions take an optional `db` (defaults to `getDb()`) so
      integration tests run against local Postgres via node-postgres.
  - `features/workflows/actions.ts` — `runWorkflowAction` now snapshots the
    approved graph into an immutable version before triggering the run.
    FAIL-OPEN for legacy: the Phase-2 tables may not exist on Neon yet
    (migration requires explicit human approval), so a snapshot failure logs a
    warning and the Phase-1 run continues unchanged.
  - `features/workflows/lib/workflow-versions.test.ts` — 8 integration
    assertions against local Phase-2 Postgres: concurrent creation → distinct
    monotonic versions; unchanged rerun reuses snapshot; edit creates new
    snapshot; tenant guard; stale-save conflict; current-version save;
    legacy no-token path; monotonic max. Skips cleanly (exit 0) when the local
    stack is down.
  - `package.json` — added `pg` 8.23.0 (+ `@types/pg`); wired the new test
    into `npm test`.
- **Concurrency/distributed correctness:** Postgres owns version state; the
  unique constraint is the invariant guard (no transactions needed on
  neon-http). Duplicate delivery of a snapshot request dedupes by graph_hash.
  Crash between save and snapshot: snapshot is created after the canonical
  save; a crash in between leaves the canonical graph saved but no version —
  the next run creates one (no corruption). A late/stale save cannot overwrite
  a newer version (optimistic check). Liveblocks CRDT editing is untouched
  (no-go honored).
- **No-go compliance:** no workflow_version row is ever mutated after
  creation; Liveblocks behavior unchanged; Phase-1 default behavior unchanged
  (fail-open hook); no secrets committed.
- **Validation:**
  - `npx tsx features/workflows/lib/workflow-versions.test.ts` → ✅ 8/8
  - `npm test` → ✅ 40/40 (28 Phase-1 + 4 contract + 8 M20 versioning)
  - `npm run typecheck` → ✅ exit 0
  - `npm run lint` → ✅ exit 0
- **Human action:** none (local Postgres only).
- **COMMIT:** `0ea1c1c` — `phase2(m20): immutable workflow versions + optimistic concurrency`
- **NEXT:** M21 — Implement Redis Streams transport in the C++ scheduler.

---

## M21 — Implement Redis Streams transport in the C++ scheduler

- **BASE_SHA:** `e686f27`
- **What was inspected:** master prompt M21 spec + §2.3 (why Redis Streams) +
  Appendix D (Redis Streams semantics checklist), `engine/proto/evo/execution.proto`
  (TaskEnvelope/ResultEnvelope), `engine/CMakeLists.txt` (target layout,
  pkg-config gRPC pattern), `engine/core/include/evo/ready_queue.hpp` (style),
  hiredis 1.4.1 API, host Homebrew prefixes (Intel `/usr/local` vs ARM
  `/opt/homebrew`).
- **What changed:**
  - `engine/core/include/evo/transport.hpp` + `core/src/transport.cpp` —
    `TaskTransport` abstraction (ensure_group/publish/read/ack/pending_count/
    stream_length) + `InMemoryTransport` fake mirroring Redis Streams semantics
    (append-only, per-group FIFO, pending-until-ack, reclaim/redelivery) +
    namespaced key helpers (`task/result/control_stream_key`). Scheduler-core
    tests use the fake; no live Redis needed.
  - `engine/redis/include/evo/redis_transport.hpp` + `redis/src/redis_transport.cpp`
    — `RedisTransport` over hiredis: XGROUP CREATE (idempotent, BUSYGROUP=ok),
    XADD (binary-safe argv), XREADGROUP (at-least-once, stop_token-aware block
    slices), XACK (workers only; late/dup ack harmless), XPENDING, XLEN.
    Single mutex-guarded redisContext with bounded exponential-backoff
    reconnect (base 50ms, cap 2s, max 5 retries/op).
  - `engine/third_party/build-hiredis.sh` — reproducible arm64 static hiredis
    build into `engine/third_party/hiredis-prefix/` (gitignored). Needed
    because the machine's `brew` on PATH is the Intel prefix (x86_64) while the
    engine builds arm64 against `/opt/homebrew`; ARM brew was blocked by an
    unrelated untrusted-tap policy, so a self-contained source build avoids
    changing the user's brew state.
  - `engine/CMakeLists.txt` — `transport.cpp` in core; `evo_redis_transport`
    target (hiredis headers SYSTEM, `-Wno-c99-extensions` for sds.h flexible
    arrays); `transport` + `redis_transport` CTest targets (redis one only when
    the hiredis prefix exists).
  - `engine/tests/transport_test.cpp` — 20 in-memory assertions incl. 200-msg
    concurrent publish/read race smoke.
  - `engine/tests/redis_transport_test.cpp` — 18 assertions against local Redis:
    enqueue/read/pending/ack, duplicate payload → new stream id (transport does
    NOT dedupe; app-level dedupe is by envelope identity in M33), deterministic
    TaskEnvelope encoding round-trip, idempotent group create, late/dup ack.
    Skips cleanly when local Redis is unreachable.
  - `.gitignore` — ignore `engine/third_party/hiredis-src/` + `hiredis-prefix/`.
- **Concurrency/distributed correctness:** Redis owns stream state; the
  transport owns one mutex-guarded connection. At-least-once delivery: read
  marks pending, only the worker acks (scheduler never acks on behalf of
  workers — M21 no-go honored). Duplicate delivery is expected and safe (dedupe
  is app-level, M33). Reconnect is bounded; exhausted retries surface as
  failure to the caller's retry policy. Timestamps in envelopes are wall-clock
  UTC (proto Timestamp); transport itself adds no timestamps.
- **No-go compliance:** no ack on behalf of workers; keys namespaced with
  explicit prefix; no invented perf numbers; scheduler-core tests still use the
  in-memory fake; Phase-1 untouched.
- **Validation:**
  - `cmake --build engine/build` → ✅ clean (arm64)
  - `./build/evo_transport_test` → ✅ 20/20 in-memory
  - `./build/evo_redis_transport_test` → ✅ 18/18 vs local Redis
  - Release CTest → ✅ 14/14
  - ASan+UBSan CTest → ✅ 14/14
  - TSan CTest → ✅ 14/14 (no data races in transport)
- **Human action:** none (hiredis built from source locally; no system changes).
- **COMMIT:** `47e565e` — `phase2(m21): add redis streams task transport`
- **NEXT:** M22 — Finalize task/result envelope semantics and event transport.

---

## M22 — Finalize task/result envelope semantics and event transport

- **BASE_SHA:** `30f2809`
- **What was inspected:** master prompt M22 spec, `engine/proto/evo/execution.proto`
  (TaskEnvelope/ResultEnvelope field numbers), `engine/proto/generate.sh`,
  `engine/tests/contract_test.cpp` + `features/workflows/lib/contract-roundtrip.test.ts`
  (existing cross-language pattern), protobufjs enum decode behavior.
- **What changed:**
  - `engine/proto/evo/execution.proto` — ADDITIVE proto extension (no field
    numbers reused):
    - New `ErrorClass` enum (TRANSIENT/PERMANENT/RESOURCE_EXHAUSTED/CANCELED).
    - `ResultEnvelope` gains `error_class`(11), `optional bool retryable`(12),
      `worker_id`(13), `started_at`(14). `optional` gives explicit presence so
      "hint unset" is distinguishable from explicit false.
    - Documented identity semantics on TaskEnvelope: logical task id =
      (run_id,node_id); attempt id = (run_id,node_id,attempt_number); transport
      message ids are NOT identity. Payload rule: node_payload_json carries only
      execution inputs, never UI state or secrets.
    - Regenerated `engine/proto/evo_gen/evo/execution.pb.{cc,h}` (grpc.pb
    unchanged — service surface untouched).
  - `engine/core/include/evo/envelope.hpp` + `core/src/envelope.cpp` — new
    `evo_envelope` target (links evo_proto, not transport):
    - `kMaxEnvelopeBytes` = 256 KiB size limit.
    - `validate_task_envelope` / `validate_result_envelope` — identity, attempt
      numbering, completed/error consistency, size limit.
    - `AttemptKey` + `ResultDedupe` — thread-safe dedupe by attempt id.
    - `is_late_result` — late/stale results ignored (never overwrite newer state).
    - `should_consider_retry` — combines error_class + worker retryable hint
      with a policy floor (permanent/canceled never retry).
  - `engine/tests/envelope_test.cpp` — 20 semantics assertions (validation,
    size limits, malformed, dedupe, late-result, retry-hint).
  - `engine/tests/envelope_fixture_gen.cpp` — deterministic golden fixture
    generator; writes `engine/tests/fixtures/{task,result}_envelope.bin`
    (committed) with a byte-stability self-check.
  - `features/workflows/lib/envelope-crosslang.test.ts` — TS decodes the C++
    golden bytes with protobufjs, asserts M22 fields preserved, and asserts
    BYTE-IDENTICAL re-encode (C++ <-> TS wire compatibility). Plus malformed
    (truncated/garbage) rejection and size-limit detection.
  - `engine/CMakeLists.txt` — `evo_envelope` lib + `envelope` test +
    `evo-envelope-fixture-gen` tool.
  - `package.json` — wired the cross-language test into `npm test`.
- **Concurrency/distributed correctness:** dedupe is a mutex-guarded set keyed
  by attempt id (duplicate delivery → first applied, rest ignored). Late-result
  rule guarantees a stale attempt can never overwrite newer logical state.
  Validation runs before any durable mutation (trust boundary). Timestamps in
  envelopes are wall-clock UTC; this module adds none.
- **No-go compliance:** additive proto only (no field reuse); no secrets in
  payloads; no invented perf numbers; Phase-1 untouched; scheduler-core still
  uses the in-memory fake (envelope target does not need Redis).
- **Validation:**
  - `bash engine/proto/generate.sh` → ✅ regenerated (byte-stable)
  - `./build/evo_envelope_test` → ✅ 20/20
  - `./build/evo-envelope-fixture-gen` → ✅ fixtures written (byte-stable)
  - `npx tsx features/workflows/lib/envelope-crosslang.test.ts` → ✅ 8/8
  - `npm test` → ✅ 48/48 (28 Phase-1 + 4 contract + 8 M20 + 8 M22)
  - `npm run typecheck` → ✅ exit 0; `npm run lint` → ✅ exit 0
  - Release CTest → ✅ 15/15; ASan+UBSan → ✅ 15/15; TSan → ✅ 15/15
- **Human action:** none.
- **COMMIT:** `ad88fd0` — `phase2(m22): harden distributed task result protocol`
- **NEXT:** M23 — Build the TypeScript distributed worker service.

---

## M23 — Create the TypeScript distributed worker service

- **BASE_SHA:** `d7a2118`
- **What was inspected:** master prompt M23 spec, `worker/` (new dir),
  `engine/proto/evo/execution.proto` (envelope contract), M21 RedisTransport
  semantics (to mirror in TS), M22 envelope semantics (dedupe/late-result),
  ioredis 6.0.0 API, tsconfig include scope (`**/*.ts`), package.json
  `"type": "module"` (ESM).
- **What changed:**
  - `worker/src/redis-streams.ts` — `RedisStreamsClient` (ioredis): mirrors the
    C++ RedisTransport — ensureGroup (idempotent, BUSYGROUP=ok, startId `$`/`0`),
    publish (XADD), readGroup (XREADGROUP at-least-once), ack (XACK, late/dup
    harmless), pendingCount (XPENDING), streamLength (XLEN). Namespaced key
    helpers matching the C++ `task/result/control_stream_key`.
  - `worker/src/envelope-codec.ts` — protobufjs codec loading the shared proto;
    `decodeTaskEnvelope`, `encodeTaskEnvelope`, `encodeResultEnvelope`;
    ResultStatus/ErrorClass numeric constants; wall-clock UTC timestamps.
    Self-contained Timestamp fallback if Homebrew headers absent.
  - `worker/src/worker.ts` — `Worker` class: joins the task-stream consumer
    group, claims TaskEnvelopes, executes via a pluggable `TaskExecutor`,
    publishes ResultEnvelope, and acks ONLY after the result is durably
    published (M23 "durable handoff" rule). Malformed envelopes are quarantined
    (logged + acked). Graceful `stop()`: stops claiming, drains in-flight up to
    `drainTimeoutMs`, leaves unfinished tasks pending for redelivery (never
    silently abandoned). Stable `workerId` (generated or configured).
  - `worker/src/synthetic-executor.ts` — deterministic synthetic executor
    (`bench:sleep`/`bench:burn`/`bench:fail`/`bench:echo`), explicitly
    NOT a production executor; real executors wired in M24 behind the same
    `TaskExecutor` interface.
  - `worker/src/main.ts` — standalone entry point (separate from Next.js and
    Trigger.dev) with SIGTERM/SIGINT graceful shutdown; env-var config, no
    secrets required for the local stack.
  - `worker/src/worker.test.ts` — integration test vs local Redis: 1 worker
    (5 tasks), 2 workers (8 tasks), 4 workers (20 tasks, exactly 20 results —
    no loss/dup), graceful shutdown drains in-flight work, result stream
    carries decodable envelopes. Skips cleanly when Redis is down.
  - `worker/Dockerfile` — worker image (node:20-alpine), loads the shared
    proto at runtime; no secrets baked in.
  - `infra/phase2/docker-compose.yml` — opt-in `worker` service behind the
    `worker` profile (`--profile worker up --scale worker=4`); default
    redis+postgres stack unchanged.
  - `package.json` — added `ioredis` 6.0.0; wired worker test into `npm test`.
  - `features/workflows/lib/workflow-versions.test.ts` — fixed a race-dependent
    assumption (concurrent version creation assigns numbers nondeterministically;
    the rerun-dedupe step now uses whichever graph actually became the latest).
- **Concurrency/distributed correctness:** Redis owns stream state; the worker
  owns one connection. At-least-once delivery; duplicate results are deduped
  scheduler-side by attempt id (M22). Durable-handoff rule: result published
  BEFORE ack; if publish fails the task stays pending for redelivery. Graceful
  shutdown leaves unfinished tasks pending (a slow worker is not confused with
  a dead one at the ack layer). Malformed envelopes quarantined at the trust
  boundary. Result timestamps are wall-clock UTC.
- **No-go compliance:** worker does not ack before durable handoff; synthetic
  executor is clearly non-production; no secrets committed; Phase-1 untouched;
  default compose stack unchanged (worker is profile-gated).
- **Validation:**
  - `npx tsx worker/src/worker.test.ts` → ✅ 5/5 (1/2/4 workers + shutdown + result decode)
  - `npm test` → ✅ 53/53 (28 Phase-1 + 4 contract + 8 M20 + 8 M22 + 5 M23)
  - `npm run typecheck` → ✅ exit 0; `npm run lint` → ✅ exit 0
  - `docker compose -f infra/phase2/docker-compose.yml config` → ✅ valid
    (default: redis+postgres; `--profile worker`: +worker)
- **Human action:** none.
- **COMMIT:** `dd01b19` — `phase2(m23): add scalable typescript worker service`
- **NEXT:** M24 — Reuse existing interpolation and node executors inside distributed workers.

---

## M24 — Reuse existing interpolation and node executors inside distributed workers

- **BASE_SHA:** `dd01b19`
- **What was inspected:** master prompt M24 spec, `features/workflows/nodes/
  node-executors.ts` (NodeExecutor/NodeContext), `features/workflows/lib/
  interpolate.ts`, `features/workflows/tasks/run-workflow.ts` (legacy per-node
  loop), `features/workflows/nodes/{send-email,open-url,act,extract,observe,
  agent}.ts`, `lib/resend.ts`, `features/workflows/nodes/node-registry.ts`
  (NodeType/StepNodeType), worker M23 TaskExecutor interface.
- **What changed:**
  - `worker/src/node-executor-adapter.ts` — `createNodeExecutorAdapter()`:
    worker-side execution adapter around the EXISTING node registry/executors.
    Per-task flow mirrors run-workflow.ts for a single node: load immutable
    version snapshot → locate node → load predecessor outputs → interpolate
    (existing `interpolate()`) → call existing executor → return opaque JSON
    output. Trigger nodes complete with no work (legacy parity). Version/
    outputs come from injectable loaders (fakes in M24 tests; durable Postgres
    loaders in M26). Browser session via optional `getStagehand` (wired M25).
    `executorOverrides` enables test sinks. Secrets stay server/worker-only
    (env vars read by executors; never in the envelope).
  - `worker/src/email-test-sink.ts` — safe in-memory test sink for
    side-effecting email (M24 step 7); records calls, returns the real
    `sendEmail` output shape `{ id }` for interpolation parity. Test-only;
    never registered in the product node registry.
  - `worker/src/node-executor-adapter.test.ts` — output-compatibility test
    (M24 step 9): for mocked deterministic nodes, the worker adapter produces
    BYTE-IDENTICAL output to the legacy execution path (extract, interpolated
    email body, trigger no-work, unknown-node permanent failure). Pure unit
    test — no Redis/DB/network.
  - `package.json` — wired the M24 test into `npm test`.
- **Concurrency/distributed correctness:** the adapter is stateless per task;
  all mutable state (version snapshots, node outputs) is owned by the loaders
  (durable Postgres in M26). Interpolation is pure. A node requiring a browser
  without one available fails cleanly (permanent) rather than hanging.
  Cancellation is checked before execution (cooperative).
- **No-go compliance:** no executor reimplementation (reuses nodeExecutors +
  interpolate); no secrets in envelopes; email side effect mocked in tests;
  Phase-1 untouched; adapter is opt-in Evo path only.
- **Validation:**
  - `npx tsx worker/src/node-executor-adapter.test.ts` → ✅ 4/4
  - `npm test` → ✅ 57/57 (28 Phase-1 + 4 contract + 8 M20 + 8 M22 + 5 M23 + 4 M24)
  - `npm run typecheck` → ✅ exit 0; `npm run lint` → ✅ exit 0
- **Human action:** none.
- **COMMIT:** `e581e67` — `phase2(m24): reuse existing node executors in worker`
- **NEXT:** M25 — Implement distributed browser session ownership and live-view parity.

## M25 — Implement distributed browser session ownership and live-view parity

- **BASE_SHA:** `8883037` (phase2 branch, clean tree after M24 docs)
- **What was inspected:** master prompt M25 spec (steps 1–9, no-go list,
  validation), `features/workflows/tasks/run-workflow.ts` (legacy Stagehand
  construction, live-view handshake, final screenshot, close semantics),
  `features/workflows/data.ts` (`isLiveViewConnected`), `features/workflows/
  lib/highlight-element.ts` (DOM-injection highlight helpers — reused by the
  executors themselves, unchanged), `worker/src/{worker,node-executor-adapter,
  envelope-codec}.ts`, M24 tests.
- **What changed:**
  - `worker/src/browser-session-manager.ts` — NEW worker-local
    `BrowserSessionManager` keyed by run/affinity key (default `run:<runId>`).
    One Stagehand session per affinity key, opened lazily on the first browser
    task and reused by every later same-key task (single-flight open; the
    affinity key is pinned to the owning worker while the session is live).
    Publishes the Browserbase session id via `onSessionOpened` as soon as it
    exists (M25 step 3; durable engine-neutral event wiring lands in M26/M27).
    Reuses the Phase-1 live-view handshake: polls `isLiveViewConnected`
    (injectable) up to `liveViewWaitMs` before the first browser action, once
    per session, then proceeds anyway so an unwatched run never hangs. Reuses
    the Phase-1 final-screenshot pattern (jpeg q70 → base64) before close,
    surfaced via `onFinalScreenshot`. `closeForRun` (success/failure/
    cancellation) and `closeAll` (graceful shutdown) are idempotent. DOM
    highlight helpers need no change — the executors already call them on the
    session's pages. WORKER-CRASH SEMANTICS documented explicitly: a crashed
    worker does NOT recover/reattach its Browserbase session (leaks until
    Browserbase idle timeout); crash recovery is M34.
  - `worker/src/node-executor-adapter.ts` — `getStagehand` option now takes
    the `TaskEnvelopeView` so the manager can key by the task's affinity key;
    a browser node with no session provider fails with a clear permanent
    error instead of hanging.
  - `worker/src/worker.ts` — new optional `onShutdown` hook, invoked during
    graceful shutdown after the in-flight drain; errors are logged, never
    fatal (Browserbase idle timeout is the backstop).
  - `worker/src/main.ts` — wires the manager with a production Stagehand
    factory mirroring run-workflow.ts (Browserbase env, Model Gateway model,
    `disablePino`); `BROWSERBASE_API_KEY` read from worker env only, never in
    an envelope; missing key fails browser tasks cleanly. `onShutdown` closes
    all live sessions.
  - `worker/src/browser-session-manager.test.ts` — 8 tests with a FAKE
    Stagehand (no Browserbase/network): one-session-per-key + reuse + id
    publish; live-view handshake polled once before first action; timeout
    proceeds; concurrent single-flight open; closeForRun screenshot→publish→
    close idempotent; closeAll on shutdown; default affinity key; adapter
    integration (browser node session owned/reused/closed via the manager).
  - `package.json` — wired the M25 test into `npm test`.
- **Concurrency/distributed correctness:** mutable state (the session map) is
  owned solely by the BrowserSessionManager inside one worker process; a
  single-flight promise prevents concurrent same-key opens. Cross-worker
  pinning is enforced by the scheduler's capacity-1 affinity policy (M12)
  routing same-key tasks to one worker; the manager enforces one-session-per-
  key locally. Close is idempotent (double closeForRun is a no-op). Shutdown
  races completion safely: drain first, then closeAll. Live-view polling
  errors are swallowed so they cannot fail a run.
- **Phase-1 preservation:** legacy run-workflow.ts untouched; live-view
  handshake, screenshot, and close semantics are mirrored, not forked;
  highlight helpers reused as-is; no Phase-1 default behavior changed.
- **Validation:**
  - `npx tsx worker/src/browser-session-manager.test.ts` → ✅ 8/8
  - `npm test` → ✅ 65/65 (28 Phase-1 + 4 contract + 8 M20 + 8 M22 + 5 M23 +
    4 M24 + 8 M25)
  - `npm run typecheck` → ✅ exit 0; `npm run lint` → ✅ exit 0
  - Live-key manual E2E: N/A — BROWSERBASE_API_KEY not configured in this
    environment (paid external test; not authorized for autonomous spend).
    Mock session-manager tests cover the semantics; live E2E deferred to a
    key-configured environment.
- **Human action:** none.
- **COMMIT:** `3828448` — `phase2(m25): preserve browser session semantics in workers`
- **NEXT:** M26 — Persist results and unlock dependencies through the distributed loop.

## M26 — Persist results and unlock dependencies through the distributed loop

- **BASE_SHA:** `7be1d42` (phase2 branch, clean tree after M25 docs)
- **What was inspected:** master prompt M26 spec (steps 1–8, no-go list,
  validation), `docs/phase2/ARCHITECTURE.md` §4.4/§6/§7, `lib/db/schema.ts`
  (workflow_runs/node_runs/task_attempts), `lib/db/migrations/
  0001_phase2_run_schema.sql`, `engine/core/{state_machine,transport,
  envelope,dag,execution_policy,scheduler}.{hpp,cpp}`, `engine/redis/
  redis_transport.{hpp,cpp}`, `worker/src/{worker,redis-streams,envelope-codec,
  synthetic-executor,main}.ts`, `engine/CMakeLists.txt`, `scripts/phase2/
  {lib,migrate-local}.sh`, `infra/phase2/docker-compose.yml`.
- **What changed:**
  - `engine/core/include/evo/run_store.hpp` + `core/src/run_store.cpp` — NEW
    `RunStore` interface: durable engine-neutral run state (run/node/attempt
    rows) with schema-matching status strings, `now_wall_ms()` (wall-clock
    UTC), and `InMemoryRunStore` for scheduler-core tests. Invariants: unique
    (run,node) node runs, unique (node,attempt) attempts, at-most-once
    terminal completion (`complete_node_run` returns false once terminal).
  - `engine/core/include/evo/distributed_run_loop.hpp` + `core/src/
    distributed_run_loop.cpp` — NEW `DistributedRunLoop` (M26 steps 1–7):
    persists run + node rows, dispatches validated TaskEnvelopes (attempt row
    durably recorded BEFORE publish), consumes ResultEnvelopes with identity
    validation → applicability (node must be RUNNING) → attempt-id dedupe
    (M22 ResultDedupe) → late-result rule, persists terminal node state
    FIRST and unlocks successor dependency counters ONLY when the store
    applied it (duplicate success can never double-unlock), persists failure
    details + cancels downstream (retry policy is M32), publishes normalized
    run events (JSON) on the event stream + in-process callback, finalizes
    the run row. `cancel()`/`stop()` are cross-thread (atomics + stop_token);
    blocking reads honor stop — no busy-spin. Resource capacity (M12) gates
    dispatch; slots freed only on applied results.
  - `engine/core/{include/evo,src}/transport.{hpp,cpp}` — `event_stream_key()`
    helper; `ensure_group` gains a `start_id` param ("$" default, "0" replay).
  - `engine/redis/{include/evo,src}/redis_transport.{hpp,cpp}` — `ensure_group`
    start_id passthrough (XGROUP CREATE ... <start_id> MKSTREAM).
  - `engine/pg/include/evo/pg_run_store.hpp` + `pg/src/pg_run_store.cpp` — NEW
    `PgRunStore` over libpq: PARAMETERIZED SQL only (PQexecParams; no string
    interpolation of values), INSERT ... ON CONFLICT DO NOTHING idempotency,
    conditional UPDATEs (`WHERE status NOT IN (terminal)`) for at-most-once
    completion, bounded reconnect/backoff mirroring RedisTransport. Never
    owns migrations (M19 Drizzle SQL is the DDL authority).
  - `engine/tests/distributed_run_loop_test.cpp` — 35 assertions over the
    in-memory transport + store with a fake worker thread: diamond E2E +
    audit, duplicate-result storm (no double unlock), identity validation,
    failure path (downstream canceled, never unlocked), malformed payload
    quarantine, cancel/stop terminal states, event ordering.
  - `engine/tests/pg_run_store_test.cpp` — Postgres integration (skips when
    local PG unreachable/unmigrated): idempotent creation, duplicate-attempt
    rejection, at-most-once completion, late-failure rejection, audit
    readers, SQL-injection probe (hostile node_id round-trips as data).
  - `engine/tests/distributed_e2e_test.cpp` — M26 step 8: C++ run loop + real
    Redis + 2 spawned TS worker processes (`npx tsx worker/src/main.ts`) +
    real Postgres, diamond DAG, faithful duplicate-result storm, Postgres
    audit assertions (worker attribution, one attempt per node), event-stream
    replay, zero pending tasks. Skips cleanly without the stack.
  - `engine/CMakeLists.txt` — `run_store.cpp` in core; new `evo_distributed`
    + `evo_pg_run_store` targets (libpq via find_path/find_library, SYSTEM
    headers); three new tests (E2E gated on hiredis+libpq, 180s timeout).
  - `worker/src/redis-streams.ts` — **wire-format fix:** `readGroup` now uses
    ioredis `xreadgroupBuffer` (binary-safe). The default string reply path
    UTF-8-transcoded payload bytes ≥ 0x80, corrupting protobuf envelopes that
    carry wall-clock Timestamps (multi-byte varints). M23's ASCII-only
    envelopes had masked this; the M26 E2E exposed it.
- **Concurrency/distributed correctness:** the loop is single-threaded (owns
  all scheduling state); transport + store are thread-safe shared objects;
  cancel/stop are the only cross-thread entry points. Duplicate delivery is
  safe at three layers: attempt-id dedupe, RUNNING-state applicability, and
  the store's at-most-once terminal UPDATE. A result can never be applied
  before its dispatch (applicability check) and never after terminal state
  (late-result rule). Crash after publish but before ack: task redelivers;
  duplicate result is deduped. Crash after durable write but before unlock:
  the loop re-derives readiness from persisted state on restart (M35).
- **Validation:**
  - `ctest --test-dir build` → ✅ 18/18 (incl. distributed_run_loop,
    pg_run_store, distributed_e2e)
  - `ctest --test-dir build-asan` → ✅ 18/18; `ctest --test-dir build-tsan`
    → ✅ 18/18
  - `npm run typecheck` → ✅; `npm run lint` → ✅; `npm test` → ✅ 65/65
  - Distributed E2E (real Redis + 2 TS workers + Postgres) → ✅ all audit
    assertions; duplicate storm contained.
- **Human action:** none.
- **COMMIT:** `2db7bb9` — `phase2(m26): close distributed scheduling result loop`
- **NEXT:** M27 — Introduce the Next.js execution-engine abstraction and feature flag.

## M27 — Introduce the Next.js execution-engine abstraction and feature flag

- **BASE_SHA:** `3ef28c6` (phase2 branch, clean tree after M26 docs)
- **What was inspected:** master prompt M27 spec (steps 1–8, no-go list,
  validation), `features/workflows/actions.ts` (runWorkflowAction /
  cancelWorkflowRunAction), `features/workflows/tasks/run-workflow.ts`
  (runWorkflowTask), `features/workflows/lib/workflow-versions.ts`
  (createWorkflowVersion + VersioningDb pattern), `features/workflows/lib/
  validate-graph.ts`, `lib/db/{index,schema}.ts`, `engine/app/grpc_service.cpp`
  (ControlService SubmitRun/CancelRun/GetRun/Health + synthetic local
  executor), `engine/proto/evo/execution.proto`, `next.config.ts`, bundled
  Next.js docs (`serverExternalPackages.md`), `@grpc/grpc-js` 1.14.4 +
  `@grpc/proto-loader` 0.8.1 (installed, Apache-2.0).
- **What changed:**
  - `features/workflows/lib/execution-engine.ts` — NEW engine-neutral
    interface (M27 step 1): `ExecutionEngineAdapter` (startRun/cancelRun/
    getRunStatus), `EngineRunHandle`, `StartRunArgs`, `EngineRunStatus`.
    Server-only feature flag `EXECUTION_ENGINE=legacy|evo` via
    `getExecutionEngine()` — FAIL-CLOSED: only exact "evo" (trimmed,
    case-insensitive) selects Evo; unset/empty/typo stays legacy (step 4).
    Adapter cached per process; test reset helper.
  - `features/workflows/lib/legacy-engine-adapter.ts` — NEW legacy adapter
    (M27 step 2): wraps the EXISTING Trigger.dev trigger/cancel calls with
    identical behavior; run id = Trigger.dev run id so live-view/cancel/
    replay/results keep working unchanged. Trigger/cancel injectable for the
    regression test; defaults are the real SDK calls.
  - `features/workflows/lib/evo-engine-adapter.ts` — NEW Evo adapter (M27
    step 3): `graphToCanonicalDagJson()` converts the React Flow graph to the
    canonical DAG JSON the C++ `Dag::from_json` parses (sorted nodes/edges,
    trigger/action kinds, from/to edges, NO React Flow UI state, no secrets);
    submits a client-generated engine-neutral run id + dagJson through an
    injectable `EvoSchedulerClient`; maps the C++ RunStatus enum to the
    engine-neutral status. Real gRPC client loaded lazily (dynamic import) so
    the legacy path + unit tests never pull in the gRPC stack.
  - `features/workflows/lib/evo-scheduler-client.ts` — NEW gRPC client over
    `@grpc/grpc-js` + `@grpc/proto-loader` loading the shared
    `engine/proto/evo/execution.proto`; promise wrappers for SubmitRun/
    CancelRun/GetRun/Health; insecure channel to the loopback scheduler
    (matches the M17 no unauthenticated-non-local rule).
  - `features/workflows/lib/run-records.ts` — NEW engine-neutral run records
    (M27 step 6): `createWorkflowRunRecord()` inserts the workflow_runs row
    with the `engine` discriminator BEFORE submission (idempotent on runId
    via PK conflict → read existing); `getRunEngine()` resolves the owning
    engine for cancel routing (undefined → legacy for pre-table runs).
  - `features/workflows/actions.ts` — runWorkflowAction now routes through
    the adapter AFTER Clerk auth + Pro-plan gate (step 5). Evo: snapshot is
    REQUIRED (fail-closed), run row created before submission, client-
    generated run id submitted. Legacy: behavior unchanged (fail-open
    snapshot + best-effort run row). cancelWorkflowRunAction resolves the
    owning engine via getRunEngine and routes the cancel. Trigger.dev deps
    and the run task are NOT removed (step 8).
  - `next.config.ts` — `serverExternalPackages: ["@grpc/grpc-js"]` (Node-
    specific dynamic require; per bundled Next.js docs).
  - `features/workflows/lib/execution-engine.test.ts` — 7 tests: flag
    fail-closed matrix; legacy adapter regression (exact trigger call shape +
    handle mapping + cancel forwarding); canonical DAG conversion (sorted,
    kinds, no UI state); Evo adapter submit/cancel/query via fake client;
    rejection surfacing; RunStatus mapping; run records against local
    Postgres (discriminator + idempotency + resolver; skips without stack).
  - `features/workflows/lib/evo-scheduler-client.test.ts` — 7 integration
    tests driving the REAL gRPC client against the REAL C++ scheduler server
    binary on a private loopback port: health, synthetic diamond submission,
    idempotent re-submit, poll to RUN_SUCCEEDED, outcome, cancel RPC, cyclic
    DAG rejected at the trust boundary. Skips when the server binary is
    missing.
  - `package.json` — added `@grpc/grpc-js` + `@grpc/proto-loader`; wired both
    M27 tests into `npm test`.
- **Concurrency/distributed correctness:** the flag + adapter cache are
  process-local and read once; the run row's primary key is the idempotency
  guard for duplicate submission; the C++ SubmitRun is itself idempotent on
  run_id. Cancel races completion safely (C++ cancel on a terminal run is a
  no-op returning ok). The Evo run id is engine-neutral and separate from any
  provider id; no secrets cross the gRPC boundary (org/version/dag only).
- **Phase-1 preservation:** legacy is the default and unchanged; planner still
  never auto-runs; Run remains the only explicit trigger; Clerk + Pro gate run
  before engine selection; Trigger.dev deps + task retained; no test removed.
- **Validation:**
  - `npm test` → ✅ 79/79 across 11 suites (28 Phase-1 + 4 contract + 8 M20 +
    8 M22 + 5 M23 + 4 M24 + 8 M25 + 7 M27 engine + 7 M27 evo submission)
  - `npm run typecheck` → ✅; `npm run lint` → ✅
  - `npm run build` (production) → ✅ (validates gRPC bundling config)
  - Evo synthetic submission integration (real gRPC ↔ real C++ server) → ✅
  - Legacy adapter regression → ✅ (exact pre-M27 trigger call shape)
- **Human action:** none.
- **COMMIT:** `799a4f6` — `phase2(m27): add dual execution engine abstraction`
- **NEXT:** M28 — Build engine-neutral Evo run events and realtime frontend transport.

## M28 — Build engine-neutral Evo run events and realtime frontend transport

- **Status:** ✅ DONE
- **What was inspected:** master prompt M28 spec (steps 1–17, no-go list,
  required validation); `features/workflows/components/workflow-runs-provider.tsx`
  (shared-provider architecture to preserve); `app/(dashboard)/workflows/[id]/page.tsx`;
  `app/api/runs/[runId]/screenshot/route.ts` (auth pattern); `lib/auth.ts`;
  `lib/db/schema.ts` (workflow_runs/node_runs); C++ `RunEvent::to_json_string`
  + event-stream publish path (M26); `worker/src/redis-streams.ts` (wire
  protocol); bundled Next.js streaming docs (`node_modules/next/dist/docs/01-app/02-guides/streaming.md`).
- **What changed:**
  - `features/workflows/lib/run-view-model.ts` — NEW normalized engine-neutral
    run view model (M28 steps 1–2): `NormalizedRunViewModel` (status, steps,
    timing, browser session id, final URL, output, stats), `triggerRunToViewModel`
    (legacy Trigger adapter: output steps preferred, metadata fallback, live
    session id), `reduceEvoEvents` (folds ordered C++ RunEvents idempotently
    per (kind, node_id); node type from dispatch `detail`, output from success
    `detail`; cross-run isolation; unknown kinds ignored; cancel never
    overwrites a completed node).
  - `lib/evo-redis.ts` — NEW server-side Redis client (lazy, cached; local
    Phase-2 Redis; credentials server-only — never sent to the browser).
  - `features/workflows/lib/evo-run-events.ts` — NEW server-side event reader
    (M28 steps 3+5): `readEvoRunEvents` (XRANGE replay filtered to one run),
    `tailEvoRunEvents` (blocking XREAD tail with abort/timeout/onIdle hooks;
    never throws on Redis hiccups — client reconnects and replays),
    `durableRunSnapshot` (reconnect fallback reconstructing the view from
    workflow_runs + node_runs — durable state is persisted BEFORE events are
    published, so the snapshot is always at least as fresh), `loadEvoRunView`.
  - `features/workflows/lib/evo-run-events-route.ts` — NEW authorized-route
    core (M28 step 4): `buildEvoRunEventsResponse` checks run row existence +
    org ownership + engine=evo (unknown/tenant-mismatch/legacy all 404 so
    cross-tenant existence is not leaked; legacy stays on the Trigger
    provider), then streams SSE: replay (or resume after Last-Event-ID) →
    durable snapshot fallback → live tail until terminal/disconnect/10-min cap,
    with 15s keep-alive pings. Split from the route so authorization +
    reconnect behavior are unit-testable without Clerk.
  - `app/api/runs/[runId]/events/route.ts` — NEW thin route handler: Clerk
    session + `resolveActiveOrgId()` before delegating to the builder;
    `force-dynamic`; passes request signal for client-disconnect abort.
  - `features/workflows/components/evo-run-events-provider.tsx` — NEW shared
    client provider (M28 step 6): ONE EventSource per run shared by all
    components (no per-component subscriptions); keyed remount per runId;
    consumes `run-event` frames through `reduceEvoEvents` and `snapshot`
    frames as wholesale fallback; EventSource auto-reconnect sends
    Last-Event-ID and the idempotent reducer makes duplicate delivery safe;
    closes for good after a terminal event. Exports `useEvoRunView` /
    `useEvoRunEventsError`. The Trigger provider is NOT deleted (no-go).
  - `features/workflows/lib/run-view-model.test.ts` — 20 tests: normalized
    model + legacy mapper regression (Phase-1 provider regression), event
    ordering, duplicate idempotency, cross-run isolation, unknown kinds,
    failure/cancel/terminal paths, cancel-vs-completion race, authorized-route
    404 matrix (unknown/tenant/legacy), ordered replay + close-at-terminal,
    reconnect resume after Last-Event-ID, durable snapshot fallback, live tail
    delivering late events. Redis+Postgres sections skip cleanly when the
    local stack is down.
  - `package.json` — wired the M28 test into `npm test`.
- **Concurrency/distributed correctness:** the event stream is append-only and
  owned by the C++ run loop (single writer per run); the SSE route is a pure
  reader with per-request cursors — no shared mutable state. Duplicate event
  delivery is safe (reducer idempotent per (kind, node_id)). Reconnect never
  loses progress: replay from stream or durable snapshot (persisted before
  publish). Cancellation racing completion: a `node_canceled` after
  `node_succeeded` cannot overwrite the terminal step status. Client
  disconnect aborts the tail via the request signal; a 10-minute cap prevents
  pinned connections. Late results cannot overwrite newer state — the reducer
  only folds forward and the durable store enforces at-most-once terminal
  completion (M26).
- **Timestamps:** event `wall_ms` and Postgres timestamps are wall-clock UTC;
  no steady_clock value crosses the process boundary.
- **Phase-1 preservation:** Trigger realtime provider untouched and still the
  default path; legacy runs get 404 on the Evo events route; Clerk auth stays
  server-side; no Phase-1 default behavior changed; no test removed.
- **Validation:**
  - `npm test` → ✅ exit 0 across 12 suites (incl. M28: 20 passed —
    normalized model, authorized event route, reconnect simulation, Phase-1
    provider regression)
  - `npm run typecheck` → ✅; `npm run lint` → ✅
  - `npm run build` (production) → ✅ (`/api/runs/[runId]/events` present,
    dynamic)
- **Human action:** none.
- **COMMIT:** `c3723bc` — `phase2(m28): add engine-neutral realtime run model`
- **NEXT:** M29 — Achieve UI parity for Evo runs.

---

## M29 — Achieve UI parity for Evo runs

- **START_SHA:** `abe3eb6` (M28 recorded; branch `phase2`, clean claim).
- **Objective:** make Evo execution preserve the Phase-1 user experience —
  pending/running/done/failed node states, live Browserbase view, readable
  completion results, org-checked screenshot, correct replay session, editable
  canvas after terminal, rerun with fresh run id + session — before any
  reliability features land.
- **DB topology (the core M29 problem):** the app's `getDb()` talks to Neon
  (Phase-1 tables only); the C++ engine + workers talk to the LOCAL Phase-2
  Postgres (:5433) + Redis (:6390). M29 makes every Evo UI path read the
  Phase-2 store:
  - `lib/db/phase2.ts` — `getPhase2Db()` (lazy pg.Pool + drizzle snake_case),
    `EVO_PHASE2_PG_*` env, defaults 127.0.0.1:5433/evo/evo_phase2.
  - Evo version snapshot + run record + run listing + SSE route all pointed at
    the Phase-2 store; legacy path untouched (still Neon, fail-open).
- **gRPC → distributed bridge:** `engine/app/grpc_service.cpp` now runs product
  DAGs through `DistributedRunLoop` (Redis transport + PgRunStore) instead of
  the local synthetic scheduler, so app-submitted Evo runs execute on real
  workers. Synthetic `bench:*` runs keep the M17 local path. Links
  `evo_distributed`/`evo_redis_transport`/`evo_pg_run_store` behind
  `EVO_HAVE_DISTRIBUTED`.
- **Worker real executors (M29b):** `worker/src/main.ts` composite executor —
  `start`/`bench:*` → synthetic; everything else → the M24 node-executor
  adapter with durable loaders (`worker/src/durable-loaders.ts`: version +
  predecessor outputs from Phase-2 PG) and worker-local browser sessions.
  Per-browser-task screenshot capture + `run_artifacts` upsert.
- **Durable Browserbase session id (M29c1):** new additive column
  `workflow_runs.browserbase_session_id` (migration
  `0002_phase2_run_browserbase_session.sql`, applied to LOCAL PG only — Neon
  untouched, needs explicit human approval). The worker stamps it the moment a
  session opens (`saveRunBrowserbaseSession`, write-once), so replay /
  live-view / screenshot resolve the session from durable state even after the
  event stream is gone. `listEvoRunsForWorkflow` + `durableRunSnapshot` carry
  it into the view model.
- **C++ queued→running fix (found during M29):** the app pre-creates the run
  row as `queued`, but `PgRunStore::create_run` used `ON CONFLICT DO NOTHING`,
  so the run never became `running` and `started_at` stayed null (breaking live
  status + duration). Now `ON CONFLICT DO UPDATE ... WHERE status='queued'`
  promotes it; terminal rows are never regressed. Mirrored in
  `InMemoryRunStore`. Regression-tested in `pg_run_store_test` (ran live, 6 new
  checks green).
- **Unified engine-neutral provider (M29c2):** extracted the console mapping
  into pure `features/workflows/lib/run-console.ts`
  (`toConsoleRunFromLegacy` = verbatim Phase-1 logic, `toConsoleRunFromEvo`,
  `consoleRunStatusLabel`, `mergeConsoleRuns`). `workflow-runs-provider.tsx`
  now merges legacy realtime runs + a single durable Evo poll (2s) into one
  newest-first de-duplicated `ConsoleRun[]`; every exported hook
  (`useConsoleRuns`/`useLatestRun`/`useLatestRunSteps`/`useLiveRun`/
  `useLiveBrowserbaseSessionId`) keeps its name/signature, so all consumers
  work for both engines unchanged.
- **Consumer parity (M29c3):** logs/inspector/results now use the derived
  booleans (`isCompleted`/`isFailed`/`isCanceled`) + shared status label;
  `RunStep.status` gained Evo-only `"canceled"` (renders inactive, never
  spinning); `step-node` paints a canceled state; `NodeIcon` falls back
  gracefully for unknown node types.
- **Org-checked routes for Evo (M29d):** screenshot + live-view GET/connected
  routes are engine-aware — legacy resolves via Trigger.dev (unchanged), Evo
  resolves ownership + session id from the Phase-2 run row
  (`features/workflows/lib/evo-run-data.ts`, all reads org-scoped + fail-open).
  The connected handshake writes to the Phase-2 store for Evo, and the worker
  now waits for it (`isEvoLiveViewConnected`, 60s/1s, fail-open) — same
  "hold the first browser step until the view connects" behavior as Phase-1.
- **Rerun:** `runWorkflowAction` mints a fresh `evo_<uuid>` run id + fresh
  version snapshot per submission; the worker opens a new session per run
  affinity key, so rerun gets a new engine-neutral run id AND a new browser
  session (parity asserted in the lifecycle suite).
- **Parity regression suite (M29e):** `run-console-parity.test.ts` (9
  scenarios) drives the exact console-mapping code with a legacy run and an Evo
  run in each lifecycle state and asserts identical derived semantics, status
  label, step-status vocabulary, session-id resolution, queued→running
  promotion, and merged-history ordering/dedup. Wired into `npm test`.
- **Timestamps:** all durable + event timestamps wall-clock UTC; steady_clock
  never crosses the process boundary.
- **Phase-1 preservation:** legacy Trigger.dev engine remains the default
  (flag fail-closed); Run/Stop/results/replay/rerun behavior unchanged for
  legacy runs (mapping preserved verbatim); Clerk auth + Pro gate stay
  server-side and fail-closed; browser credentials stay server/worker-only; no
  test removed; canvas stays editable after terminal (no change needed — the
  provider never blocks editing).
- **No-go compliance:** no performance comparison or numbers (parity only);
  Evo NOT made the default; no future component marked implemented; no secret
  or credential value committed; Phase-1 default behavior untouched.
- **Validation:**
  - `npm test` → ✅ exit 0 across 13 suites (incl. new M29 parity: 9/9)
  - `npm run typecheck` → ✅; `npm run lint` → ✅ (0 errors, 0 warnings)
  - CMake build → ✅ clean; `ctest` → ✅ 18/18 (incl. pg_run_store promotion
    regression against live local PG)
  - `npm run build` (production) → ✅ (all routes present incl.
    `/api/runs/[runId]/events`, `/api/runs/[runId]/screenshot`, live-view)
  - Manual live E2E with configured keys: NOT run this session (requires live
    Browserbase/keys + running scheduler/worker); durable-path parity is
    covered by the regression suites. Marked as the one open item for the
    final evidence campaign (M39/M40).
- **Known limitations:** Evo console liveness comes from a 2s durable poll
  (not push) — acceptable for parity; push fan-out is a later optimization.
  Live E2E with real keys deferred to the final campaign.
- **Human action:** none (local PG migration only; Neon migration still
  requires explicit approval).
- **COMMIT:** `a24dd9e` — `phase2(m29): reach evo engine ui parity`
- **NEXT:** M30 — Implement end-to-end cancellation across app, scheduler, queue, worker, and browser.

---

## M30 — Implement end-to-end cancellation across app, scheduler, queue, worker, and browser

**Status:** ✅ DONE — Stop is now meaningful end-to-end (app → gRPC → scheduler → control stream → worker → browser), not just a UI flag.

- **Durable cancellation-request timestamp (M30a):** new additive column
  `workflow_runs.cancel_requested_at` (migration
  `0003_phase2_run_cancel_requested_at`, applied to local PG only). Written
  once, idempotently, by `RunStore::mark_cancel_requested` (first request
  wins; `WHERE cancel_requested_at IS NULL`). Implemented in both
  `InMemoryRunStore` and `PgRunStore`; `get_run` round-trips the timestamp +
  reason. This proves the request was durably recorded even if a process dies
  mid-cancel, and lets tooling distinguish "cancel requested" from "cancel
  finalized".
- **Scheduler-side cancel (M30b):** `DistributedRunLoop::cancel()` is now
  idempotent + terminal-no-op + durable + propagated. The FIRST request wins
  (reason + wall-clock timestamp preserved); it stamps the run row via the
  store, publishes exactly one `CANCEL_RUN` `ControlEnvelope` on the control
  stream, and sets the cancel flag. `dispatch_ready()` re-checks the flag so
  NO task dispatches after a cancel. If cancel races `run()` startup (run row
  not yet created), `run()` retries the durable stamp right after creating the
  row. `finalized_` is set at every terminal exit so Stop-after-terminal is a
  no-op.
- **New proto message (additive):** `ControlEnvelope { kind, run_id, reason,
  requested_at }` with `Kind.CANCEL_RUN`. Field numbers never reused; the
  control stream (`<prefix>:control`) already existed in both C++/TS key
  helpers but was unused — now it carries scheduler→worker cancellation.
- **Worker-side cancel (M30c):** the TS `Worker` now runs a dedicated
  control-stream fan-out loop on its OWN Redis connection (a shared connection
  would let one blocking XREADGROUP delay the other). Each worker reads with
  its OWN consumer group (`control-<workerId>`, start id "0") so EVERY worker
  sees EVERY control message (fan-out, not competing-consumers). On
  `CANCEL_RUN`: abort in-flight attempts for the run via a per-run
  `AbortController` (created lazily at task start so an in-flight task holds a
  signal a later cancel can abort), short-circuit queued tasks for the run
  (publish a `CANCELED` result, never execute), and close the run's browser
  session via `onCancelRun` → `BrowserSessionManager.closeAllForRun` (matches
  by runId across affinity keys, skips the final screenshot for prompt close,
  and closes sessions racing an in-flight open so none leak). Duplicate
  deliveries are no-ops (the canceled-run set is the dedupe key).
- **gRPC CancelRun (M30d):** idempotent + terminal-no-op. Stop-after-terminal
  reports the run's ACTUAL terminal outcome (never re-cancels); repeated Stop
  on a running run is a no-op after the first (the loop's cancel is
  first-request-wins); unknown run → NOT_FOUND. The TS client now passes
  `requested_at` (wall-clock UTC ms → Timestamp) for correlation.
- **Late-result rejection (M30 step 6):** after a run is terminal canceled, a
  late/forged success for an in-flight node is rejected — the run loop checks
  cancellation before consuming results, and the node is already terminal, so
  the late result never overwrites durable state (regression-tested).
- **Tests added (M30e):**
  - C++ `distributed_run_loop_test`: cancel-before-dispatch (no attempt rows,
    all nodes canceled, timestamp stamped despite startup race, exactly one
    well-formed CANCEL_RUN control message), repeated-cancel idempotency
    (first reason wins, one control message), Stop-after-terminal no-op, and
    late-success-after-terminal-canceled rejection. +14 checks.
  - C++ `pg_run_store_test`: mark_cancel_requested first-write-wins +
    round-trip + unknown-run false. +6 checks.
  - C++ `grpc_integration_test`: repeated CancelRun idempotent,
    Stop-after-terminal reports real outcome, unknown run → NOT_FOUND.
  - TS `worker.test`: cancel-during-synthetic (in-flight abort → CANCELED
    result, ERROR_CANCELED, not retryable), queued-task short-circuit (3 tasks
    never executed, all CANCELED), duplicate CANCEL_RUN idempotent, and
    cancel-during-mocked-browser (abort + session closed promptly). +4
    scenarios (9/9).
  - TS `browser-session-manager.test`: closeAllForRun closes all of a run's
    sessions regardless of affinity key + idempotent, and cancel racing an
    in-flight open closes the session (no leak). +2 scenarios (10/10).
- **Diagnostic latency (NOT benchmark-grade; single samples, labeled):**
  scheduler cancel→terminal ≈ 19µs (in-process); worker control→abort ≈
  360–400ms (dominated by the blocking-read slice + Redis round-trip);
  worker control→browser-stop ≈ 367ms. These are diagnostic only — no
  benchmark methodology (workload/hardware/sample-count) was satisfied, so no
  resume number is claimed.
- **Timestamps:** all durable + control-message timestamps wall-clock UTC;
  steady_clock used only for the diagnostic latency delta (never persisted).
- **Phase-1 preservation:** legacy Trigger.dev Stop path untouched (the app
  routes cancel by engine; legacy still forwards to Trigger.dev). No Phase-1
  default behavior changed; no test removed; browser credentials stay
  server/worker-only.
- **No-go compliance:** no promise of instant interruption of an arbitrary
  in-flight third-party SDK call (abort is cooperative at the signal boundary;
  the durable store + late-result rule are the backstop); no new task dispatch
  after terminal cancellation (asserted); no performance numbers invented; no
  future component marked implemented; no secret/credential committed.
- **Validation:**
  - CMake build → ✅ clean; `ctest` → ✅ 18/18 (incl. new M30 loop + PG +
    gRPC checks against live local PG/Redis)
  - `npm test` → ✅ exit 0 across 12 suites (worker 9/9, browser-mgr 10/10,
    parity 9/9, all prior suites green)
  - `npm run typecheck` → ✅; `npm run lint` → ✅ (0 errors, 0 warnings)
  - `npm run build` (production) → ✅ (all routes present)
  - Manual live-browser Stop: NOT run this session (requires live
    Browserbase/keys + running scheduler/worker fleet); the durable +
    mocked-browser paths are covered by the suites. Marked as an open item for
    the final evidence campaign (M39/M40).
- **Known limitations:** worker cancel latency is bounded by the blocking-read
  slice (default 500ms) — a worker mid-slice sees the control message on the
  next slice. A slow (not dead) worker that misses the control message is still
  bounded by the late-result rule once the run is terminal. Live-browser Stop
  deferred to the final campaign.
- **Human action:** none (local PG migration only; Neon migration still
  requires explicit approval).
- **COMMIT:** `200386a` — `phase2(m30): implement distributed cancellation`
- **NEXT:** M31 — Implement worker registry, leases, and heartbeats.

---

## M31 — Implement worker registry, leases, and heartbeats

**Status:** ✅ DONE — lost workers/tasks are now detected via durable leases + heartbeats, without equating slowness with death.

- **BASE_SHA:** `5ba1b8a` (phase2 branch; M31 was claimed in-progress with
  uncommitted work, which this session reviewed, fixed, tested, and completed).
- **What was inspected:** master prompt M31 spec (steps 1–8, no-go list,
  validation), the uncommitted M31 working tree (C++ run loop / run store /
  state machine / PG store, TS worker + lease-store, migration 0004),
  `engine/core/src/transport.cpp` (redelivery semantics),
  `engine/tests/{distributed_run_loop_test,pg_run_store_test}.cpp`,
  `worker/src/worker.test.ts`, `docs/phase2/DECISIONS.md`.
- **What changed:**
  - **Durable schema (M31 step 2):** migration
    `0004_phase2_worker_registry_leases.sql` — new `workers` registry table
    (worker_id PK, env_prefix, status, registered_at, last_heartbeat_at) + 4
    additive `task_attempts` lease columns (`lease_acquired_at`,
    `lease_renewed_at`, `lease_expires_at`, `lease_expired_at`) +
    `ix_task_attempts_lease_expires` index. Applied to LOCAL PG only (Neon
    untouched, needs explicit human approval). `lib/db/schema.ts` mirrors it.
  - **Heartbeat vs lease separation (M31 step 1):** a heartbeat proves the
    PROCESS is alive (registry row, `heartbeatIntervalMs`); a lease proves a
    specific ATTEMPT is being worked (`leaseDurationMs` /
    `leaseRenewIntervalMs`). Two independent clocks, documented in the schema,
    the worker, and DECISIONS.md.
  - **C++ RunStore lease API:** `worker_heartbeat`, `init_attempt_lease`,
    `acquire_attempt_lease` (steal guard), `renew_attempt_lease` (holder-only),
    `scan_expired_attempt_leases`, `mark_attempt_lease_expired` (at-most-once
    reap), `get_attempt_lease`, `get_worker` — implemented in both
    `InMemoryRunStore` and `PgRunStore` (parameterized SQL).
  - **Two-phase lease (M31 steps 3–4):** the scheduler stamps a queue-wait
    deadline at dispatch (`init_attempt_lease`, `lease_initial_duration` —
    deliberately generous so a live-but-slow-to-claim worker is not reaped);
    the worker takes over on claim (`acquire_attempt_lease`, resets to
    `lease_duration`) and renews while working. **Fixed:** `lease_initial_duration`
    was dead code — `dispatch_ready()` now actually uses it.
  - **Recovery, not failure (M31 step 5):** `SchedulerState::abandon_node()`
    moves RUNNING/DISPATCHED → READY (no successor touched, no dependency-counter
    change); the run loop's `scan_expired_leases()` reaps expired attempts and
    re-dispatches the node as a NEW attempt, releasing the abandoned resource
    slot. Emits a `node_lease_expired` event.
  - **At-most-once reap (M31 no-go):** `mark_attempt_lease_expired` applies only
    when the attempt is still `running` and held by the recorded worker; a racing
    completion already left `running`, so it can never be double-completed.
  - **PG NULL-worker fix (found this session):** `record_attempt` stores
    `NULLIF(worker_id,'')`, so a never-claimed attempt has `worker_id IS NULL`;
    `NULL = ''` is never true in SQL, which made queue-wait leases unreapable.
    The reap now matches an empty `worker_id` argument against NULL explicitly.
  - **Worker-side (TS):** `TaskLeaseStore` interface + `PgTaskLeaseStore`
    (parameterized SQL mirroring the C++ queries, incl. the
    `to_timestamp(ms/1000.0) AT TIME ZONE 'UTC'` conversion). The worker
    registers/heartbeats on start (own cadence), acquires the lease before
    executing, renews on an interval while running, stops renewing once the
    attempt finishes, and skips + acks a task whose lease it cannot acquire.
  - **Tests added (M31 steps 7–8):**
    - C++ `distributed_run_loop_test`: slow-but-renewing worker is NOT reaped
      (run succeeds on one attempt each, no lease_expired event), and a killed
      worker's lease expires → attempt reaped to `lease_expired` → node
      re-dispatched as attempt 2 → healthy worker completes it (recovery, not
      permanent failure). +16 checks.
    - C++ `pg_run_store_test`: full durable lease lifecycle (heartbeat
      register/refresh, init, acquire, steal-guard, renew holder-only, scan
      before/after expiry, at-most-once reap, completed-attempt-not-reapable)
      + queue-wait (never-claimed, NULL worker) reap. +43 checks.
    - TS `worker.test`: worker registers + heartbeats on its own cadence;
      acquires the lease, renews while running, stops after finish; unacquirable
      lease → task skipped + acked, never executed. +3 scenarios (12/12).
- **Concurrency/distributed correctness:** the run loop is single-threaded (owns
  all scheduling state); the scan runs inside it, paced by steady_clock but
  comparing durable wall-clock `lease_expires_at`. The conditional UPDATE is the
  invariant guard against double-complete. A slow (not dead) worker that keeps
  renewing is never reaped; a dead worker's renewals stop and the lease expires.
  Late results from a reaped attempt are bounded by the existing late-result rule
  (M22/M26). Browser-affinity slots held by a dead worker are released on reap.
- **Timestamps:** all durable lease/heartbeat timestamps wall-clock UTC ms;
  steady_clock used only to pace the scan cadence (never persisted).
- **Phase-1 preservation:** legacy Trigger.dev engine untouched; no Phase-1
  default behavior changed; no test removed; browser credentials stay
  server/worker-only. Migration is additive and local-only.
- **No-go compliance:** heartbeat documented as proving process liveness, NOT
  task progress; lease expiry cannot double-complete a logical node (asserted);
  no performance numbers invented; no future component marked implemented; no
  secret/credential committed.
- **Validation:**
  - CMake build → ✅ clean; `ctest` (Release) → ✅ 18/18
  - ASan+UBSan → ✅ 18/18; TSan → ✅ 18/18 (no data races)
  - `npm test` → ✅ exit 0 across 13 suites (worker 12/12 incl. 3 new M31,
    all prior suites green)
  - `npm run typecheck` → ✅; `npm run lint` → ✅ (0 errors, 0 warnings)
  - Postgres/Redis audit assertions → ✅ (pg_run_store 43 M31 checks vs live PG;
    distributed_run_loop lease evidence vs in-memory store)
- **Known limitations:** `grpc_integration` uses `pick_free_port()` (bind-0,
  release, reuse) — a pre-existing M17 TOCTOU pattern that can flake under
  parallel load; it passes consistently on re-run and is not M31-related.
  Recovery-latency benchmarking is deferred to M39. Live-browser lease behavior
  (a real Browserbase session held across a lease renewal) is covered by the
  mocked-session suites; live E2E deferred to the final campaign.
- **Human action:** none (local PG migration only; Neon migration still
  requires explicit approval).
- **COMMIT:** `4543443` — `phase2(m31): add worker leases and heartbeats`
- **NEXT:** M32 — Implement node-level retry policy, exponential backoff, jitter, and dead-lettering.

## M32 — Implement node-level retry policy, exponential backoff, jitter, and dead-lettering

**Status:** ✅ DONE — Evo runs now retry transient failures per resource class with bounded deterministic backoff, park nodes in a durable RETRY_WAIT state (no blocked threads), and dead-letter exhausted failures as terminal run failures.

- **BASE_SHA:** `2abd695` (phase2 branch; M32 was claimed in-progress with
  uncommitted work, which this session reviewed, completed, tested, and
  documented — including adding the missing "retry after worker lease expiry"
  scenario required by M32 step 9).
- **What was inspected:** master prompt M32 spec (steps 1–19, no-go list,
  validation, exit criteria), the uncommitted M32 working tree (C++ retry
  policy / run loop / state machine / run store / PG store, proto contract,
  migration 0005, TS UI mappings), `engine/core/src/retry_policy.cpp`,
  `engine/tests/{retry_policy_test,distributed_run_loop_test,pg_run_store_test}.cpp`,
  `features/workflows/lib/{run-view-model,evo-run-events}.ts`,
  `engine/app/grpc_service.cpp`, `docs/phase2/DECISIONS.md`.
- **What changed:**
  - **Error taxonomy (M32 step 1):** `ErrorCategory` enum — transient,
    resource_lost, permanent, validation, authorization, canceled, unknown —
    classified from the proto `ErrorClass` via `classify_error()`. Added
    `ERROR_VALIDATION = 5` and `ERROR_AUTHORIZATION = 6` to the proto
    `ErrorClass` enum (additive-only field evolution) and regenerated the
    pb sources with `engine/proto/generate.sh`.
  - **Default retry policy by resource class (M32 step 2):**
    `RetryPolicySet::for_class()` — internal = 3 attempts / 100ms base,
    browser = 2 attempts / 500ms base (sessions are expensive),
    external_io = 1 attempt (NO retry by default: side effects require an
    idempotency strategy, which is M33). Legacy Trigger.dev untouched —
    Evo-only path.
  - **Policy floor (M32 no-go):** permanent / validation / authorization /
    canceled are NEVER retried even if the worker hinted `retryable=true`;
    unknown retries ONLY on an explicit positive hint; a `retryable=false`
    hint overrides a default-retryable class. The scheduler's taxonomy
    outranks the worker hint for never-retry classes.
  - **Attempt limits + budget (M32 step 3):** `failed_attempt >= max_attempts`
    ⇒ dead-letter. The attempt number rides in the result envelope, so a
    lease-expiry re-dispatch (M31 recovery) consumes NO retry budget — the two
    mechanisms compose (asserted in run-loop test 18).
  - **Exponential backoff + bounded deterministic jitter (M32 step 4):**
    `base * multiplier^(attempt-1)` capped at `max_backoff`, scaled by a
    factor in `[1-j, 1+j]` from a per-(node, attempt, run-seed) FNV-1a-seeded
    xorshift64*. Same inputs ⇒ same backoff ⇒ reproducible tests.
  - **Durable retry evidence (M32 step 5):** migration
    `0005_phase2_node_retry.sql` — 2 additive `node_runs` columns
    (`retry_wait_until` timestamp, `retry_reason` text). Applied to LOCAL PG
    only (Neon untouched, needs explicit human approval). `lib/db/schema.ts`
    mirrors it. `set_node_retry_wait()` implemented in both
    `InMemoryRunStore` and `PgRunStore`, guarded against terminal nodes.
  - **Non-blocking backoff (M32 step 6):** the node parks in `RETRY_WAIT`
    with a persisted due-time; `process_retry_waits()` re-readies due nodes
    each loop iteration (state + due-time, NOT a blocked thread). The run
    loop stays single-threaded; no new synchronization.
  - **Dead-lettering (M32 step 7):** `dead_letter_node()` moves the node to
    `DEAD_LETTERED` + cancels downstream; `finalize_run()` now counts
    `DeadLettered` as failure (**fix found this session** — without it a
    dead-lettered run finalized as CANCELED, not FAILED).
  - **Diagnostics, not UI noise (M32 step 8):** `node_retry_scheduled` keeps
    the step reading as RUNNING (a transient failure is not terminal);
    `node_dead_lettered` reads as FAILED with the reason; durable
    `retry_wait` maps to running in the reconnect snapshot. Attempt history
    stays in `task_attempts` + events. Three mapping surfaces updated
    consistently: `reduceEvoEvents`, `mapNodeStatus`, gRPC `GetRun`
    (`NODE_STATE_DEAD_LETTER`).
  - **State-machine transitions:** `retry_wait_node()` (RUNNING→RETRY_WAIT),
    `ready_from_retry()` (RETRY_WAIT→READY), `dead_letter_node()`
    (→DEAD_LETTERED + cancel downstream). Cancellation racing backoff wins
    cleanly: `process_retry_waits()` short-circuits once `cancel_requested_`
    is set, and a parked node is canceled, never re-dispatched.
  - **Tests added (M32 step 9 — all five required scenarios):**
    - C++ `retry_policy_test` (NEW, 38 checks): taxonomy classification,
      policy floor (never-retry classes ignore worker hint), unknown-needs-hint,
      attempt budget, backoff bounds + deterministic jitter, dead-letter
      decision.
    - C++ `distributed_run_loop_test` +4 scenarios: (14) transient-then-success
      (2 attempts, retry event, downstream proceeds), (15) permanent fail-fast
      (1 attempt, no retry, downstream canceled), (16) repeated transient →
      dead-letter (exactly max_attempts, run FAILED, downstream canceled),
      (17) cancellation during backoff (parked node canceled, not re-dispatched,
      run CANCELED), and (18) **retry after worker lease expiry** (killed
      attempt reaped → re-dispatched → transient fail → RETRY_WAIT → success on
      attempt 3; reap consumes no retry budget).
    - C++ `pg_run_store_test`: retry persistence (set_node_retry_wait round-trip,
      terminal guard) vs live PG.
    - TS `run-view-model.test` +5 checks: retry-then-success fold, backoff reads
      as running (not failed), dead-letter folds to terminal failed, dead-letter
      does not overwrite a completed node, and durable snapshot maps
      retry_wait→running / dead_lettered→failed (25/25).
- **Concurrency/distributed correctness:** the run loop is single-threaded and
  owns `retry_due_`; backoff never blocks a thread. Retry/dead-letter
  transitions are guarded by the state machine AND the store's terminal guard,
  so cancellation racing backoff wins cleanly (test 17). A late result for a
  reaped attempt is bounded by the existing late-result rule (M22/M26).
  Lease-expiry recovery and retry compose without double-counting attempts
  (test 18). Duplicate deliveries remain idempotent (M26 dedup unchanged).
- **Timestamps:** `retry_wait_until` and the due-time comparison are wall-clock
  UTC ms (durable boundary); steady_clock is never persisted for retry.
- **Phase-1 preservation:** legacy Trigger.dev engine untouched; no Phase-1
  default behavior changed; no test removed; browser credentials stay
  server/worker-only. Migration is additive and local-only.
- **No-go compliance:** authorization/validation never retried blindly (policy
  floor, asserted); external_io side effects NOT retried by default (idempotency
  is M33); no performance numbers invented; no future component marked
  implemented; no secret/credential committed.
- **Validation:**
  - CMake build → ✅ clean; `ctest` (Release) → ✅ 19/19
  - ASan+UBSan → ✅ 19/19; TSan → ✅ 19/19 (no data races)
  - `npm test` → ✅ exit 0 across 13 suites (run-view-model 25/25 incl. 5 new
    M32, all prior suites green)
  - `npm run typecheck` → ✅; `npm run lint` → ✅ (0 errors, 0 warnings)
  - Postgres audit assertions → ✅ (pg_run_store retry persistence vs live PG;
    migration 0005 applied locally)
- **Known limitations:** `grpc_integration` uses `pick_free_port()` (bind-0,
  release, reuse) — a pre-existing M17 TOCTOU pattern that can flake under
  parallel load; it passes consistently on re-run and is not M32-related.
  Retry-latency/throughput benchmarking is deferred to M39. external_io retry
  is intentionally OFF pending the M33 idempotency strategy. Live-browser retry
  behavior (a real Browserbase session re-opened across a retry) is covered by
  the mocked-session suites; live E2E deferred to the final campaign.
- **Human action:** none (local PG migration only; Neon migration still
  requires explicit approval).
- **COMMIT:** `dcad613` — `phase2(m32): add node retries backoff and dead lettering`
- **NEXT:** M33 — Implement idempotency and duplicate suppression.

## M33 — Implement idempotency and duplicate suppression

**Status:** ✅ DONE — at-least-once delivery is now safe to discuss honestly: duplicate result application is suppressed by a durable ledger, and duplicate/crash-recovered side-effecting deliveries produce exactly one external effect via provider-side idempotency.

- **BASE_SHA:** `f67b93b` (phase2 branch; M32 SHA recorded).
- **What was inspected:** master prompt M33 spec (steps 1–19, no-go list,
  validation, exit criteria), the existing dedup landscape —
  `engine/core/src/distributed_run_loop.cpp` (`apply_result`, in-memory
  `ResultDedupe`, late-result rule), `engine/core/{include,src}/evo/envelope.*`
  (attempt identity + dedupe), `engine/core/{include,src}/run_store.*` +
  `engine/pg/{include,src}/pg_run_store.*` (at-most-once completion,
  parameterized SQL), `lib/db/schema.ts` + migration 0001 (the pre-existing
  `idempotency_records` table), `worker/src/{worker,node-executor-adapter,
  email-test-sink}.ts`, `features/workflows/nodes/{node-executors,send-email}.ts`,
  the installed Resend SDK 6.17.2 (`idempotencyKey` option) + Resend
  send-email docs (Idempotency-Key header confirmed supported),
  `engine/tests/{distributed_run_loop_test,pg_run_store_test}.cpp`,
  `worker/src/{worker,node-executor-adapter}.test.ts`,
  `docs/phase2/DECISIONS.md`.
- **What changed:**
  - **Logical operation key (M33 step 1):** `result_idempotency_key()` in
    `envelope.{hpp,cpp}` derives `result:{run_id}:{node_id}:{attempt_number}`
    from the attempt identity — deterministic, so a duplicate delivery of the
    same result claims the same key.
  - **Durable idempotency ledger (M33 step 2):** `claim_idempotency_key` +
    `get_idempotency_response` added to the `RunStore` interface and both
    implementations. Backed by the EXISTING `idempotency_records` table
    (M19; `key` is the PRIMARY KEY) — `INSERT ... ON CONFLICT (key) DO
    NOTHING`, so first claim wins and a duplicate affects 0 rows. NO new
    migration was needed. The committed response is stored and readable for
    reuse.
  - **Scheduler duplicate-result suppression (M33 step 3):** `apply_result`
    now claims the durable ledger AFTER the in-memory `ResultDedupe` fast path
    and BEFORE the late-result rule. The in-memory set stays the same-process
    fast path; the durable ledger is the authoritative gate that survives a
    scheduler restart. A duplicate result never re-applies, never
    double-unlocks successors, never double-frees a resource slot.
  - **Reuse committed output (M33 step 4):** a duplicate SUCCESS delivery's
    output is stored in the ledger; `get_idempotency_response` returns the
    first committed output (asserted equal to the node's persisted output).
  - **send-email provider-side idempotency (M33 step 5):** Resend's send-email
    endpoint supports an `Idempotency-Key` header (confirmed in the installed
    SDK 6.17.2 `idempotencyKey` option + Resend docs). `sendEmail` now accepts
    an optional `idempotencyKey` and forwards it; `NodeContext` carries it;
    the node-executor-adapter derives a deterministic key
    `side-effect:{run_id}:{node_id}` from the LOGICAL OPERATION (run + node),
    NOT the attempt number. Undefined key (legacy Trigger.dev path) => Resend's
    default behavior, unchanged.
  - **Crash window closed by design (M33 step 6):** because the side-effect key
    is stable across attempts, a worker that performs the effect and dies before
    the result is durably applied gets its lease reaped (M31) and the node
    re-dispatched as a NEW attempt — but the re-execution reuses the SAME key,
    so the provider returns the original effect instead of sending twice. The
    residual ambiguity (a single in-flight provider call whose outcome is not
    locally knowable, e.g. timeout after provider commit) is documented, not
    hidden; retrying with the same key is safe.
  - **Deterministic fake email sink (M33 step 7):** `createEmailTestSink` now
    mirrors Resend's provider-side dedup — a repeated `idempotencyKey` returns
    the cached result WITHOUT recording a new send. Tests count ACTUAL side
    effects via `sent.length`. Output shape unchanged (M24 parity preserved).
  - **Duplicate classes injected (M33 step 8):** duplicate task delivery
    (worker executes both, provider dedupes => 1 send), duplicate result
    delivery (ledger suppresses => 1 application), crash-after-result /
    lease-reap re-dispatch (same side-effect key => 1 send), lost-ack
    redelivery (dup-storm + at-least-once transport).
  - **Tests added:**
    - C++ `distributed_run_loop_test`: extended the dup-storm test with durable
      ledger evidence (exactly one claim for the applied result; ledger response
      == node's persisted output).
    - C++ `pg_run_store_test`: M33 ledger section — first claim creates,
      duplicate claim suppressed by the PK unique constraint, duplicate reuses
      the first committed response, unknown key has none, empty key rejected,
      failure claim (empty response) still claims. vs live PG.
    - TS `node-executor-adapter.test`: M33 side-effect idempotency — first
      delivery sends once, duplicate delivery does NOT double-send (reuses
      output), crash-recovery re-dispatch (new attempt) does NOT double-send,
      a new run (re-run) sends a fresh email.
    - TS `worker.test`: M33 duplicate task delivery over real Redis — the SAME
      task envelope published twice hands off two results (at-least-once) but
      the fake sink records exactly ONE send.
- **Evidence table (M33 step 9) — suppressed vs. remaining ambiguity:**
  - SUPPRESSED: duplicate result application (durable ledger, survives
    restart); duplicate successor unlock; duplicate resource-slot free;
    duplicate task-delivery side effect (provider key); crash-recovery
    re-dispatch side effect (same logical key).
  - REMAINING AMBIGUITY: a single ambiguous in-flight provider call (e.g. a
    network timeout after the provider committed but before the worker saw the
    response) — the outcome is not locally knowable. Retrying with the same key
    is safe (provider dedupes). This is inherent to at-least-once + external
    side effects; we do NOT claim exactly-once (M33 no-go).
- **Concurrency/distributed correctness:** the run loop is single-threaded and
  owns result application; the ledger claim is a single conditional INSERT
  (unique constraint is the invariant guard). Two workers cannot both apply the
  same result (the ledger admits one). The side-effect key is derived
  deterministically from the envelope, so any worker re-executing the same
  logical operation uses the same key. Duplicate deliveries are acked (consumed,
  never reprocessed). A late result is still bounded by the late-result rule.
- **Timestamps:** no new timestamps introduced by M33; `idempotency_records.
  created_at` is DB `now()` (wall-clock). Result/side-effect keys carry no
  timestamps.
- **Phase-1 preservation:** legacy Trigger.dev engine untouched; `sendEmail`
  without a key keeps Resend's default behavior; no Phase-1 default behavior
  changed; no test removed; browser credentials stay server/worker-only. No new
  migration; the reused table is additive from M19 and local-only.
- **No-go compliance:** never claim exactly-once (evidence table states the
  residual ambiguity); the ledger alone does not eliminate every external
  side-effect ambiguity (documented); no performance numbers invented; no future
  component marked implemented; no secret/credential committed.
- **Validation:**
  - CMake build → ✅ clean; `ctest` (Release) → ✅ 19/19
  - ASan+UBSan → ✅ 19/19; TSan → ✅ 19/19 (no data races)
  - `npm test` → ✅ exit 0 across 13 suites (worker 13/13 incl. 1 new M33,
    adapter 5/5 incl. 1 new M33, all prior suites green)
  - `npm run typecheck` → ✅; `npm run lint` → ✅ (0 errors, 0 warnings)
  - Postgres audit assertions → ✅ (pg_run_store M33 ledger checks vs live PG)
- **Known limitations:** `grpc_integration` uses `pick_free_port()` (pre-existing
  M17 TOCTOU pattern) — passes consistently on re-run, not M33-related. The
  single ambiguous in-flight provider call remains inherently ambiguous
  (documented above). Provider-side idempotency is wired for `send-email` only
  (the sole external side-effect node); browser/pure nodes are idempotent by
  nature or read-only. Duplicate-suppression throughput benchmarking deferred to
  M39. Live Resend idempotency (a real API call) is covered by the mocked-sink
  suites; live E2E deferred to the final campaign.
- **Human action:** none (no new migration; reused the M19 local table; Neon
  untouched).
- **COMMIT:** `2887e88` — `phase2(m33): add idempotency and duplicate suppression`
- **NEXT:** M34 — Implement worker crash recovery and failure injection.

## M34 — Implement worker crash recovery and failure injection

**Status:** ✅ DONE — task reassignment is now DEMONSTRATED, not described: a real TS worker is SIGKILLed mid-task while holding its lease, the scheduler reaps the expired lease, re-dispatches the node as a new attempt on a surviving worker, and no logical task is lost. Recovery timelines are recorded as raw samples.

- **BASE_SHA:** `c23855d` (phase2 branch; M34 was claimed in-progress with
  uncommitted work, which this session reviewed, FIXED, tested, and completed).
- **What was inspected:** master prompt M34 spec (steps 1–8, no-go list,
  validation, exit criteria), the uncommitted M34 working tree (crash recovery
  test, run-loop browser-affinity test, `apply_result` late-result reorder,
  `main.ts` lease-cadence env config, CMake target), `engine/tests/
  distributed_e2e_test.cpp` (M26 spawn pattern), the `npx tsx` process-tree
  behavior on this host, `docs/phase2/FAILURE_MODEL.md` §4 (browser loss
  policy), `docs/phase2/BENCHMARK_METHODOLOGY.md` §4 (artifact format).
- **What changed:**
  - **Fault-injection harness (M34 step 1):** `engine/tests/crash_recovery_test.cpp`
    (NEW) drives a real multi-process fleet — `DistributedRunLoop` (this process)
    + 2 spawned TS workers (`worker/src/main.ts`) over real Redis + Postgres —
    over a `start -> {slow, quick} -> join` DAG where `slow` is a 4000ms
    `bench:sleep`. It waits until SOME worker acquires the `(slow, attempt 1)`
    lease, records the injection timestamp, SIGKILLs that exact worker, then
    asserts recovery. 3 trials by default (`EVO_M34_TRIALS`).
  - **CRITICAL FIX found this session — process-group kill:** `npx tsx` spawns
    a 3-level tree (`npm exec` → `node tsx` → `node main.ts`). The original
    harness killed only the spawned pid, which left the REAL worker running —
    the "killed" worker finished its own task (1 attempt, no reap), and the
    orphan held CTest's stdout pipe open, wedging ctest until its 300s timeout.
    Fixed with `POSIX_SPAWN_SETPGROUP` (whole tree in its own pgid) +
    `kill(-pid, SIGKILL)` (signal the whole group), and worker stdout/stderr
    redirected to `/dev/null` so no orphan can wedge CTest on pipe EOF.
  - **Reassignment on another worker (M34 step 2):** the killed worker's lease
    expires (short 1500ms lease, env-configurable via M34 `main.ts` change),
    the scheduler's expired-lease scan reaps the attempt to `lease_expired`,
    and re-dispatches the node as attempt 2, which the SURVIVING worker
    completes. Asserted: run succeeds, exactly 2 attempts for `slow`, attempt 2
    on a different worker, `slow` succeeded, unaffected siblings + join
    succeeded, single logical commit (output present).
  - **Recovery timeline recorded (M34 step 3):** per-trial wall-clock UTC ms —
    `t_inject` (SIGKILL), `t_lease_expired` (scheduler reap),
    `t_replacement_acquired` (new worker lease), `t_run_complete` — plus
    derived reap/reassign/recovery latencies.
  - **No lost task + no corruption (M34 steps 4–5):** asserted per trial (run
    succeeds, `slow` succeeded, single terminal success with output). Duplicate/
    late completion cannot corrupt state — the M33 ledger + late-result rule
    (reordered BEFORE the durable claim this milestone) are the guards.
  - **Browser-affinity resource-loss policy (M34 step 6):** run-loop test 19
    (NEW, `distributed_run_loop_test.cpp`) proves the documented policy for
    browser work: when the OWNING worker of a capacity-1 browser affinity key
    dies, the slot is RELEASED on lease reap (not leaked), the node
    re-dispatched as a new attempt on a FRESH session, and the downstream
    browser node then runs on the freed slot. No transparent session
    continuation is claimed. (`make_browser_chain`: b1 trigger + b2 action, both
    `open-url` => same capacity-1 key.)
  - **Raw recovery artifacts (M34 step 7):** when `EVO_M34_ARTIFACT_DIR` is
    set, writes `manifest.json` (slug/workload/resource_class/build_mode/
    commit/hardware/trials/clock/note), `samples.jsonl` (per-trial timeline),
    `summary.json` (min/median/max recovery_latency_ms), and `command.txt`,
    following BENCHMARK_METHODOLOGY.md §4. Committed under
    `engine/bench-results/m34/`. Timings are DIAGNOSTIC (single local stack),
    not evidence-grade benchmark numbers.
  - **Separate recovery documentation (M34 step 8):** synthetic/non-browser
    work recovers by reassignment on a surviving worker (crash test);
    browser-affinity work recovers by resource-loss + re-create on a fresh
    session (run-loop test 19). Documented separately, never conflated.
  - **`apply_result` late-result reorder:** the late-result rule now runs
    BEFORE the durable idempotency claim, so a forged/late result can never
    pollute the ledger's "first committed output".
  - **`worker/src/main.ts` lease-cadence env config:** `EVO_WORKER_LEASE_DURATION_MS`
    / `_RENEW_INTERVAL_MS` / `_HEARTBEAT_INTERVAL_MS` (undefined => Worker's
    production defaults). Lets the crash test use short leases; production
    unchanged.
  - **`engine/CMakeLists.txt`:** `evo_crash_recovery_test` target (gated on
    hiredis + libpq), 300s timeout, `EVO_REPO_ROOT_DIR` baked for worker spawn.
- **Measured recovery evidence (diagnostic, 3 trials, Release, Apple M2 arm64,
  commit recorded in manifest):** reap_latency ≈ 1570–1672ms (bounded by the
  1500ms lease + 100ms scan), reassign_latency ≈ 114–121ms, recovery_latency
  (SIGKILL → run complete) ≈ 6460–6552ms (includes the 4000ms re-execution of
  the slow task on the replacement worker). These are DIAGNOSTIC samples — no
  resume number is claimed; evidence-grade recovery benchmarking is M39.
- **Concurrency/distributed correctness:** the run loop is single-threaded and
  owns lease scanning; the reap is an at-most-once conditional UPDATE (M31),
  so a racing completion can never be double-completed. The killed worker's
  in-flight task produces NO result (process dead), so there is no late result
  to race; the late-result rule + ledger bound any hypothetical one. The
  browser affinity slot is released exactly once on reap (no leak, no double
  free). Recovery uses wall-clock UTC ms at every durable boundary.
- **Phase-1 preservation:** legacy Trigger.dev engine untouched; no Phase-1
  default behavior changed; no test removed; browser credentials stay
  server/worker-only. `main.ts` lease-cadence config defaults to production
  values when unset.
- **No-go compliance:** the kill targets an ACTIVE lease-holding worker mid-task
  (not an idle worker); recovery claims specify workload/resource class
  (synthetic INTERNAL vs browser-affinity, documented separately); no
  performance numbers invented; no future component marked implemented; no
  secret/credential committed.
- **Validation:**
  - CMake build → ✅ clean; `ctest` (Release) → ✅ 20/20 (incl. crash_recovery
    27.5s, distributed_run_loop incl. browser-affinity test 19)
  - ASan+UBSan → ✅ 20/20; TSan → ✅ 20/20 (no data races)
  - `npm test` → ✅ exit 0 across 13 suites (all prior suites green; no new TS
    suite — the crash harness is C++-driven, `main.ts` change is config-only)
  - `npm run typecheck` → ✅; `npm run lint` → ✅ (0 errors, 0 warnings)
  - Postgres/Redis audit assertions → ✅ (crash recovery vs live Redis + PG;
    browser-affinity slot release vs in-memory store)
  - Recovery artifacts → ✅ `engine/bench-results/m34/` (manifest/samples/
    summary/command with commit + hardware provenance)
- **Known limitations:** `grpc_integration` uses `pick_free_port()` (pre-existing
  M17 TOCTOU pattern) — passes consistently, not M34-related. Recovery latencies
  are diagnostic single-stack samples; evidence-grade multi-trial benchmarking
  deferred to M39. Browser-affinity recovery is proven at the scheduler/slot
  level (in-memory store); a live Browserbase session re-open across a crash is
  covered by the mocked-session suites, live E2E deferred to the final campaign.
  The crash test spawns workers via `npx tsx` (adds ~1.5s startup); acceptable
  for a fault-injection harness.
- **Human action:** none (no new migration; reused M19/M31 local tables; Neon
  untouched).
- **COMMIT:** `b6cff19` — `phase2(m34): implement and measure worker crash recovery`
- **NEXT:** M35 — Implement scheduler restart recovery and durable reconciliation.

## M35 — Implement scheduler restart recovery and durable reconciliation

**Status:** ✅ DONE — the orchestrator is now RESTARTABLE without forgetting active runs: a real scheduler process is SIGKILLed mid-run (workers/Redis/Postgres stay alive), a second scheduler process restarts with `resume=true`, reconstructs logical state from the durable store, drains the dead scheduler's pending result messages, and drives the run to a consistent terminal outcome. No logical task is lost and no node is double-completed. Restart-with-no-active-work is a clean no-op (no resurrection).

- **BASE_SHA:** `603b389` (phase2 branch; M35 was claimed in-progress with
  uncommitted work, which this session reviewed, completed, and validated).
- **What was inspected:** master prompt M35 spec (steps 1–8, no-go list,
  validation, exit criteria), the uncommitted M35 working tree (run-store
  readers, transport `read_pending`, `SchedulerState::reconstruct`, run-loop
  `reconstruct_from_store`/`drain_pending_results`, `resume` config, migration
  0006, schema mirror, `run_loop_driver.cpp`, `scheduler_restart_test.cpp`,
  CMake targets), `engine/app/grpc_service.cpp` (M29 distributed submit path),
  `docs/phase2/ARCHITECTURE.md` §7 (durable timestamps are wall-clock UTC),
  `docs/phase2/FAILURE_MODEL.md` (Postgres is the audit source).
- **What changed:**
  - **Durable topology (M35 step 1):** migration `0006_phase2_run_dag_json.sql`
    adds `workflow_runs.dag_json text`; `lib/db/schema.ts` mirrors `dagJson`.
    `RunRecord.dag_json` is persisted by `create_run` (NULLIF => NULL for
    legacy/empty; ON CONFLICT backfills only when the existing row lacks it)
    and read back by `get_run`. The canonical engine DAG is captured at
    submission (`run.dag_json = dag_.to_json_string()`) so a restarted
    scheduler can reconstruct the run's topology from durable state.
  - **RunStore readers (M35 step 2):** `list_node_runs(run_id)` (ordered by
    node_id) and `list_active_evo_run_ids()` (engine='evo' AND status IN
    queued/running, ordered by id) added to the `RunStore` interface and both
    implementations (InMemory + PG). These let startup identify every
    non-terminal Evo run and read its persisted node rows.
  - **Transport pending reclaim (M35 step 3):** `read_pending(stream, group,
    consumer)` added to `TaskTransport` (InMemory + Redis). Redis uses
    `XREADGROUP GROUP g c COUNT 1 STREAMS s 0` (id "0" => the consumer's own
    PEL, non-blocking). A recovery loop drains by `read_pending -> apply ->
    ack`, so results the pre-crash loop read but did not ack are not lost.
  - **State reconstruction (M35 steps 4–5):** `SchedulerState::reconstruct(rows)`
    maps each persisted node status to a logical state (terminal stays terminal;
    dispatched/running => RUNNING in-flight; retry_wait parked; else BLOCKED),
    rebuilds `completed_`/`results_`/`failure_reasons_` for terminal nodes,
    re-derives `dep_counts_` (remaining = predecessors not Succeeded), and
    promotes non-terminal BLOCKED nodes with 0 remaining deps to READY. An
    in-flight node is restored to RUNNING so its valid lease is respected — the
    loop does NOT dispatch a duplicate replacement while a lease still exists
    (M35 step 4); if the lease lapses, the ordinary lease-reap path handles it
    (M35 step 6).
  - **Run-loop resume (M35 steps 3–6):** `DistributedRunConfig.resume`; on
    `run()`, when `resume` is set the loop first checks whether the run is
    already terminal (returns its durable status without re-executing — never
    resurrect a finished run), else calls `reconstruct_from_store()`: rebuilds
    logical state, restores per-node attempt numbers / retry due-times /
    in-flight resource slots, honors a durable cancel request, then
    `drain_pending_results()` (read_pending -> apply_result -> ack) before the
    main loop resumes dependency scheduling. Falls back to fresh-run init when
    there is nothing to reconstruct.
  - **gRPC startup reconciliation (M35 steps 2–3):** `grpc_service.cpp`
    `reconcile_active_runs()` runs once at startup BEFORE the server accepts
    RPCs: lists every non-terminal Evo run from Postgres, reconstructs each
    run's DAG from its persisted `dag_json`, and re-drives it with `resume=true`
    (same `scheduler-grpc` consumer id so the pending-entry list is reclaimed).
    Runs with missing/invalid `dag_json` (pre-M35 rows) are logged and skipped
    — the documented small restart window that is not recoverable (M35 no-go).
    Best-effort: unreachable infra or an unrecoverable run never blocks the
    server from starting.
  - **Scheduler driver binary (M35 step 7):** `engine/app/run_loop_driver.cpp`
    (NEW) runs ONE `DistributedRunLoop` against real Redis + Postgres so the
    restart test can SIGKILL the scheduler as a REAL process and restart it
    with `resume=true`. Fixed `start -> {slow, quick} -> join` DAG (mirrors the
    M34 crash DAG); exit 0 on SUCCEEDED.
  - **Restart integration test (M35 steps 7–8):** `engine/tests/
    scheduler_restart_test.cpp` (NEW) drives a real multi-process fleet — driver
    scheduler + 2 spawned TS workers over real Redis + Postgres. Per trial:
    spawn fleet, wait for the `(slow, attempt 1)` lease, SIGKILL the scheduler's
    process group (workers survive), assert the run is still non-terminal,
    restart with `resume=1`, assert the run reaches SUCCEEDED with every node
    succeeded exactly once (no double-completion), then restart AGAIN against
    the now-terminal run and assert it exits cleanly with no new attempts (no
    resurrection). 2 trials by default (`EVO_M35_TRIALS`). Skips cleanly when
    Redis/Postgres/tsx/the driver are unavailable.
  - **Unit tests (M35 steps 4–8):** `distributed_run_loop_test.cpp` tests
    20–23 — resume-with-no-durable-state falls back to a fresh run; restart
    mid-run reconstructs + resumes to success without re-dispatching an
    already-succeeded node and resumes downstream dependency scheduling; restart
    never resurrects an already-terminal run; resume drains the consumer's
    pending (unacked) result and applies it to the existing attempt.
  - **`engine/CMakeLists.txt`:** `evo-run-loop-driver` target + `evo_scheduler_
    restart_test` target (gated on hiredis + libpq), 300s timeout,
    `EVO_REPO_ROOT_DIR` baked for worker spawn, `EVO_M35_DRIVER_BIN` pointed at
    the real driver binary.
- **Concurrency/distributed correctness:** Postgres remains the durable source
  of truth (Redis alone is never the audit database). Reconstruction is
  single-threaded per run loop; the at-most-once terminal completion guard
  (M26) + idempotency ledger (M33) + late-result rule mean a duplicate or late
  result across the restart can never double-complete a node. An in-flight
  node's valid lease is respected on resume (no duplicate replacement); an
  expired lease is reaped by the ordinary M31 path. A durable cancel request
  made before the crash is honored on resume. All durable timestamps are
  wall-clock UTC ms.
- **Phase-1 preservation:** legacy Trigger.dev engine untouched; no Phase-1
  default behavior changed; no test removed; browser credentials stay
  server/worker-only. The `dag_json` column is additive (NULL for legacy rows);
  reconciliation only touches engine='evo' runs.
- **No-go compliance:** Postgres remains the audit source; the small
  non-recoverable restart window (pre-M35 rows with no `dag_json`) is documented
  above; no performance numbers invented (no resume number claimed — this is not
  a benchmark milestone); no future component marked implemented; no
  secret/credential committed.
- **Validation:**
  - CMake build → ✅ clean; `ctest` (Release) → ✅ 21/21 (incl.
    scheduler_restart 17.6s, distributed_run_loop incl. tests 20–23)
  - ASan+UBSan → ✅ 21/21; TSan → ✅ 21/21 (no data races)
  - `npm test` → ✅ exit 0 (all prior suites green; M29 parity 9/9)
  - `npm run typecheck` → ✅; `npm run lint` → ✅ (0 errors, 0 warnings)
  - Postgres/Redis audit assertions → ✅ (restart recovery vs live Redis + PG;
    no double-completion; no resurrection)
- **Known limitations:** restart recovery reconstructs LOGICAL state from
  Postgres; a run whose `dag_json` was never persisted (pre-M35 rows) cannot be
  reconstructed and is skipped (logged) — the documented non-recoverable
  window. A live Browserbase session owned by a worker that ALSO died is handled
  by the M34 resource-loss policy (lease reap -> re-dispatch on a fresh
  session), not by session continuation. Recovery timing is not benchmarked
  (evidence-grade recovery benchmarking is M39).
- **Human action:** migration 0006 applied to the LOCAL Phase-2 Postgres only
  (`scripts/phase2/migrate-local.sh`); Neon untouched.
- **COMMIT:** `fb5313e` — `phase2(m35): add scheduler restart recovery`
- **NEXT:** M36 — Multi-tenant quotas and backpressure.

## M36 — Add multi-tenant quotas and backpressure

**Status:** ✅ DONE — one organization can no longer exhaust the scheduler's global resources. A shared `TenantQuotaGate` enforces per-org active-run admission (reject with RESOURCE_EXHAUSTED when exhausted), per-org in-flight task limits, and global resource-class capacities (browser-session and side-effect pools tracked separately). Over-capacity dispatch is DEFERRED (backpressure), never silently queued without bound.

- **BASE_SHA:** `90a5079` (phase2 branch; M35 docs SHA).
- **What was inspected:** master prompt M36 spec (steps 1–9, no-go list,
  validation, exit criteria), `engine/core/include/evo/execution_policy.hpp`
  (ResourceClass + ResourcePolicy), `engine/core/src/distributed_run_loop.cpp`
  (dispatch_ready / apply_result / scan_expired_leases / reconstruct_from_store
  / terminal paths), `engine/app/grpc_service.cpp` (SubmitRun admission,
  submit_local/submit_distributed, reconcile_active_runs, Health),
  `engine/core/include/evo/metrics.hpp` (counter conventions),
  `engine/core/include/evo/retry_policy.hpp` (error taxonomy).
- **What changed:**
  - **`engine/core/include/evo/quota.hpp` + `engine/core/src/quota.cpp` (NEW):**
    `TenantQuotaGate` — the single cross-run authority for quotas. Thread-safe
    (one mutex). Two limit axes: (1) run-level ADMISSION — per-org and global
    caps on concurrently ACTIVE runs; `admit_run` rejects when either is full,
    `readmit_run` re-counts a resumed run without checking caps (M35 restart
    recovery), `release_run` frees a slot at terminal. (2) task-level CAPACITY —
    per-org in-flight task cap + global per-ResourceClass capacity;
    `acquire_task` grants a slot only when BOTH permit (atomic: no partial
    mutation on rejection), `release_task` returns it (clamps at zero),
    `reacquire_task` re-counts a restored in-flight node without checking caps.
    `QuotaCounters` tracks admitted/rejected runs, acquired/deferred/released
    tasks, active-run depth + high-water. `to_json_string()` emits a structured
    snapshot (per-org depth + per-class in-flight). All limits default to 0 =
    unlimited (backwards compatible).
  - **`engine/core/include/evo/distributed_run_loop.hpp` + `.cpp`:**
    `DistributedRunConfig.quota_gate` (optional shared gate; nullptr => no
    cross-run gating). `dispatch_ready()` acquires a slot after the affinity
    check; a full gate DEFERS the node (left READY, re-examined next iteration
    — backpressure, not rejection) and releases the affinity slot grabbed
    first. `release_quota_slot()` returns a slot exactly once per node
    (`quota_held_` set is the guard against double-release from duplicate
    results / racing lease reap / retry park). Release sites: applied result
    (main loop + M35 pending-drain), lease reap, and the two local-failure
    dispatch paths. `release_all_quota_slots()` runs on the cancel/timeout/stop
    terminal paths so force-canceled in-flight nodes do not leak capacity.
    `reconstruct_from_store()` re-acquires slots for restored in-flight nodes so
    capacity accounting survives a restart.
  - **`engine/app/grpc_service.cpp`:** service-wide `quota_gate_` member
    configured from env (`EVO_QUOTA_MAX_ACTIVE_RUNS_PER_ORG`,
    `_MAX_ACTIVE_RUNS_GLOBAL`, `_MAX_INFLIGHT_TASKS_PER_ORG`,
    `_BROWSER_CAPACITY`, `_EXTERNAL_IO_CAPACITY`; 0 => unlimited). `SubmitRun`
    admits the org before parsing the DAG and rejects with
    `grpc::RESOURCE_EXHAUSTED` when the cap is exhausted (the org_id originates
    from the authenticated server-side submission — Clerk — never a browser
    client, per M36 step 9). The admitted slot is released on every rejection
    path (malformed DAG, unreachable infra) and on terminal in all three runner
    threads (local, distributed, reconcile). The distributed loop + reconcile
    loops receive `dcfg.quota_gate = &quota_gate_`. `Health` detail now carries
    the quota gate's JSON snapshot (queue depth + rejected/deferred counters,
    M36 step 6). `EVO_LOCAL_SLEEP_MS` test-only override (default 3ms,
    unchanged) lets the admission test hold a run ACTIVE.
  - **`engine/tests/quota_test.cpp` (NEW):** 12 pure unit tests — unconfigured
    gate admits everything; per-org + global active-run caps; release/readmit;
    per-org task cap; global class capacity (browser vs external-io separate
    pools); release/reacquire; full counter lifecycle; double-release clamps at
    zero; JSON snapshot parses.
  - **`engine/tests/distributed_run_loop_test.cpp` (tests 24–26):** tenant
    isolation (noisy org fan-out serialized to its per-org cap of 1 while a
    small org completes unaffected; deferred dispatches counted; no slot leak);
    global browser-session capacity across two runs (high-water == 1); global
    ExternalIo capacity across two runs (separate pool, browser untouched).
    New helpers: `ConcurrencyTrackingWorker` (per-class high-water),
    `OrgTrackingWorker` + `OrgConcurrencyTracker` (per-org high-water),
    `make_single_browser_run` / `make_single_email_run` / `make_fanout_run3`.
  - **`engine/tests/quota_admission_test.cpp` (NEW):** gRPC integration —
    spawns `evo-scheduler-server` with per-org cap 1 + 2s local sleep; asserts
    org-a run #1 accepted, org-a run #2 rejected RESOURCE_EXHAUSTED, org-b run
    accepted (own quota), Health carries the quota snapshot, and org-a run #3
    accepted after #1 releases its slot.
  - **`engine/CMakeLists.txt`:** `quota.cpp` in `evo_scheduler_core`;
    `evo_quota_test` + `evo_quota_admission_test` targets (admission pointed at
    the real server binary, 60s timeout).
- **Concurrency/distributed correctness:** the gate is the single owner of its
  counters; every method locks the internal mutex, so concurrent run loops
  (one thread each) admit/acquire/release safely — TSan-clean across the full
  suite. `acquire_task` is atomic (no partial mutation on rejection). A slot is
  released exactly once per node (`quota_held_`), so a duplicate result, a
  lease reap racing a terminal result, or a retry park can never double-release
  (inflate capacity). Force-cancel terminal paths release all held slots. The
  gate is in-process state: global capacity is enforced per scheduler process,
  not cluster-wide (a durable cluster-wide counter is later work).
- **Phase-1 preservation:** legacy Trigger.dev engine untouched; no Phase-1
  default behavior changed (all limits default to 0 = unlimited); no test
  removed; browser credentials stay server/worker-only. `EVO_LOCAL_SLEEP_MS`
  defaults to the prior 3ms.
- **No-go compliance:** backpressure is explicitly NOT fairness (deferral does
  not reorder tenants; fair scheduling is M37); no unbounded per-tenant queues
  (over-capacity submission is rejected, over-capacity dispatch is deferred and
  re-examined); no performance numbers invented; no future component marked
  implemented; no secret/credential committed.
- **Validation:**
  - CMake build → ✅ clean; `ctest` (Release) → ✅ 23/23 (incl. quota,
    quota_admission 5.0s, distributed_run_loop incl. tests 24–26)
  - ASan+UBSan → ✅ 23/23; TSan → ✅ 23/23 (no data races in the shared-gate
    concurrency tests)
  - `npm test` → ✅ exit 0 (all prior suites green; M29 parity 9/9)
  - `npm run typecheck` → ✅; `npm run lint` → ✅ (0 errors, 0 warnings)
- **Known limitations:** the quota gate is per-process; a multi-scheduler
  deployment would need a durable cluster-wide counter (later work). Backpressure
  defers but does not reorder — a saturated org's deferred nodes are re-examined
  each iteration but not prioritized against other tenants (fairness is M37).
  The per-org in-flight task cap is enforced at dispatch, so a node may wait
  READY until its org's in-flight count drops.
- **Human action:** none (no new migration; no schema change; Neon untouched).
- **COMMIT:** `d490b7b` — `phase2(m36): add tenant quotas and backpressure`
- **NEXT:** M37 — Fair scheduling and starvation resistance.

## M37 — Implement fair scheduling and starvation resistance

**Status:** ✅ DONE — one tenant's backlog can no longer monopolize a contended
resource class. Fair scheduling is an OPT-IN mode on the shared `TenantQuotaGate`
(`fair_scheduling = true`): when a capped resource class has more demand than
capacity, the gate grants the next slot to the least-served tenant (weighted
least-served-first, a pull-based weighted-round-robin analog), so a small tenant
is served within a bounded number of grants instead of waiting behind a large
tenant's whole backlog. Default remains M36 FCFS (backwards compatible).

- **BASE_SHA:** `91c5df2` (phase2 branch; M36 docs SHA).
- **What was inspected:** master prompt M37 spec (steps 1–18, no-go list,
  validation, exit criteria), `engine/core/include/evo/quota.hpp` +
  `engine/core/src/quota.cpp` (M36 gate), `engine/core/src/distributed_run_loop.cpp`
  (dispatch_ready / acquire_quota_slot / main-loop re-examination),
  `engine/core/src/execution_policy.cpp` (ResourceClass mapping + browser
  affinity key), `engine/app/grpc_service.cpp` (make_quota_config env wiring),
  `engine/tests/distributed_run_loop_test.cpp` (tests 24–27 helpers),
  `engine/proto/evo/execution.proto` (TaskEnvelope.became_ready_at),
  `docs/phase2/BENCHMARK_METHODOLOGY.md` (Jain's index + artifact format).
- **What changed:**
  - **`engine/core/include/evo/quota.hpp` + `engine/core/src/quota.cpp`:**
    `QuotaConfig` gains `fair_scheduling` (opt-in; default false = M36 FCFS),
    `org_weights` (absent => weight 1), `fair_demand_timeout_ms` (0 => default
    5000ms). `acquire_task` (fair path, capped classes only): registers/refreshes
    the org's demand FIRST, then — if the class still has a free slot — picks the
    recipient via `fair_recipient_locked`: drop stale demand (older than the
    timeout), then choose min `served/weight` via cross-multiplication
    (`served * best_weight < best_served * weight`), ties broken by org-id
    iteration order. If the chosen recipient is not the caller, the caller is
    DEFERRED (`fair_order_deferrals` counted) and re-examines next loop iteration.
    On grant the org's `served_` count increments and its demand is cleared
    (liveness: a granted tenant stops blocking others). `served_count`,
    `demand_count`, `weight_for` accessors; `QuotaCounters.fair_order_deferrals`;
    `to_json_string` includes fair_scheduling, per-org weight, served_by_class.
  - **`engine/app/grpc_service.cpp`:** `make_quota_config` parses
    `EVO_FAIR_SCHEDULING` (bool), `EVO_FAIR_DEMAND_TIMEOUT_MS`, and
    `EVO_ORG_WEIGHTS` (`"org-a:2,org-b:1"` format; absent => weight 1).
  - **`engine/tests/quota_test.cpp` (tests 13–18):** fairness off = FCFS; equal
    weights least-served wins; weighted (weight-2 earns 2 per 1; sequence a,b,a);
    starvation resistance (org-a served 5x, org-b then served within bounded
    grants); demand cleared on grant + stale demand dropped (1ms timeout + 5ms
    sleep); fairness applies only to capped classes.
  - **`engine/tests/distributed_run_loop_test.cpp` (test 27):** end-to-end
    starvation resistance — a large tenant's 5-node sequential browser backlog
    (30ms each) does not starve a small tenant's single browser node; with fair
    scheduling ON + global browser capacity 1, the small tenant's run reaches a
    terminal state BEFORE the large tenant's backlog completes, and the gate
    granted the small tenant exactly one browser slot (deterministic proof it was
    served, not skipped). Helpers: `make_browser_chain_n`, `OrderRecordingWorker`.
  - **`engine/tests/fairness_bench_test.cpp` (NEW):** M37 fairness benchmark
    (steps 6–7). K tenants × T browser fan-out tasks against a global browser
    capacity of 1, fair scheduling ON. Workload A (equal duration): asserts
    Jain(span) ≥ 0.90 and Jain(served) == 1.0 (equal slot grants). Workload B
    (unequal duration, tenant 0 is 3x slower): asserts every tenant completes
    (no starvation) + slot grants stay equal (Jain(served) == 1.0) + the slow
    tenant holds the slot longer in aggregate (durations exercised). Reports
    per-org queue-wait max/median. Emits manifest.json / samples.jsonl /
    summary.json / command.txt when `EVO_M37_ARTIFACT_DIR` is set.
  - **`engine/CMakeLists.txt`:** `evo_fairness_bench_test` target (links
    `evo_distributed`, 120s timeout).
- **Concurrency/distributed correctness:** the gate remains the single owner of
  its counters; every method locks the internal mutex, so concurrent run loops
  acquire/defer/release safely — TSan-clean across the full suite. Demand
  registration precedes the capacity check (no lost-wakeup ordering bug); demand
  is cleared on grant (a granted tenant cannot keep blocking others). Fairness is
  enforced at the shared gate, which is the only cross-org arbiter in the
  pull-based architecture (each run loop dispatches independently). The gate is
  in-process state (same as M36): fairness is per scheduler process, not
  cluster-wide.
- **Where browser affinity legitimately reduces ideal fairness (M37 step 8):**
  within a run, all browser nodes share a capacity-1 affinity key (one browser
  session per run), so a tenant presents ONE browser task at a time to the global
  pool regardless of how many browser nodes it has ready. Fairness is therefore
  guaranteed at the level of SLOT GRANTS (Jain(served) == 1.0), not completion
  SPANS: a tenant with a deep browser backlog cannot parallelize its own tasks,
  so its end-to-end span is proportional to (tasks × duration) even under perfect
  grant fairness. Under round-robin fair scheduling the slow/large tenant's last
  task can be granted before a fast tenant's last task, so span ordering is NOT a
  fairness property — the benchmark asserts grant counts + no starvation, and
  reports Jain(span) as info. This is the intended, documented interaction
  between per-run browser affinity (M12) and cross-tenant fairness (M37).
- **Phase-1 preservation:** legacy Trigger.dev engine untouched; no Phase-1
  default behavior changed (fair_scheduling defaults to false = M36 FCFS); no
  test removed; browser credentials stay server/worker-only.
- **No-go compliance:** the algorithm is a simple explainable weighted
  least-served-first (documented tradeoff vs deficit/weighted round robin in
  DECISIONS.md); dependency readiness / resource affinity kept separate from
  tenant selection (only the cross-tenant SELECTION order changes); fairness
  tests use deterministic workloads (fixed tenant/task counts, fixed work_ms, no
  randomness); no performance numbers invented (benchmark timings labeled
  diagnostic, not evidence-grade — that is M39); no future component marked
  implemented; no secret/credential committed.
- **Validation:**
  - CMake build → ✅ clean; `ctest` (Release) → ✅ 24/24 (incl. quota tests
    13–18, distributed_run_loop test 27, fairness_bench)
  - ASan+UBSan → ✅ 24/24; TSan → ✅ 24/24 (no data races in the shared-gate
    fairness concurrency)
  - `npm test` → ✅ exit 0 (all prior suites green; M29 parity 9/9)
  - `npm run typecheck` → ✅; `npm run lint` → ✅ (0 errors, 0 warnings)
- **Benchmark (diagnostic, single local stack — not evidence-grade):**
  - Command: `EVO_M37_ARTIFACT_DIR=engine/bench-results/m37 ./build/evo_fairness_bench_test`
  - Workload A (3 tenants × 4 tasks, equal 15ms): Jain(span)=0.994,
    Jain(served)=1.000, deferrals=31; per-org max_wait 37–77ms, median ~35–38ms.
  - Workload B (tenant 0 = 45ms, others 15ms): Jain(span)=0.998,
    Jain(served)=1.000; slow tenant busy time 190ms vs fast 66ms (durations
    exercised); every tenant completed.
  - Artifacts: `engine/bench-results/m37/{manifest.json,samples.jsonl,summary.json,command.txt}`
    (24 raw per-task dispatch/complete samples; commit + hardware + build-mode
    provenance in manifest).
- **Known limitations:** fairness is per scheduler process (in-process gate); a
  multi-scheduler deployment would need a durable cluster-wide served/demand
  ledger (later work). Fairness applies only to CAPPED resource classes (an
  uncapped class has no contention to arbitrate). Weighted fairness is configured
  via `EVO_ORG_WEIGHTS` but not yet surfaced in any product UI. The demand-timeout
  default (5000ms) means a tenant that stops refreshing demand is dropped from
  arbitration after 5s (liveness), which is correct for pull-based dispatch but
  worth noting for very long idle windows.
- **Human action:** none (no new migration; no schema change; Neon untouched).
- **COMMIT:** `02cc98e` — `phase2(m37): add fair multi-tenant scheduling`
- **NEXT:** M38 — Observability, service security, CI quality gates.

---

## M38 — Add observability, service security, and CI quality gates

- **Status:** ✅ DONE (session B)
- **Objective:** Make the distributed engine diagnosable and continuously
  verifiable without live secrets.
- **What was implemented:**
  - **Structured JSON logging (C++ + TS) with secret redaction.**
    `engine/core/src/log.cpp` (`evo::log::emit`) and `worker/src/logger.ts`
    (`createWorkerLogger`) both emit one JSON line per event to stderr with
    correlation fields (run/node/attempt/org/worker/trace ids). Any field whose
    KEY matches a secret-like pattern (password/secret/token/credential/
    authorization/api_key/private_key) has its value replaced with `[REDACTED]`;
    the TS logger also redacts Bearer tokens and URL-embedded credentials.
    Redaction is defense in depth — callers must not pass secrets in the first
    place. Verified by `engine/tests/log_test.cpp` + `worker/src/logger.test.ts`.
  - **Trace/correlation id propagation.** `DistributedRunConfig.trace_id` is set
    from the SubmitRun request (defaulting to the run id) and copied into every
    dispatched TaskEnvelope (`env.set_trace_id(...)`), so one id follows a run
    across scheduler→Redis→worker. OpenTelemetry was NOT added (no exporter/trace
    path proven this milestone, per M38 step 4); correlation is via explicit ids.
  - **Prometheus metrics.** `MetricsRegistry` (thread-safe counter/gauge) +
    `prometheus.cpp` text formatter, exposed by a minimal loopback HTTP server
    (`engine/app/metrics_http.hpp`) on `EVO_METRICS_PORT` (default 9090, `0`
    disables). Binds `INADDR_LOOPBACK` only. `GET /metrics` returns
    `text/plain; version=0.0.4`.
  - **Service-to-service engine-token auth.** When `EVO_ENGINE_TOKEN` is set,
    every RPC must carry `authorization: Bearer <token>` (constant-time compare,
    `engine/core/src/auth_token.cpp`) else `UNAUTHENTICATED`; when unset, auth is
    disabled (backwards compatible; default binds loopback only). The server-only
    TS client (`features/workflows/lib/evo-scheduler-client.ts`) reads the token
    from its own env and attaches it as call metadata — it never reaches a
    browser. Verified by `auth_token_test.cpp` (17) + `auth_integration_test.cpp`
    (negative + positive + backwards-compat).
  - **Input size limits + identifier validation at the gRPC trust boundary**
    (`engine/core/src/input_limits.cpp`): ids must be `[A-Za-z0-9._-]` ≤256
    bytes; DAG JSON ≤8 MiB. Validated BEFORE quota admission and any durable
    write. 18 unit tests.
  - **GitHub Actions CI** (`.github/workflows/ci.yml`): secret-scan, node
    (test/typecheck/lint/build), cpp-gcc, cpp-clang, cpp-asan, cpp-tsan,
    distributed (Redis+Postgres+gRPC service containers), bench-smoke. Requires
    NO Browserbase/Resend/Clerk/Neon/paid keys (M38 step 8): Node suites skip
    cleanly without live services, the production build succeeds without
    `SENTRY_AUTH_TOKEN`/`.env.local`, and the distributed job uses the synthetic
    `bench:*` executor (never a real browser).
  - **`scripts/secret-scan.sh`** — scans git-tracked files for high-signal secret
    patterns with a documented allowlist; runs as the `secret-scan` CI job.
  - **`scripts/phase2/bench-smoke.sh`** — benchmark HARNESS smoke: asserts the
    manifest is well-formed (header + commit/build metadata + raw + summary
    samples) and NEVER asserts timing on shared runners (M38 step 9).
  - **`docs/phase2/SECURITY.md`** — trust boundaries, implemented controls,
    threat-model table (T1–T10), explicit no-go items, and honest "not covered"
    limitations.
- **CI-enabling engine changes (all surfaced by actually building on GCC/Linux):**
  - **Proto layer split + graceful degradation.** `evo_proto` is now
    MESSAGES-ONLY (`execution.pb.cc`); a separate `evo_proto_grpc` holds the gRPC
    stubs. Both protobuf and gRPC are OPTIONAL in CMake (`EVO_HAVE_PROTO` /
    `EVO_HAVE_GRPC`), mirroring the hiredis/libpq pattern. The distributed runtime
    needs only protobuf messages, so the distributed CI job builds with just
    `libprotobuf-dev`; core-only jobs skip the proto layer entirely.
  - **CI regenerates gencode** against the runner's own protobuf
    (`engine/proto/generate.sh`, now platform-aware with `--messages-only`),
    because the committed gencode is pinned to the exact protobuf runtime it was
    generated with (`PROTOBUF_VERSION` guard) which stock runners lack.
  - **macOS dual-Homebrew guard:** on Apple Silicon, `CMAKE_PREFIX_PATH` is
    prepended with `/opt/homebrew` so protobuf/gRPC/absl resolve to the
    native-arch prefix (a hintless `find_package` otherwise picks the x86_64
    `/usr/local` protobuf while pkg-config's grpc++ resolves arm64 → link failure).
  - **Cross-platform portability fixes:** (a) `bench_runner.cpp` `#ifdef __APPLE__`
    left a dangling `<<` on non-Apple (syntax error) — now emits
    apple/linux/unknown; (b) `posix_spawn_file_actions_addchdir` is macOS/BSD-only
    (Linux glibc names it `..._np`) — added `tests/spawn_chdir.hpp` shim used by
    the 3 spawn-based tests; (c) `build-hiredis.sh` word-split `ARCHFLAGS` into a
    bogus make target and called `file` (absent on minimal images) — now exports
    ARCHFLAGS and guards `file`; (d) `bench.cpp` `volatile` compound assignment
    (`acc +=`) is deprecated in C++20 — replaced with simple assignment.
  - **distributed_e2e Linux pipe-wedge fix:** the test now spawns workers with
    stdout/stderr→`/dev/null` + `POSIX_SPAWN_SETPGROUP` and kills the whole
    process group on shutdown (same pattern as M34/M35), so the `npx` wrapper's
    child can't outlive SIGTERM and hold CTest's output pipe open on Linux. The
    final "no pending tasks" check became a bounded poll (still requires 0
    pending) to tolerate async ack propagation. Timeout bumped 180s→300s.
  - **`tsconfig.json` + `eslint.config.mjs` exclude `engine/`** — CMake's Makefile
    generator emits `compiler_depend.ts` files under `engine/build/` that the
    `**/*.ts` glob otherwise picked up, breaking `npm run build`/`tsc`/`eslint` on
    a Linux runner. The engine is pure C++ the app never type-checks/lints.
  - **`scripts/phase2/migrate-local.sh`** gained a direct-TCP mode (auto-detected,
    or forced via `EVO_PHASE2_MIGRATE_DIRECT=1`) for CI, where Postgres runs as a
    service container and the runner connects over TCP instead of `docker exec`.
- **Validation (measured, not copied):**
  - macOS Release `ctest` → ✅ 29/29 (incl. all M38 tests + distributed e2e/crash/
    restart). ASan+UBSan → ✅ 29/29. TSan → ✅ 29/29.
  - Linux (ubuntu-24.04, GCC 13) core-only build → ✅ 16/16 (proto layer skipped;
    libpq absent). Linux distributed (proto layer, protoc 3.21) → ✅ 5/5
    (envelope, retry_policy, distributed_run_loop, redis_transport, pg_run_store).
    Linux distributed_e2e (full repo + Node 20 + real Redis/Postgres) → ✅ PASS
    (4.18s) after the pipe-wedge fix.
  - `npm test` → ✅ exit 0 (all suites green; M29 parity 9/9). `npm run typecheck`
    → ✅ exit 0. `npm run lint` → ✅ exit 0 (0 errors). `npm run build` (no
    `.env.local`, no `SENTRY_AUTH_TOKEN`) → ✅ exit 0, all routes compiled.
  - `scripts/secret-scan.sh` → ✅ clean (patterns verified to fire on known-bad
    samples). `scripts/phase2/bench-smoke.sh` → ✅ PASS (raw=240 summary=24;
    timings diagnostic only, not asserted).
  - `.github/workflows/ci.yml` → YAML valid (parsed).
- **Known limitations:** the engine token is a single shared service secret, not
  per-org (tenant isolation is the app's Clerk auth + per-org quota). No TLS on
  the gRPC channel (loopback today). Redis has no auth by default (loopback
  binding). Secret scanning is a repo-tuned heuristic, not a dedicated scanner.
  Metrics carry no auth (loopback only). All documented in SECURITY.md §5.
- **Human action:** none (no new migration; no schema change; Neon untouched; no
  secrets committed).
- **COMMIT:** `f9aa65e` — `phase2(m38): add observability security and ci gates`
- **NEXT:** M39 — Final reproducible performance, scaling, and chaos campaign.

---

## M39 — Run the final reproducible performance, scaling, and chaos campaign

**Status:** ✅ DONE (session B) — the evidence from which resume metrics may be
written now exists, measured on the reference machine and preserved as raw
artifacts. No number below was chosen in advance; every one is derived from the
captured samples.

- **BASE_SHA:** `b4b651d` (M39 claim commit; campaign frozen at this SHA).
- **What was inspected:** master prompt M39 spec (steps 1–25, no-go list,
  Appendix S scaling catalog, Appendix T failure protocol F01–F25),
  `docs/phase2/BENCHMARK_METHODOLOGY.md` (§1 no-fabrication, §4 artifact format,
  §5 publication gate), `engine/app/bench_runner.cpp` (M15 corpus runner),
  `engine/core/{include/evo,src}/bench.{hpp,cpp}` (synthetic tasks + RNG),
  `engine/core/include/evo/{concurrent_scheduler,metrics,scheduler,dag}.hpp`
  (per-node timestamps + M13 RunMetrics), `engine/tests/crash_recovery_test.cpp`
  (M34 artifact pattern + spawn/process-group pattern),
  `engine/tests/fairness_bench_test.cpp` (M37 artifact pattern),
  `engine/tests/distributed_e2e_test.cpp` (spawn_worker + audit pattern),
  `engine/core/include/evo/{transport,run_store}.hpp` + `engine/redis/...` +
  `engine/pg/...` (bounded retry/backoff: base 50ms, cap 2s, max 5 attempts),
  `worker/src/{main.ts,synthetic-executor.ts,redis-streams.ts}` (ioredis
  reconnect), `scripts/phase2/{up.sh,lib.sh,bench-smoke.sh}`, `.gitignore`
  (results dir is gitignored by convention, matching M34).
- **What changed (new evidence infrastructure; no product behavior touched):**
  - **`engine/app/m39_local_campaign.cpp` (NEW):** final local-scheduler
    campaign. Sequential reference vs ConcurrentScheduler across shapes
    {linear, diamond, wide, layered, seeded-random} × sizes {10,50,100,500,1000}
    × profiles {simulated I/O-bound `bench:sleep`, synthetic CPU `bench:burn`}
    × threads {1,2,4,8}, with warmup + repeated trials, steady_clock durations.
    Emits §4 artifacts (manifest/samples/summary/command). Emits BOTH
    `speedup` (seq-vs-con, meaningful for io) and `thread_scaling_vs_1t`
    (con t1 vs tN, the honest CPU signal — the cooperative CPU task polls its
    stop_token every 256 iters, so seq-vs-con understates the concurrent
    scheduler's own scaling).
  - **`engine/tests/m39_scaling_test.cpp` (NEW):** distributed worker scaling
    (Appendix S). Worker counts {1,2,4} × logical tasks {100,500}, wide DAG of
    `bench:sleep` (INTERNAL/unbounded), 2 repeated trials per cell with the
    fleet spawned once per cell (identical worker conditions). Audits the
    durable store per trial (every task succeeded, exactly one attempt — no
    lost/duplicated work). Emits §4 artifacts with median throughput +
    scaling-vs-1-worker. Skips cleanly without Redis/Postgres/tsx.
  - **`engine/tests/m39_chaos_test.cpp` (NEW):** infrastructure outage chaos
    (Appendix T F09–F12). `docker pause`/`unpause` the Redis container (~2.5s)
    and the Postgres container (~2.0s) mid-run, injected only after ≥3 tasks
    are observably dispatched. Verifies the run reaches a terminal state and
    records recovery-to-success as measured evidence (a non-recovery is
    preserved, not hidden, per methodology step 15). Emits §4 artifacts.
    Skips cleanly without docker/the containers.
  - **`scripts/phase2/m39-campaign.sh` (NEW):** orchestrator. Freezes the
    benchmark SHA + records git state, reconfigures so `EVO_BUILD_COMMIT`
    matches the frozen SHA, runs local + scaling + chaos + M34 crash-recovery
    + M37 fairness evidence + the 27-scenario distributed_run_loop fault audit,
    then assembles `engine/benchmarks/results/<ts>_m39_<sha>/` with
    campaign.json, per-leg §4 artifacts, fault_audit.txt, checksums.sha256,
    and a REPORT.md derived from the raw data. **Rosetta guard:** refuses to
    record emulation-tainted timing — detects `sysctl.proc_translated=1` and
    self-heals by re-executing via `arch -arm64 /bin/bash` (bare `bash` on this
    machine is an x86_64-only Homebrew build; `arch -arm64 bash` fails with
    "Bad CPU type").
  - **`engine/CMakeLists.txt`:** `evo-m39-local` (Release evidence runner, not a
    CTest target) + `evo_m39_scaling_test` + `evo_m39_chaos_test` (CTest, 600s
    timeout, gated on hiredis+libpq+proto like the other distributed tests).
- **Measured results (reference machine: Apple M2, 8 cores, Darwin arm64,
  Apple clang 21, Release; frozen SHA `b4b651d`; results dir
  `engine/benchmarks/results/20260822-141648_m39_b4b651d/`):**
  - **Local scheduler, simulated I/O-bound — speedup vs sequential reference
    (near-linear):** t1 1.03x, t2 2.01x, t4 4.01x, **t8 8.01x** (efficiency
    ~1.00). Reproducibility re-run: t8 8.09x (within noise).
  - **Local scheduler, synthetic CPU — thread scaling (con t1 vs tN):** t2
    2.01x, t4 3.75x, **t8 5.57x**. Re-run: 5.73x. (seq-vs-con NOT reported for
    CPU — see thread_scaling note above.)
  - **Distributed worker scaling (wide DAG, bench:sleep):** 1w→2w gives
    1.08x–1.14x; **4 workers is SLOWER than 1** (0.81x–0.87x). This is a real,
    preserved result, not hidden: the TS worker already parallelizes tasks
    internally via async, so adding worker processes does not add parallelism
    for these fine-grained synthetic tasks — the bottleneck is the
    single-threaded C++ result-consumption loop + Redis transport round-trips,
    and extra workers add claim/contention overhead. Coarser-task diagnostic
    (100ms) confirmed the same shape (4w 0.65x). This is the honest scaling
    ceiling of the current single-loop architecture for I/O-bound synthetic
    work; it is an architecture finding, not a defect in the evidence.
  - **Infrastructure outage chaos:** Redis outage (2.5s pause) → run reached
    terminal, **recovered to success 30/30 tasks** (makespan 5330ms). Postgres
    outage (2.0s pause) → **recovered to success 30/30** (4714ms). Both
    survived via the transport/store bounded reconnect backoff.
  - **Worker crash recovery (M34, SIGKILL lease-holder):** recovery latency
    (SIGKILL → run complete) min 6413 / median 6470 / max 6489 ms over 3 trials.
  - **Fair scheduling (M37):** Jain index — equal workload span 0.995 / served
    1.0; unequal workload span 0.997 / served 1.0 (no starvation).
  - **Fault outcome audit:** `distributed_run_loop_test` (27 scenarios:
    duplicate result, identity validation, failure path, malformed payload,
    cancellation races, lease expiry/reassignment, retry/dead-letter, restart
    recovery, tenant isolation, starvation resistance) → 100% passed.
- **Validation (measured, not copied):**
  - macOS Release `ctest` → ✅ 31/31 (incl. new m39_scaling 60.4s, m39_chaos
    12.7s). ASan+UBSan → ✅ 31/31. TSan → ✅ 31/31.
  - Reproducibility: re-ran the local campaign; headline speedups within noise
    (io t8 8.09x vs 7.86x; cpu t8 5.73x vs 5.57x).
  - Raw/summary calculation check: recomputed every summary median from raw
    samples → 200 local cells + 6 scaling cells, **0 mismatches**.
  - `checksums.sha256` over all 27 artifact files → 0 FAILED.
  - Campaign hardware record verified native arm64 (a first run under Rosetta
    was detected, discarded, and the guard added — see below).
- **Known limitations / honest caveats:**
  - Distributed worker scaling does NOT improve beyond ~2 workers for
    fine-grained synthetic I/O tasks (see measured 4-worker regression and its
    explanation above). This is the current architecture's ceiling, preserved
    as evidence.
  - Local scheduler numbers are scheduler-only synthetic and MUST NOT be
    generalized to browser end-to-end speedup (methodology §1.7).
  - No Browserbase/live external E2E campaign was run this milestone (no paid
    keys authorized; Appendix U.6). Browser end-to-end performance is dominated
    by network + LLM latency, not scheduling.
  - Per-node ready-to-dispatch/queue-latency percentiles are captured by the
    local campaign (full steady_clock timestamps); the distributed campaign
    reports makespan/throughput/retries/errors from the durable store.
  - The results directory is gitignored by convention (matching M34); it is
    referenced here and reproducible via `scripts/phase2/m39-campaign.sh`.
- **Rosetta-taint incident (transparency):** the first campaign run executed
  under Rosetta (x86-64 emulation) because the background shell's `bash` is an
  x86_64-only Homebrew build; it recorded `Darwin x86_64` and its timings were
  emulation-tainted. That results directory was **discarded** (never published),
  a Rosetta guard + native self-heal was added to the orchestrator, and the
  campaign was re-run natively (recorded `Darwin arm64`). Only the native run
  is retained as evidence.
- **Human action:** none (no migration, no schema change, Neon untouched, no
  secrets, no paid external calls).
- **COMMIT:** `200b6ee` — `phase2(m39): capture final benchmark and chaos evidence`
- **NEXT:** M40 — Final Phase-2 audit, documentation, release, and resume
  evidence registry.
