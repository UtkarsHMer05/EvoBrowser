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