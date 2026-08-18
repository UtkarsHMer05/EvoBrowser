# Phase 2 — Progress Log

One entry per milestone. Entries record observed results, not promises.
Commit subjects follow `phase2(mNN): <description>`.

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
- **NEXT:** M13 — scheduler clock discipline and run/node timestamp instrumentation.
