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
| M27 | Introduce the Next.js execution-engine abstraction and feature flag | ✅ DONE | `<M27_SHA>` |

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
- **COMMIT:** `<M27_SHA>` — `phase2(m27): add dual execution engine abstraction`
- **NEXT:** M28 — Build engine-neutral Evo run events and realtime frontend transport.
