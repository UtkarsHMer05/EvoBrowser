# EvoBrowser Phase 2 — Authoritative Master Implementation Prompt

## C++20 Concurrent DAG Scheduler + Distributed Worker Runtime + Evidence-Grade Benchmarks

**Repository:** `https://github.com/UtkarsHMer05/EvoBrowser`

**Prepared for:** EvoBrowser / Evo Builder Phase 2

**Date baseline:** 2026-08-18

**Purpose:** Give an autonomous coding agent one authoritative operating document that extends the completed Phase-1 product without regressing it, implements a real C++20 concurrent/distributed execution engine, validates every milestone, creates one local Git commit per milestone, and produces only evidence-backed performance claims.

---

# 0. READ THIS FIRST — THIS FILE IS AN OPERATING CONTRACT

You are the implementation agent for **EvoBrowser Phase 2**.

Your task is not to produce a proposal and stop.

Your task is to inspect the real checked-out repository, reconcile it with the baseline described here, and then implement the milestones in order until Phase 2 is complete or a genuine human-only blocker is reached.

The highest-priority rule is:

> **Correctness and evidence are more important than impressive wording.**

The second-highest-priority rule is:

> **Phase 1 is a behavioral compatibility baseline. Phase 2 builds beneath and beside it; Phase 2 does not casually rewrite it.**

The third-highest-priority rule is:

> **Never write a resume metric first and then try to make the experiment match it. Measure first, preserve raw evidence, then derive wording from the evidence.**

This file deliberately contains much more detail than an ordinary coding prompt. Treat the detail as guardrails, not as permission to ignore the repository you actually have in front of you.

# 1. VERIFIED PHASE-1 BASELINE FROM THE SUPPLIED ARCHIVE

The supplied Phase-1 archive was inspected before this prompt was generated.

The archive contains a completed TypeScript/Next.js project with the following important properties.

## 1.1 Current product behavior that must remain intact

- A user can create a workflow and enter a natural-language automation goal.
- A server-side AI planner converts that goal into a schema-validated workflow plan.
- The planner catalog is derived from the live node registry rather than an unrelated hard-coded list.
- Generated workflows become normal React Flow + Liveblocks collaborative state.
- The user reviews and edits the graph before execution.
- Nothing executes merely because the AI planner returned a result.
- The user explicitly presses Run.
- The current execution path uses a Trigger.dev task.
- The current task performs a topological ordering and walks the connected nodes sequentially.
- Browser nodes share one Browserbase/Stagehand session per run.
- Browser execution waits for the Live Browser panel using the existing live-view handshake.
- Act / Observe / Extract / Open URL / Agent can produce live in-page visual highlights.
- Run step metadata is streamed to the existing UI.
- Stop cancels the current run.
- Stagehand cleanup is performed in a `finally` path.
- A final screenshot is captured before the browser session closes.
- The results UI shows readable node output, timing, final URL, screenshot, and replay.
- The workflow remains editable after completion/failure/cancellation.
- A rerun gets a fresh run identity and fresh browser session.
- Clerk organizations and plan gates remain server enforced.
- Neon Postgres + Drizzle are the current application database layer.
- Sentry is already present for existing application observability.

## 1.2 Current workflow node catalog observed in the archive

- `start`
- `open-url`
- `act`
- `extract`
- `observe`
- `agent`
- `send-email`

Do not add new public workflow node types merely to manufacture concurrency numbers.

Benchmark-only synthetic tasks may exist inside the Phase-2 engine without becoming user-facing planner nodes.

## 1.3 Current database state observed in the archive

The existing Drizzle schema currently contains the main concepts:

- `workflows`
- `live_view_connections`
- `run_artifacts`

There is not yet a Phase-2 durable model for:

- immutable workflow versions
- engine-neutral workflow runs
- node-run records
- task attempts
- worker leases
- idempotency records

These are Phase-2 additions and must be additive migrations.

## 1.4 Current run path observed in the archive

Conceptually the current path is:

```text
React Flow / Liveblocks graph
        ↓
runWorkflowAction
        ↓
saveWorkflowGraph
        ↓
Trigger.dev tasks.trigger("run-workflow")
        ↓
runWorkflowTask
        ↓
topological order
        ↓
for each node in order
        ↓
interpolate inputs
        ↓
nodeExecutors[node.type]
        ↓
Stagehand / Browserbase / Resend
```

That path is the **legacy Phase-1 execution engine**.

Do not delete it during Phase 2.

## 1.5 Current tests observed in the archive

The Phase-1 package exposes `npm test` and contains three workflow-oriented suites:

- plan conversion/editability
- execution integration/regression
- lifecycle regression

The exact current test counts must be re-measured from the checked-out source before work begins.

Never copy a test count from this document into a README without re-running the tests.

## 1.6 Current known limitations that motivate Phase 2

- sequential node execution
- independent DAG branches are not scheduled concurrently
- a single browser resource is shared by a workflow run
- no immutable workflow-version history
- no node-level retry model
- no distributed worker fleet
- no worker leases / heartbeats
- no custom task crash recovery
- no Phase-2 idempotency ledger
- no scheduler-level tenant fairness / backpressure
- no reproducible scheduler/distributed benchmark corpus yet

These limitations are opportunities, not permission to break the working product.

# 2. PHASE-2 TARGET ARCHITECTURE

The final architecture should preserve the Phase-1 product/control plane while introducing a second execution engine.

```text
                             USER
                              │
                              ▼
                 AI Planner + Visual Canvas
                     (existing Phase 1)
                              │
                              ▼
                    Explicit Run / Stop
                              │
                              ▼
                    Execution Abstraction
                       /              \
                      /                \
             Legacy Engine           Evo Engine
             Trigger.dev             custom path
                 │                       │
                 │                       ▼
                 │                 gRPC / Protobuf
                 │                       │
                 │                       ▼
                 │               C++20 Orchestrator
                 │              ┌───────────────────┐
                 │              │ DAG scheduler     │
                 │              │ ready queues      │
                 │              │ jthread pool      │
                 │              │ resource limits   │
                 │              │ tenant fairness   │
                 │              │ cancellation      │
                 │              │ retries / leases  │
                 │              └─────────┬─────────┘
                 │                        │
                 │                   Redis Streams
                 │                        │
                 │          ┌─────────────┼─────────────┐
                 │          │             │             │
                 │          ▼             ▼             ▼
                 │      TS Worker 1   TS Worker 2   TS Worker N
                 │          │             │             │
                 │          └─────────────┼─────────────┘
                 │                        │
                 │            existing node executors
                 │                        │
                 │          Stagehand / Browserbase / Resend
                 │                        │
                 └──────────────┬─────────┘
                                │
                                ▼
                     Normalized run events/state
                                │
                                ▼
                  Existing EvoBrowser run experience
```

## 2.1 Why C++ is used

C++ is responsible for problems where it is technically justified:

- dependency-aware scheduling
- concurrent state transitions
- bounded worker-thread execution for local/synthetic tasks
- synchronization
- resource accounting
- fairness
- cancellation coordination
- task dispatch
- durable distributed orchestration
- performance-sensitive metrics collection

C++ is **not** used to rewrite Stagehand or Browserbase just to add a keyword to a resume.

## 2.2 Why TypeScript workers remain

The existing browser/email implementations already live in TypeScript.

Distributed workers should reuse those implementations rather than duplicating them in C++.

The worker is allowed to:

- load the immutable workflow version
- load upstream outputs
- run the existing interpolation implementation
- execute the existing node executor
- manage the Stagehand session owned by that worker/run
- publish result/failure metadata

## 2.3 Why Redis Streams is used

Redis Streams provides a practical durable task transport with:

- append-only stream semantics
- consumer groups
- pending entries
- explicit acknowledgement
- replay/redelivery building blocks

The project must still implement its own application-level leases, attempt semantics, retry policy, and idempotency rules.

Do not claim Redis automatically makes the application exactly-once or fault-tolerant.

## 2.4 Why Postgres remains

Neon/Postgres remains the durable application/audit database.

Phase 2 adds immutable workflow versions and engine-neutral run records.

The Drizzle schema remains the schema/migration authority even if the C++ service reads/writes selected Phase-2 tables through a native PostgreSQL client.

# 3. SOURCE-OF-TRUTH HIERARCHY

- The checked-out working tree is the source of truth for code.
- `git status` is the source of truth for uncommitted user work.
- Existing repository instructions such as `AGENTS.md` must be read before coding.
- The current `package.json` is the source of truth for Node scripts and versions.
- The current Drizzle schema/migrations are the source of truth for database structure.
- Official dependency documentation is the source of truth for current external APIs.
- This prompt is the source of truth for Phase-2 intent and invariants, not for exact third-party function signatures.
- If this prompt conflicts with verified current source behavior required for Phase-1 compatibility, preserve the behavior and document the conflict.

# 4. AUTONOMOUS MILESTONE EXECUTION POLICY

- Proceed milestone by milestone in numerical order.
- Do not ask `Should I continue?` after a green milestone.
- Do not skip a milestone because a later milestone looks more interesting.
- Do not combine many milestones into one giant unreviewable commit.
- A milestone is complete only after implementation, tests, diff review, and the required local commit.
- If a milestone is blocked only by an optional external service, complete every non-external part first and clearly isolate what remains.
- Stop only for a genuine human-only blocker, a destructive action requiring consent, missing secret/account setup, or an unresolved regression.
- When blocked, state the exact blocker and the smallest exact human action required.

# 5. HUMAN-INTERVENTION CONTRACT

- Never ask the user to paste secrets into chat.
- Tell the user the environment-variable name and where to set it locally.
- Do not print secret values in logs.
- Do not commit `.env.local` or secret-bearing files.
- Do not silently install global system packages with `sudo`.
- If Docker Desktop, a compiler, CMake, or another system prerequisite is missing, provide the platform-appropriate installation requirement and pause that milestone.
- If an additive database migration needs to be applied to a shared Neon environment, generate and validate it locally first, then ask the user before applying it remotely.
- If Git author identity is not configured, ask the user to configure it; do not invent a name/email.

# 6. GIT AND COMMIT CONTRACT

- Never use destructive reset on user work.
- Never force push.
- Never rewrite Phase-1 history.
- Use a dedicated Phase-2 branch once the baseline is certified.
- Before each commit run `git status --short`, inspect the diff, and run `git diff --check`.
- Stage only files belonging to the current milestone.
- Create one local commit per milestone using `phase2(mNN): <description>`.
- Record the milestone commit SHA in `docs/phase2/PROGRESS.md`.
- Do not push unless the user explicitly authorizes push behavior.

# 7. PHASE-1 REGRESSION CONTRACT

- After every milestone that touches production application code, run the verified Phase-1 test command.
- Run TypeScript type checking and linting after every relevant TypeScript milestone.
- Run the production build at high-risk integration milestones and before final release.
- Do not delete or weaken Phase-1 regression tests merely to make Phase 2 pass.
- If a Phase-1 test must change because an internal representation changed, preserve the behavioral assertion and explain why the test changed.
- Legacy Trigger.dev mode must remain functional until the final compatibility decision.
- AI planner behavior, Liveblocks editing, explicit Run, Stop, live view, results, replay, and rerun remain protected behaviors.

# 8. C++ ENGINEERING CONTRACT

- Use C++20.
- Use RAII for ownership and cleanup.
- Prefer standard-library synchronization primitives where practical.
- Use `std::jthread` and `std::stop_token` for owned worker threads where appropriate.
- Never detach threads.
- Do not busy-spin when a condition variable or blocking transport can be used.
- Do not create one OS thread per workflow node.
- Do not introduce lock-free data structures only for resume wording.
- Do not use atomics when a mutex-protected invariant is clearer.
- Every thread boundary must define exception handling and shutdown behavior.
- Use `std::chrono::steady_clock` for duration measurements.
- Compile with strong warnings on project-owned C++ code.
- Use sanitizers when supported by the current platform/toolchain.

# 9. DISTRIBUTED-SYSTEM SEMANTICS CONTRACT

- Assume messages may be duplicated.
- Assume workers may disappear after receiving a task.
- Assume acknowledgements may be delayed or lost.
- Assume Redis/Postgres/network operations may fail transiently.
- Assume the scheduler can restart.
- Do not infer that a worker is dead merely because it is slow; use time-bounded leases/heartbeats.
- Prefer at-least-once delivery semantics with explicit idempotency/deduplication.
- Document the ambiguity window around irreversible external side effects.
- Do not claim transparent browser-session continuation after a worker crash unless it is actually implemented and tested.

# 10. BROWSER SESSION SAFETY CONTRACT

- Browser-mutating nodes that share one workflow browser session must not execute concurrently against the same page/context.
- Derive browser affinity from execution policy, not from planner prose.
- All default Phase-1 browser nodes in one run share a browser affinity key unless a later explicitly tested design changes that invariant.
- Pin a live browser resource group to one owning distributed worker while the session exists.
- Do not dispatch a second worker to the same browser affinity key simultaneously.
- If that worker dies, treat the browser resource as lost unless reconnection is explicitly supported by verified Browserbase/Stagehand APIs.
- Preserve the existing live-view gate and final screenshot/replay semantics in Evo engine mode.

# 11. DATABASE CONTRACT

- Drizzle remains the migration/schema authority.
- Phase-2 migrations must be additive and reversible where reasonable.
- Never run destructive production migrations without explicit approval.
- Use parameterized SQL from C++.
- Do not store secrets in run/task tables.
- Use immutable workflow-version rows for run snapshots.
- Keep engine-neutral run IDs separate from external provider IDs such as Trigger run IDs.
- Use unique constraints for logical idempotency invariants when appropriate.

# 12. BENCHMARK INTEGRITY CONTRACT

- Never choose target resume numbers in advance.
- Every published number must have a reproducible command and raw result artifact.
- Never compare Debug C++ to Release C++ as a performance claim.
- Record hardware, OS, compiler, build mode, thread count, worker count, workload, seed, warmup count, and sample count.
- Use repeated trials.
- Store raw samples, not only averages.
- Separate scheduler-only synthetic tests from external Browserbase/LLM end-to-end tests.
- Label sleep-based synthetic workloads as simulated I/O-bound workloads.
- Label CPU synthetic workloads as synthetic CPU workloads.
- Do not generalize a synthetic speedup into a browser-automation speedup.
- Do not use shared CI runner timing as final performance evidence.
- If a metric is noisy, report the noise rather than hiding it.

# 13. TARGET STATE MACHINES

The exact names may evolve, but the semantics must be explicit and tested.

## 13.1 Workflow run state

```text
QUEUED
  ↓
RUNNING
  ├────────────→ SUCCEEDED
  ├────────────→ FAILED
  └────────────→ CANCELED
```

A terminal run never returns to RUNNING.

A rerun is a new run row with a new run ID.

## 13.2 Node logical state

```text
BLOCKED
   ↓ dependencies complete
READY
   ↓ scheduler dispatches
DISPATCHED
   ↓ worker accepts/starts
RUNNING
   ├────────────→ SUCCEEDED
   ├────────────→ RETRY_WAIT → READY
   ├────────────→ FAILED
   ├────────────→ DEAD_LETTERED
   └────────────→ CANCELED
```

A logical node may have multiple task attempts but at most one terminal logical success.

## 13.3 Task-attempt state

```text
ENQUEUED
  ↓
LEASED
  ├────────→ SUCCEEDED
  ├────────→ FAILED
  ├────────→ EXPIRED
  └────────→ CANCELED
```

An expired attempt may coexist historically with a replacement attempt.

Late completion from an expired attempt must not corrupt a newer logical result.

## 13.4 Dependency invariant

A node becomes READY if and only if:

- the run is not terminal;
- every required predecessor is logically SUCCEEDED;
- the node itself is not already logically terminal;
- resource/tenant admission has not permanently rejected it.

Resource availability controls dispatch, not dependency correctness.

## 13.5 Fan-in invariant

For `A -> D`, `B -> D`, `C -> D`, node D cannot become ready after only one or two predecessors complete.

The scheduler must survive duplicated completion messages without decrementing the dependency counter twice.

# 14. PERFORMANCE METRIC DEFINITIONS

Use these definitions consistently in code, benchmark reports, README, and resume evidence.

## 14.1 Workflow makespan

`terminal_timestamp - run_start_timestamp`

Do not call queue wait before admission `execution time` unless the report explicitly includes it.

## 14.2 Ready-to-dispatch scheduling latency

`task_enqueued_for_dispatch_timestamp - node_became_ready_timestamp`

This measures scheduler/resource wait after dependency satisfaction.

## 14.3 Queue latency

`worker_started_timestamp - task_transport_enqueue_timestamp`

This measures transport + worker availability.

## 14.4 Node execution latency

`worker_completed_timestamp - worker_started_timestamp`

Do not include retry wait in one-attempt execution latency.

## 14.5 Logical node latency

`logical_terminal_timestamp - logical_first_ready_timestamp`

This may include retries and lease recovery.

## 14.6 Throughput

Use a clearly defined numerator:

- logical tasks completed / second
- workflows completed / minute

Do not mix attempts and logical tasks.

## 14.7 Speedup

`sequential_reference_makespan / concurrent_makespan`

Only compare identical DAG, task durations/work, machine, build configuration, and trial protocol.

## 14.8 Parallel efficiency

`speedup / worker_or_thread_count`

Label which count is used.

## 14.9 Recovery time

For an injected worker death:

`replacement_attempt_started_timestamp - failure_injection_timestamp`

Also report lease duration because it bounds expected detection.

## 14.10 Cancellation latency

Measure at least two values when possible:

- request → scheduler marks canceled
- request → executing worker/browser resource actually stops/closes

Do not hide the difference.

## 14.11 Duplicate suppression

Report:

- duplicate deliveries injected
- duplicate attempts observed
- logical result commits accepted
- duplicate result commits rejected
- external side effects actually observed in the controlled test sink

## 14.12 Fairness

For per-tenant service rates `x_i`, optionally report Jain's fairness index:

`J = (sum(x_i)^2) / (n * sum(x_i^2))`

Also report max/median queue wait by tenant because one scalar fairness metric can hide starvation.

## 14.13 Memory

Prefer peak RSS for process-level benchmark reporting.

Record measurement method and platform.

## 14.14 CPU

Record process CPU and/or machine utilization only when the collection mechanism is reliable on the test platform.

Never invent CPU percentage from wall time.

---

# MILESTONE 01 — Reconcile the real Phase-1 source and archive

## Objective

Find the exact working Phase-1 baseline before any C++ code is written.

## Why this milestone exists

- This milestone isolates **reconcile the real phase-1 source and archive** so failures are attributable and reviewable.
- The milestone is intentionally narrower than the final Phase-2 architecture.
- Do not implement later milestone responsibilities early unless a tiny interface stub is required for compilation.
- Any stub must be clearly marked as non-production and must not be described as a completed feature.

## Mandatory pre-read and repository inspection

- Read `AGENTS.md` and obey repository-local instructions relevant to files you will touch.
- Run `git status --short` and confirm no unrelated user work will be overwritten.
- Read every existing source file that the milestone will modify before editing it.
- Read the nearest tests covering the behavior before changing implementation.
- Inspect current package/library versions rather than assuming APIs from memory.
- Consult current official documentation before using a third-party API that may have changed.

## Entry conditions

- Milestone 00 is DONE, unless this is Milestone 01.
- Working tree contains no unexpected unrelated modifications.
- The current Phase-1 baseline gate is known and available.
- Any dependency introduced in this milestone has a verified version/source and license acceptable for the repository.

## Detailed implementation procedure

1. Read `AGENTS.md`, the current README, Phase-1 implementation report, package scripts, git status, branches, remotes, and recent commits.
2. Compare the checked-out source with the supplied archive and with the current repository default branch if network access is available.
3. Verify the planner files, live browser files, final screenshot path, tests, and current `run-workflow.ts` actually exist.
4. Record the exact source SHA and whether the working tree has uncommitted user changes.
5. If uncommitted user work exists, do not mix Phase 2 into it; stop and report the exact paths/status.
6. Record current Node, npm, and project dependency versions without upgrading them.
7. Create no production code in this milestone.
8. Before editing, write a short local plan listing the exact files expected to change.
9. Prefer the smallest interface that satisfies this milestone and leaves room for later milestones.
10. Keep names consistent with existing repository conventions; do not introduce a parallel naming universe.
11. If an existing helper already implements part of the behavior, reuse it rather than creating a near-duplicate.
12. Keep product-facing behavior backwards compatible unless the milestone explicitly introduces a new opt-in Evo path.
13. When behavior crosses a trust boundary, validate input before mutating durable state.
14. When behavior crosses a thread/process boundary, define ownership, lifetime, timeout, and failure semantics explicitly.
15. When behavior emits a timestamp, state whether it is wall-clock or steady-clock and use it consistently.
16. Add tests in the same milestone as the behavior; do not defer all testing to Milestone 39.
17. Update `docs/phase2/PROGRESS.md` with the observed result, not a future-tense promise.

## Phase-1 preservation checklist

- [ ] AI planner still requires explicit user Run before execution.
- [ ] Generated graph remains editable React Flow / Liveblocks state.
- [ ] Legacy Trigger.dev engine remains available unless a later explicit final decision says otherwise.
- [ ] Clerk auth/org enforcement remains server-side.
- [ ] Agent Pro-plan gate remains fail-closed.
- [ ] Live browser credentials remain server/worker only.
- [ ] Legacy Run/Stop/results/replay/rerun behavior must not regress.
- [ ] Existing node registry/executors are not duplicated without a compelling reason.
- [ ] No test is removed merely because Phase 2 changes an internal representation.

## Concurrency / distributed correctness questions to answer

- [ ] Which component owns the mutable state introduced here?
- [ ] Can two threads/processes modify it concurrently?
- [ ] What lock/transaction/unique constraint protects the invariant?
- [ ] What happens if an event/message is delivered twice?
- [ ] What happens if the process crashes after the first durable write but before the second?
- [ ] What happens when cancellation races completion?
- [ ] What happens when shutdown starts while another thread is waiting?
- [ ] What happens when a worker is slow rather than dead?
- [ ] Does this change browser-session affinity or ownership?
- [ ] Can a late result overwrite a newer logical state?

## Negative / adversarial scenarios

- [ ] Exercise or explicitly mark N/A: invalid/empty identifiers.
- [ ] Exercise or explicitly mark N/A: duplicate request/event.
- [ ] Exercise or explicitly mark N/A: unknown enum or node type.
- [ ] Exercise or explicitly mark N/A: timeout at external boundary.
- [ ] Exercise or explicitly mark N/A: cancellation at an inconvenient moment.
- [ ] Exercise or explicitly mark N/A: process shutdown while work is pending.
- [ ] Exercise or explicitly mark N/A: stale data or stale attempt result.
- [ ] Exercise or explicitly mark N/A: tenant mismatch / unauthorized request when relevant.
- [ ] Exercise or explicitly mark N/A: resource exhaustion / queue full when relevant.
- [ ] Exercise or explicitly mark N/A: malformed serialized payload when relevant.

## Explicit no-go items for this milestone

- No file changes are required unless the user explicitly allows a read-only audit note.
- Do not create the Phase-2 branch until the baseline source is confirmed.
- Do not invent performance numbers.
- Do not mark a future component as implemented.
- Do not silently change Phase-1 default behavior.
- Do not commit secrets or local credential values.

## Required validation

- [ ] ``git status --short``
- [ ] ``git log --oneline --decorate -30``
- [ ] ``git branch --all``
- [ ] `inspect `package.json` and Phase-1 tests`
- [ ] Run current Phase-1 `npm test` when production app code is touched.
- [ ] Run `npm run typecheck` when TypeScript is touched.
- [ ] Run `npm run lint` when TypeScript/React is touched.
- [ ] Run CMake/CTest when C++ is touched.
- [ ] Run production build at integration/high-risk milestones.

## Performance evidence rules for this milestone

- If this milestone is not explicitly a benchmark milestone, do not add a resume number just because a timer was printed.
- If timing is collected for a test, label it diagnostic unless benchmark methodology requirements are satisfied.
- If this milestone changes a previously benchmarked hot path, note that old results are no longer final for the new SHA.
- Store benchmark raw data only in the defined benchmark result structure, not ad-hoc README prose.
- A result must identify workload, build mode, hardware, sample count, and commit before it becomes evidence-grade.

## Human intervention triggers

- If A required secret/account is missing. -> report `STATUS: BLOCKED` with exact manual action; do not guess.
- If A system-level prerequisite such as Docker/CMake/compiler requires user installation. -> report `STATUS: BLOCKED` with exact manual action; do not guess.
- If A shared/remote database migration would be applied. -> report `STATUS: BLOCKED` with exact manual action; do not guess.
- If Git author configuration is missing. -> report `STATUS: BLOCKED` with exact manual action; do not guess.
- If A destructive operation would touch user data or history. -> report `STATUS: BLOCKED` with exact manual action; do not guess.
- If A paid external test would incur nontrivial cost and the user has not already authorized it. -> report `STATUS: BLOCKED` with exact manual action; do not guess.

## Diff-review checklist before commit

- [ ] Review every changed file with `git diff`.
- [ ] Run `git diff --check`.
- [ ] Remove accidental formatting churn outside the milestone scope.
- [ ] Confirm no secret or `.env.local` value appears in the diff.
- [ ] Confirm no generated build tree, node_modules, vcpkg cache, Docker volume, or large transient trace has been staged.
- [ ] Confirm comments/README wording describe implemented behavior, not planned behavior.

## Commit requirement

Create one local commit with subject:

`phase2(m01): record verified phase1 source`

After commit:

- Record the new SHA in `docs/phase2/PROGRESS.md`.
- Record commands/results for the milestone.
- Do not push unless push permission/policy has already been explicitly authorized.
- If all exit criteria are green, proceed automatically to the next milestone.

## Exit criteria

- [ ] All milestone-specific implementation items are complete.
- [ ] All relevant new tests pass.
- [ ] Required Phase-1 regression gates pass.
- [ ] No unresolved race/data-corruption/security issue is knowingly introduced.
- [ ] No secret or accidental generated artifact is staged.
- [ ] Documentation describes current behavior accurately.
- [ ] The milestone has exactly one intended local commit or a clearly documented reason for a split required by repository tooling.

## Mandatory milestone report format

```text
STATUS: DONE or BLOCKED
MILESTONE: exact milestone number and title
BASE_SHA: SHA before milestone
END_SHA: SHA after commit when DONE
WHAT_I_INSPECTED: files/docs/commands read before changes
WHAT_CHANGED: concise implementation summary
FILES_CHANGED: exact paths
TESTS: command -> result
PHASE1_REGRESSION: command -> result or N/A with reason
CPP_VALIDATION: command -> result or N/A
DISTRIBUTED_VALIDATION: command -> result or N/A
METRICS_CAPTURED: actual measured metrics only; otherwise `None`
HUMAN_ACTION: None or exact required action
KNOWN_LIMITATIONS: explicit remaining limitations
COMMIT: commit SHA + subject
NEXT: next milestone number; continue automatically if not blocked
```

---

# MILESTONE 02 — Certify and freeze the Phase-1 behavioral baseline

## Objective

Turn the current working Phase 1 into an explicit regression gate.

## Why this milestone exists

- This milestone isolates **certify and freeze the phase-1 behavioral baseline** so failures are attributable and reviewable.
- The milestone is intentionally narrower than the final Phase-2 architecture.
- Do not implement later milestone responsibilities early unless a tiny interface stub is required for compilation.
- Any stub must be clearly marked as non-production and must not be described as a completed feature.

## Mandatory pre-read and repository inspection

- Read `AGENTS.md` and obey repository-local instructions relevant to files you will touch.
- Run `git status --short` and confirm no unrelated user work will be overwritten.
- Read every existing source file that the milestone will modify before editing it.
- Read the nearest tests covering the behavior before changing implementation.
- Inspect current package/library versions rather than assuming APIs from memory.
- Consult current official documentation before using a third-party API that may have changed.

## Entry conditions

- Milestone 01 is DONE, unless this is Milestone 01.
- Working tree contains no unexpected unrelated modifications.
- The current Phase-1 baseline gate is known and available.
- Any dependency introduced in this milestone has a verified version/source and license acceptable for the repository.

## Detailed implementation procedure

1. Run the complete Phase-1 test command from the current package.json.
2. Run typecheck, lint, and production build when required secrets/environment permit build-time execution.
3. Run or document the current live manual smoke test path without modifying behavior.
4. Create `docs/phase2/PHASE1_BASELINE.md` with exact SHA, commands, pass/fail results, and known external dependencies.
5. Document current sequential behavior and one-browser-session invariant.
6. Document any Phase-1 manual behavior that automated tests do not cover.
7. Create the dedicated Phase-2 branch only after all required baseline checks are green.
8. Before editing, write a short local plan listing the exact files expected to change.
9. Prefer the smallest interface that satisfies this milestone and leaves room for later milestones.
10. Keep names consistent with existing repository conventions; do not introduce a parallel naming universe.
11. If an existing helper already implements part of the behavior, reuse it rather than creating a near-duplicate.
12. Keep product-facing behavior backwards compatible unless the milestone explicitly introduces a new opt-in Evo path.
13. When behavior crosses a trust boundary, validate input before mutating durable state.
14. When behavior crosses a thread/process boundary, define ownership, lifetime, timeout, and failure semantics explicitly.
15. When behavior emits a timestamp, state whether it is wall-clock or steady-clock and use it consistently.
16. Add tests in the same milestone as the behavior; do not defer all testing to Milestone 39.
17. Update `docs/phase2/PROGRESS.md` with the observed result, not a future-tense promise.

## Phase-1 preservation checklist

- [ ] AI planner still requires explicit user Run before execution.
- [ ] Generated graph remains editable React Flow / Liveblocks state.
- [ ] Legacy Trigger.dev engine remains available unless a later explicit final decision says otherwise.
- [ ] Clerk auth/org enforcement remains server-side.
- [ ] Agent Pro-plan gate remains fail-closed.
- [ ] Live browser credentials remain server/worker only.
- [ ] Legacy Run/Stop/results/replay/rerun behavior must not regress.
- [ ] Existing node registry/executors are not duplicated without a compelling reason.
- [ ] No test is removed merely because Phase 2 changes an internal representation.

## Concurrency / distributed correctness questions to answer

- [ ] Which component owns the mutable state introduced here?
- [ ] Can two threads/processes modify it concurrently?
- [ ] What lock/transaction/unique constraint protects the invariant?
- [ ] What happens if an event/message is delivered twice?
- [ ] What happens if the process crashes after the first durable write but before the second?
- [ ] What happens when cancellation races completion?
- [ ] What happens when shutdown starts while another thread is waiting?
- [ ] What happens when a worker is slow rather than dead?
- [ ] Does this change browser-session affinity or ownership?
- [ ] Can a late result overwrite a newer logical state?

## Negative / adversarial scenarios

- [ ] Exercise or explicitly mark N/A: invalid/empty identifiers.
- [ ] Exercise or explicitly mark N/A: duplicate request/event.
- [ ] Exercise or explicitly mark N/A: unknown enum or node type.
- [ ] Exercise or explicitly mark N/A: timeout at external boundary.
- [ ] Exercise or explicitly mark N/A: cancellation at an inconvenient moment.
- [ ] Exercise or explicitly mark N/A: process shutdown while work is pending.
- [ ] Exercise or explicitly mark N/A: stale data or stale attempt result.
- [ ] Exercise or explicitly mark N/A: tenant mismatch / unauthorized request when relevant.
- [ ] Exercise or explicitly mark N/A: resource exhaustion / queue full when relevant.
- [ ] Exercise or explicitly mark N/A: malformed serialized payload when relevant.

## Explicit no-go items for this milestone

- Do not weaken a test to certify the baseline.
- If the baseline is red, Phase 2 does not start.
- Do not invent performance numbers.
- Do not mark a future component as implemented.
- Do not silently change Phase-1 default behavior.
- Do not commit secrets or local credential values.

## Required validation

- [ ] ``npm test``
- [ ] ``npm run typecheck``
- [ ] ``npm run lint``
- [ ] ``npm run build` when possible`
- [ ] Run current Phase-1 `npm test` when production app code is touched.
- [ ] Run `npm run typecheck` when TypeScript is touched.
- [ ] Run `npm run lint` when TypeScript/React is touched.
- [ ] Run CMake/CTest when C++ is touched.
- [ ] Run production build at integration/high-risk milestones.

## Performance evidence rules for this milestone

- If this milestone is not explicitly a benchmark milestone, do not add a resume number just because a timer was printed.
- If timing is collected for a test, label it diagnostic unless benchmark methodology requirements are satisfied.
- If this milestone changes a previously benchmarked hot path, note that old results are no longer final for the new SHA.
- Store benchmark raw data only in the defined benchmark result structure, not ad-hoc README prose.
- A result must identify workload, build mode, hardware, sample count, and commit before it becomes evidence-grade.

## Human intervention triggers

- If A required secret/account is missing. -> report `STATUS: BLOCKED` with exact manual action; do not guess.
- If A system-level prerequisite such as Docker/CMake/compiler requires user installation. -> report `STATUS: BLOCKED` with exact manual action; do not guess.
- If A shared/remote database migration would be applied. -> report `STATUS: BLOCKED` with exact manual action; do not guess.
- If Git author configuration is missing. -> report `STATUS: BLOCKED` with exact manual action; do not guess.
- If A destructive operation would touch user data or history. -> report `STATUS: BLOCKED` with exact manual action; do not guess.
- If A paid external test would incur nontrivial cost and the user has not already authorized it. -> report `STATUS: BLOCKED` with exact manual action; do not guess.

## Diff-review checklist before commit

- [ ] Review every changed file with `git diff`.
- [ ] Run `git diff --check`.
- [ ] Remove accidental formatting churn outside the milestone scope.
- [ ] Confirm no secret or `.env.local` value appears in the diff.
- [ ] Confirm no generated build tree, node_modules, vcpkg cache, Docker volume, or large transient trace has been staged.
- [ ] Confirm comments/README wording describe implemented behavior, not planned behavior.

## Commit requirement

Create one local commit with subject:

`phase2(m02): certify immutable phase1 baseline`

After commit:

- Record the new SHA in `docs/phase2/PROGRESS.md`.
- Record commands/results for the milestone.
- Do not push unless push permission/policy has already been explicitly authorized.
- If all exit criteria are green, proceed automatically to the next milestone.

## Exit criteria

- [ ] All milestone-specific implementation items are complete.
- [ ] All relevant new tests pass.
- [ ] Required Phase-1 regression gates pass.
- [ ] No unresolved race/data-corruption/security issue is knowingly introduced.
- [ ] No secret or accidental generated artifact is staged.
- [ ] Documentation describes current behavior accurately.
- [ ] The milestone has exactly one intended local commit or a clearly documented reason for a split required by repository tooling.

## Mandatory milestone report format

```text
STATUS: DONE or BLOCKED
MILESTONE: exact milestone number and title
BASE_SHA: SHA before milestone
END_SHA: SHA after commit when DONE
WHAT_I_INSPECTED: files/docs/commands read before changes
WHAT_CHANGED: concise implementation summary
FILES_CHANGED: exact paths
TESTS: command -> result
PHASE1_REGRESSION: command -> result or N/A with reason
CPP_VALIDATION: command -> result or N/A
DISTRIBUTED_VALIDATION: command -> result or N/A
METRICS_CAPTURED: actual measured metrics only; otherwise `None`
HUMAN_ACTION: None or exact required action
KNOWN_LIMITATIONS: explicit remaining limitations
COMMIT: commit SHA + subject
NEXT: next milestone number; continue automatically if not blocked
```

---

# MILESTONE 03 — Create the Phase-2 architecture, invariants, and progress scaffold

## Objective

Make architecture decisions reviewable before implementation.

## Why this milestone exists

- This milestone isolates **create the phase-2 architecture, invariants, and progress scaffold** so failures are attributable and reviewable.
- The milestone is intentionally narrower than the final Phase-2 architecture.
- Do not implement later milestone responsibilities early unless a tiny interface stub is required for compilation.
- Any stub must be clearly marked as non-production and must not be described as a completed feature.

## Mandatory pre-read and repository inspection

- Read `AGENTS.md` and obey repository-local instructions relevant to files you will touch.
- Run `git status --short` and confirm no unrelated user work will be overwritten.
- Read every existing source file that the milestone will modify before editing it.
- Read the nearest tests covering the behavior before changing implementation.
- Inspect current package/library versions rather than assuming APIs from memory.
- Consult current official documentation before using a third-party API that may have changed.

## Entry conditions

- Milestone 02 is DONE, unless this is Milestone 01.
- Working tree contains no unexpected unrelated modifications.
- The current Phase-1 baseline gate is known and available.
- Any dependency introduced in this milestone has a verified version/source and license acceptable for the repository.

## Detailed implementation procedure

1. Create `docs/phase2/ARCHITECTURE.md`.
2. Create `docs/phase2/FAILURE_MODEL.md`.
3. Create `docs/phase2/PROGRESS.md`.
4. Create `docs/phase2/BENCHMARK_METHODOLOGY.md` with the no-fabrication rules from this prompt.
5. Document the legacy-vs-Evo dual-engine strategy.
6. Document control-plane, C++ scheduler, Redis, Postgres, and TypeScript worker responsibilities.
7. Document browser session affinity and why same-session browser nodes cannot blindly run in parallel.
8. Document expected run/node/attempt states and failure assumptions.
9. Before editing, write a short local plan listing the exact files expected to change.
10. Prefer the smallest interface that satisfies this milestone and leaves room for later milestones.
11. Keep names consistent with existing repository conventions; do not introduce a parallel naming universe.
12. If an existing helper already implements part of the behavior, reuse it rather than creating a near-duplicate.
13. Keep product-facing behavior backwards compatible unless the milestone explicitly introduces a new opt-in Evo path.
14. When behavior crosses a trust boundary, validate input before mutating durable state.
15. When behavior crosses a thread/process boundary, define ownership, lifetime, timeout, and failure semantics explicitly.
16. When behavior emits a timestamp, state whether it is wall-clock or steady-clock and use it consistently.
17. Add tests in the same milestone as the behavior; do not defer all testing to Milestone 39.
18. Update `docs/phase2/PROGRESS.md` with the observed result, not a future-tense promise.

## Phase-1 preservation checklist

- [ ] AI planner still requires explicit user Run before execution.
- [ ] Generated graph remains editable React Flow / Liveblocks state.
- [ ] Legacy Trigger.dev engine remains available unless a later explicit final decision says otherwise.
- [ ] Clerk auth/org enforcement remains server-side.
- [ ] Agent Pro-plan gate remains fail-closed.
- [ ] Live browser credentials remain server/worker only.
- [ ] Legacy Run/Stop/results/replay/rerun behavior must not regress.
- [ ] Existing node registry/executors are not duplicated without a compelling reason.
- [ ] No test is removed merely because Phase 2 changes an internal representation.

## Concurrency / distributed correctness questions to answer

- [ ] Which component owns the mutable state introduced here?
- [ ] Can two threads/processes modify it concurrently?
- [ ] What lock/transaction/unique constraint protects the invariant?
- [ ] What happens if an event/message is delivered twice?
- [ ] What happens if the process crashes after the first durable write but before the second?
- [ ] What happens when cancellation races completion?
- [ ] What happens when shutdown starts while another thread is waiting?
- [ ] What happens when a worker is slow rather than dead?
- [ ] Does this change browser-session affinity or ownership?
- [ ] Can a late result overwrite a newer logical state?

## Negative / adversarial scenarios

- [ ] Exercise or explicitly mark N/A: invalid/empty identifiers.
- [ ] Exercise or explicitly mark N/A: duplicate request/event.
- [ ] Exercise or explicitly mark N/A: unknown enum or node type.
- [ ] Exercise or explicitly mark N/A: timeout at external boundary.
- [ ] Exercise or explicitly mark N/A: cancellation at an inconvenient moment.
- [ ] Exercise or explicitly mark N/A: process shutdown while work is pending.
- [ ] Exercise or explicitly mark N/A: stale data or stale attempt result.
- [ ] Exercise or explicitly mark N/A: tenant mismatch / unauthorized request when relevant.
- [ ] Exercise or explicitly mark N/A: resource exhaustion / queue full when relevant.
- [ ] Exercise or explicitly mark N/A: malformed serialized payload when relevant.

## Explicit no-go items for this milestone

- No production behavior change.
- Architecture must explicitly preserve the legacy executor.
- Do not invent performance numbers.
- Do not mark a future component as implemented.
- Do not silently change Phase-1 default behavior.
- Do not commit secrets or local credential values.

## Required validation

- [ ] `docs review`
- [ ] `Phase-1 regression not required for docs-only change unless repository policy requires it`
- [ ] Run current Phase-1 `npm test` when production app code is touched.
- [ ] Run `npm run typecheck` when TypeScript is touched.
- [ ] Run `npm run lint` when TypeScript/React is touched.
- [ ] Run CMake/CTest when C++ is touched.
- [ ] Run production build at integration/high-risk milestones.

## Performance evidence rules for this milestone

- If this milestone is not explicitly a benchmark milestone, do not add a resume number just because a timer was printed.
- If timing is collected for a test, label it diagnostic unless benchmark methodology requirements are satisfied.
- If this milestone changes a previously benchmarked hot path, note that old results are no longer final for the new SHA.
- Store benchmark raw data only in the defined benchmark result structure, not ad-hoc README prose.
- A result must identify workload, build mode, hardware, sample count, and commit before it becomes evidence-grade.

## Human intervention triggers

- If A required secret/account is missing. -> report `STATUS: BLOCKED` with exact manual action; do not guess.
- If A system-level prerequisite such as Docker/CMake/compiler requires user installation. -> report `STATUS: BLOCKED` with exact manual action; do not guess.
- If A shared/remote database migration would be applied. -> report `STATUS: BLOCKED` with exact manual action; do not guess.
- If Git author configuration is missing. -> report `STATUS: BLOCKED` with exact manual action; do not guess.
- If A destructive operation would touch user data or history. -> report `STATUS: BLOCKED` with exact manual action; do not guess.
- If A paid external test would incur nontrivial cost and the user has not already authorized it. -> report `STATUS: BLOCKED` with exact manual action; do not guess.

## Diff-review checklist before commit

- [ ] Review every changed file with `git diff`.
- [ ] Run `git diff --check`.
- [ ] Remove accidental formatting churn outside the milestone scope.
- [ ] Confirm no secret or `.env.local` value appears in the diff.
- [ ] Confirm no generated build tree, node_modules, vcpkg cache, Docker volume, or large transient trace has been staged.
- [ ] Confirm comments/README wording describe implemented behavior, not planned behavior.

## Commit requirement

Create one local commit with subject:

`phase2(m03): define phase2 architecture and failure model`

After commit:

- Record the new SHA in `docs/phase2/PROGRESS.md`.
- Record commands/results for the milestone.
- Do not push unless push permission/policy has already been explicitly authorized.
- If all exit criteria are green, proceed automatically to the next milestone.

## Exit criteria

- [ ] All milestone-specific implementation items are complete.
- [ ] All relevant new tests pass.
- [ ] Required Phase-1 regression gates pass.
- [ ] No unresolved race/data-corruption/security issue is knowingly introduced.
- [ ] No secret or accidental generated artifact is staged.
- [ ] Documentation describes current behavior accurately.
- [ ] The milestone has exactly one intended local commit or a clearly documented reason for a split required by repository tooling.

## Mandatory milestone report format

```text
STATUS: DONE or BLOCKED
MILESTONE: exact milestone number and title
BASE_SHA: SHA before milestone
END_SHA: SHA after commit when DONE
WHAT_I_INSPECTED: files/docs/commands read before changes
WHAT_CHANGED: concise implementation summary
FILES_CHANGED: exact paths
TESTS: command -> result
PHASE1_REGRESSION: command -> result or N/A with reason
CPP_VALIDATION: command -> result or N/A
DISTRIBUTED_VALIDATION: command -> result or N/A
METRICS_CAPTURED: actual measured metrics only; otherwise `None`
HUMAN_ACTION: None or exact required action
KNOWN_LIMITATIONS: explicit remaining limitations
COMMIT: commit SHA + subject
NEXT: next milestone number; continue automatically if not blocked
```

---

# MILESTONE 04 — Bootstrap the reproducible C++20 toolchain

## Objective

Create a minimal portable C++ build without touching the application execution path.

## Why this milestone exists

- This milestone isolates **bootstrap the reproducible c++20 toolchain** so failures are attributable and reviewable.
- The milestone is intentionally narrower than the final Phase-2 architecture.
- Do not implement later milestone responsibilities early unless a tiny interface stub is required for compilation.
- Any stub must be clearly marked as non-production and must not be described as a completed feature.

## Mandatory pre-read and repository inspection

- Read `AGENTS.md` and obey repository-local instructions relevant to files you will touch.
- Run `git status --short` and confirm no unrelated user work will be overwritten.
- Read every existing source file that the milestone will modify before editing it.
- Read the nearest tests covering the behavior before changing implementation.
- Inspect current package/library versions rather than assuming APIs from memory.
- Consult current official documentation before using a third-party API that may have changed.

## Entry conditions

- Milestone 03 is DONE, unless this is Milestone 01.
- Working tree contains no unexpected unrelated modifications.
- The current Phase-1 baseline gate is known and available.
- Any dependency introduced in this milestone has a verified version/source and license acceptable for the repository.

## Detailed implementation procedure

1. Detect the host OS, architecture, C++ compiler, CMake, Docker, and available package-management tooling.
2. Prefer a repository-local reproducible dependency strategy such as vcpkg manifest mode; do not vendor a giant dependency tree into Git.
3. Pin dependency/tool bootstrap inputs where practical.
4. Create `engine/CMakeLists.txt` and a minimal `evo_scheduler_core` target.
5. Create a minimal executable or smoke target that prints version/build metadata only.
6. Create CTest integration and one trivial test to prove the toolchain.
7. Add build directories/tool caches to `.gitignore` without disturbing existing ignores.
8. Document exact local setup commands in `docs/phase2/BUILDING_ENGINE.md`.
9. Before editing, write a short local plan listing the exact files expected to change.
10. Prefer the smallest interface that satisfies this milestone and leaves room for later milestones.
11. Keep names consistent with existing repository conventions; do not introduce a parallel naming universe.
12. If an existing helper already implements part of the behavior, reuse it rather than creating a near-duplicate.
13. Keep product-facing behavior backwards compatible unless the milestone explicitly introduces a new opt-in Evo path.
14. When behavior crosses a trust boundary, validate input before mutating durable state.
15. When behavior crosses a thread/process boundary, define ownership, lifetime, timeout, and failure semantics explicitly.
16. When behavior emits a timestamp, state whether it is wall-clock or steady-clock and use it consistently.
17. Add tests in the same milestone as the behavior; do not defer all testing to Milestone 39.
18. Update `docs/phase2/PROGRESS.md` with the observed result, not a future-tense promise.

## Phase-1 preservation checklist

- [ ] AI planner still requires explicit user Run before execution.
- [ ] Generated graph remains editable React Flow / Liveblocks state.
- [ ] Legacy Trigger.dev engine remains available unless a later explicit final decision says otherwise.
- [ ] Clerk auth/org enforcement remains server-side.
- [ ] Agent Pro-plan gate remains fail-closed.
- [ ] Live browser credentials remain server/worker only.
- [ ] Legacy Run/Stop/results/replay/rerun behavior must not regress.
- [ ] Existing node registry/executors are not duplicated without a compelling reason.
- [ ] No test is removed merely because Phase 2 changes an internal representation.

## Concurrency / distributed correctness questions to answer

- [ ] Which component owns the mutable state introduced here?
- [ ] Can two threads/processes modify it concurrently?
- [ ] What lock/transaction/unique constraint protects the invariant?
- [ ] What happens if an event/message is delivered twice?
- [ ] What happens if the process crashes after the first durable write but before the second?
- [ ] What happens when cancellation races completion?
- [ ] What happens when shutdown starts while another thread is waiting?
- [ ] What happens when a worker is slow rather than dead?
- [ ] Does this change browser-session affinity or ownership?
- [ ] Can a late result overwrite a newer logical state?

## Negative / adversarial scenarios

- [ ] Exercise or explicitly mark N/A: invalid/empty identifiers.
- [ ] Exercise or explicitly mark N/A: duplicate request/event.
- [ ] Exercise or explicitly mark N/A: unknown enum or node type.
- [ ] Exercise or explicitly mark N/A: timeout at external boundary.
- [ ] Exercise or explicitly mark N/A: cancellation at an inconvenient moment.
- [ ] Exercise or explicitly mark N/A: process shutdown while work is pending.
- [ ] Exercise or explicitly mark N/A: stale data or stale attempt result.
- [ ] Exercise or explicitly mark N/A: tenant mismatch / unauthorized request when relevant.
- [ ] Exercise or explicitly mark N/A: resource exhaustion / queue full when relevant.
- [ ] Exercise or explicitly mark N/A: malformed serialized payload when relevant.

## Explicit no-go items for this milestone

- Do not add gRPC/Redis/Postgres dependencies yet unless needed for toolchain proof.
- Do not require a global `sudo` install silently.
- Do not invent performance numbers.
- Do not mark a future component as implemented.
- Do not silently change Phase-1 default behavior.
- Do not commit secrets or local credential values.

## Required validation

- [ ] `CMake configure`
- [ ] `CMake Release build`
- [ ] ``ctest --output-on-failure``
- [ ] `Phase-1 npm regression`
- [ ] Run current Phase-1 `npm test` when production app code is touched.
- [ ] Run `npm run typecheck` when TypeScript is touched.
- [ ] Run `npm run lint` when TypeScript/React is touched.
- [ ] Run CMake/CTest when C++ is touched.
- [ ] Run production build at integration/high-risk milestones.

## Performance evidence rules for this milestone

- If this milestone is not explicitly a benchmark milestone, do not add a resume number just because a timer was printed.
- If timing is collected for a test, label it diagnostic unless benchmark methodology requirements are satisfied.
- If this milestone changes a previously benchmarked hot path, note that old results are no longer final for the new SHA.
- Store benchmark raw data only in the defined benchmark result structure, not ad-hoc README prose.
- A result must identify workload, build mode, hardware, sample count, and commit before it becomes evidence-grade.

## Human intervention triggers

- If A required secret/account is missing. -> report `STATUS: BLOCKED` with exact manual action; do not guess.
- If A system-level prerequisite such as Docker/CMake/compiler requires user installation. -> report `STATUS: BLOCKED` with exact manual action; do not guess.
- If A shared/remote database migration would be applied. -> report `STATUS: BLOCKED` with exact manual action; do not guess.
- If Git author configuration is missing. -> report `STATUS: BLOCKED` with exact manual action; do not guess.
- If A destructive operation would touch user data or history. -> report `STATUS: BLOCKED` with exact manual action; do not guess.
- If A paid external test would incur nontrivial cost and the user has not already authorized it. -> report `STATUS: BLOCKED` with exact manual action; do not guess.

## Diff-review checklist before commit

- [ ] Review every changed file with `git diff`.
- [ ] Run `git diff --check`.
- [ ] Remove accidental formatting churn outside the milestone scope.
- [ ] Confirm no secret or `.env.local` value appears in the diff.
- [ ] Confirm no generated build tree, node_modules, vcpkg cache, Docker volume, or large transient trace has been staged.
- [ ] Confirm comments/README wording describe implemented behavior, not planned behavior.

## Commit requirement

Create one local commit with subject:

`phase2(m04): bootstrap c++20 engine toolchain`

After commit:

- Record the new SHA in `docs/phase2/PROGRESS.md`.
- Record commands/results for the milestone.
- Do not push unless push permission/policy has already been explicitly authorized.
- If all exit criteria are green, proceed automatically to the next milestone.

## Exit criteria

- [ ] All milestone-specific implementation items are complete.
- [ ] All relevant new tests pass.
- [ ] Required Phase-1 regression gates pass.
- [ ] No unresolved race/data-corruption/security issue is knowingly introduced.
- [ ] No secret or accidental generated artifact is staged.
- [ ] Documentation describes current behavior accurately.
- [ ] The milestone has exactly one intended local commit or a clearly documented reason for a split required by repository tooling.

## Mandatory milestone report format

```text
STATUS: DONE or BLOCKED
MILESTONE: exact milestone number and title
BASE_SHA: SHA before milestone
END_SHA: SHA after commit when DONE
WHAT_I_INSPECTED: files/docs/commands read before changes
WHAT_CHANGED: concise implementation summary
FILES_CHANGED: exact paths
TESTS: command -> result
PHASE1_REGRESSION: command -> result or N/A with reason
CPP_VALIDATION: command -> result or N/A
DISTRIBUTED_VALIDATION: command -> result or N/A
METRICS_CAPTURED: actual measured metrics only; otherwise `None`
HUMAN_ACTION: None or exact required action
KNOWN_LIMITATIONS: explicit remaining limitations
COMMIT: commit SHA + subject
NEXT: next milestone number; continue automatically if not blocked
```

---

# MILESTONE 05 — Implement the canonical C++ DAG model

## Objective

Represent workflow topology safely and independently of React Flow UI details.

## Why this milestone exists

- This milestone isolates **implement the canonical c++ dag model** so failures are attributable and reviewable.
- The milestone is intentionally narrower than the final Phase-2 architecture.
- Do not implement later milestone responsibilities early unless a tiny interface stub is required for compilation.
- Any stub must be clearly marked as non-production and must not be described as a completed feature.

## Mandatory pre-read and repository inspection

- Read `AGENTS.md` and obey repository-local instructions relevant to files you will touch.
- Run `git status --short` and confirm no unrelated user work will be overwritten.
- Read every existing source file that the milestone will modify before editing it.
- Read the nearest tests covering the behavior before changing implementation.
- Inspect current package/library versions rather than assuming APIs from memory.
- Consult current official documentation before using a third-party API that may have changed.

## Entry conditions

- Milestone 04 is DONE, unless this is Milestone 01.
- Working tree contains no unexpected unrelated modifications.
- The current Phase-1 baseline gate is known and available.
- Any dependency introduced in this milestone has a verified version/source and license acceptable for the repository.

## Detailed implementation procedure

1. Create strongly typed node IDs, edges, node metadata, predecessors, successors, and graph validation.
2. Keep the core graph model free of Browserbase/Next.js/Redis dependencies.
3. Implement duplicate-node detection, missing-edge-endpoint detection, cycle detection, and connected/reachable semantics matching the intended scheduler contract.
4. Define deterministic graph serialization/deserialization for test/benchmark input.
5. Use a minimal canonical JSON shape rather than copying the entire React Flow node object into the scheduler core.
6. Add unit tests for linear, diamond, wide fan-out/fan-in, disconnected, duplicate ID, missing endpoint, and cyclic graphs.
7. Document differences between Phase-1 `validateGraph` UI semantics and the engine's internal graph validation.
8. Before editing, write a short local plan listing the exact files expected to change.
9. Prefer the smallest interface that satisfies this milestone and leaves room for later milestones.
10. Keep names consistent with existing repository conventions; do not introduce a parallel naming universe.
11. If an existing helper already implements part of the behavior, reuse it rather than creating a near-duplicate.
12. Keep product-facing behavior backwards compatible unless the milestone explicitly introduces a new opt-in Evo path.
13. When behavior crosses a trust boundary, validate input before mutating durable state.
14. When behavior crosses a thread/process boundary, define ownership, lifetime, timeout, and failure semantics explicitly.
15. When behavior emits a timestamp, state whether it is wall-clock or steady-clock and use it consistently.
16. Add tests in the same milestone as the behavior; do not defer all testing to Milestone 39.
17. Update `docs/phase2/PROGRESS.md` with the observed result, not a future-tense promise.

## Phase-1 preservation checklist

- [ ] AI planner still requires explicit user Run before execution.
- [ ] Generated graph remains editable React Flow / Liveblocks state.
- [ ] Legacy Trigger.dev engine remains available unless a later explicit final decision says otherwise.
- [ ] Clerk auth/org enforcement remains server-side.
- [ ] Agent Pro-plan gate remains fail-closed.
- [ ] Live browser credentials remain server/worker only.
- [ ] Legacy Run/Stop/results/replay/rerun behavior must not regress.
- [ ] Existing node registry/executors are not duplicated without a compelling reason.
- [ ] No test is removed merely because Phase 2 changes an internal representation.

## Concurrency / distributed correctness questions to answer

- [ ] Which component owns the mutable state introduced here?
- [ ] Can two threads/processes modify it concurrently?
- [ ] What lock/transaction/unique constraint protects the invariant?
- [ ] What happens if an event/message is delivered twice?
- [ ] What happens if the process crashes after the first durable write but before the second?
- [ ] What happens when cancellation races completion?
- [ ] What happens when shutdown starts while another thread is waiting?
- [ ] What happens when a worker is slow rather than dead?
- [ ] Does this change browser-session affinity or ownership?
- [ ] Can a late result overwrite a newer logical state?

## Negative / adversarial scenarios

- [ ] Exercise or explicitly mark N/A: invalid/empty identifiers.
- [ ] Exercise or explicitly mark N/A: duplicate request/event.
- [ ] Exercise or explicitly mark N/A: unknown enum or node type.
- [ ] Exercise or explicitly mark N/A: timeout at external boundary.
- [ ] Exercise or explicitly mark N/A: cancellation at an inconvenient moment.
- [ ] Exercise or explicitly mark N/A: process shutdown while work is pending.
- [ ] Exercise or explicitly mark N/A: stale data or stale attempt result.
- [ ] Exercise or explicitly mark N/A: tenant mismatch / unauthorized request when relevant.
- [ ] Exercise or explicitly mark N/A: resource exhaustion / queue full when relevant.
- [ ] Exercise or explicitly mark N/A: malformed serialized payload when relevant.

## Explicit no-go items for this milestone

- Do not change the TypeScript graph yet.
- Do not implement concurrency yet.
- Do not invent performance numbers.
- Do not mark a future component as implemented.
- Do not silently change Phase-1 default behavior.
- Do not commit secrets or local credential values.

## Required validation

- [ ] `engine unit tests`
- [ ] `sanity build`
- [ ] `Phase-1 npm regression`
- [ ] Run current Phase-1 `npm test` when production app code is touched.
- [ ] Run `npm run typecheck` when TypeScript is touched.
- [ ] Run `npm run lint` when TypeScript/React is touched.
- [ ] Run CMake/CTest when C++ is touched.
- [ ] Run production build at integration/high-risk milestones.

## Performance evidence rules for this milestone

- If this milestone is not explicitly a benchmark milestone, do not add a resume number just because a timer was printed.
- If timing is collected for a test, label it diagnostic unless benchmark methodology requirements are satisfied.
- If this milestone changes a previously benchmarked hot path, note that old results are no longer final for the new SHA.
- Store benchmark raw data only in the defined benchmark result structure, not ad-hoc README prose.
- A result must identify workload, build mode, hardware, sample count, and commit before it becomes evidence-grade.

## Human intervention triggers

- If A required secret/account is missing. -> report `STATUS: BLOCKED` with exact manual action; do not guess.
- If A system-level prerequisite such as Docker/CMake/compiler requires user installation. -> report `STATUS: BLOCKED` with exact manual action; do not guess.
- If A shared/remote database migration would be applied. -> report `STATUS: BLOCKED` with exact manual action; do not guess.
- If Git author configuration is missing. -> report `STATUS: BLOCKED` with exact manual action; do not guess.
- If A destructive operation would touch user data or history. -> report `STATUS: BLOCKED` with exact manual action; do not guess.
- If A paid external test would incur nontrivial cost and the user has not already authorized it. -> report `STATUS: BLOCKED` with exact manual action; do not guess.

## Diff-review checklist before commit

- [ ] Review every changed file with `git diff`.
- [ ] Run `git diff --check`.
- [ ] Remove accidental formatting churn outside the milestone scope.
- [ ] Confirm no secret or `.env.local` value appears in the diff.
- [ ] Confirm no generated build tree, node_modules, vcpkg cache, Docker volume, or large transient trace has been staged.
- [ ] Confirm comments/README wording describe implemented behavior, not planned behavior.

## Commit requirement

Create one local commit with subject:

`phase2(m05): add canonical c++ dag model`

After commit:

- Record the new SHA in `docs/phase2/PROGRESS.md`.
- Record commands/results for the milestone.
- Do not push unless push permission/policy has already been explicitly authorized.
- If all exit criteria are green, proceed automatically to the next milestone.

## Exit criteria

- [ ] All milestone-specific implementation items are complete.
- [ ] All relevant new tests pass.
- [ ] Required Phase-1 regression gates pass.
- [ ] No unresolved race/data-corruption/security issue is knowingly introduced.
- [ ] No secret or accidental generated artifact is staged.
- [ ] Documentation describes current behavior accurately.
- [ ] The milestone has exactly one intended local commit or a clearly documented reason for a split required by repository tooling.

## Mandatory milestone report format

```text
STATUS: DONE or BLOCKED
MILESTONE: exact milestone number and title
BASE_SHA: SHA before milestone
END_SHA: SHA after commit when DONE
WHAT_I_INSPECTED: files/docs/commands read before changes
WHAT_CHANGED: concise implementation summary
FILES_CHANGED: exact paths
TESTS: command -> result
PHASE1_REGRESSION: command -> result or N/A with reason
CPP_VALIDATION: command -> result or N/A
DISTRIBUTED_VALIDATION: command -> result or N/A
METRICS_CAPTURED: actual measured metrics only; otherwise `None`
HUMAN_ACTION: None or exact required action
KNOWN_LIMITATIONS: explicit remaining limitations
COMMIT: commit SHA + subject
NEXT: next milestone number; continue automatically if not blocked
```

---

# MILESTONE 06 — Implement a deterministic sequential reference scheduler

## Objective

Create the correctness oracle and performance baseline for the C++ scheduler.

## Why this milestone exists

- This milestone isolates **implement a deterministic sequential reference scheduler** so failures are attributable and reviewable.
- The milestone is intentionally narrower than the final Phase-2 architecture.
- Do not implement later milestone responsibilities early unless a tiny interface stub is required for compilation.
- Any stub must be clearly marked as non-production and must not be described as a completed feature.

## Mandatory pre-read and repository inspection

- Read `AGENTS.md` and obey repository-local instructions relevant to files you will touch.
- Run `git status --short` and confirm no unrelated user work will be overwritten.
- Read every existing source file that the milestone will modify before editing it.
- Read the nearest tests covering the behavior before changing implementation.
- Inspect current package/library versions rather than assuming APIs from memory.
- Consult current official documentation before using a third-party API that may have changed.

## Entry conditions

- Milestone 05 is DONE, unless this is Milestone 01.
- Working tree contains no unexpected unrelated modifications.
- The current Phase-1 baseline gate is known and available.
- Any dependency introduced in this milestone has a verified version/source and license acceptable for the repository.

## Detailed implementation procedure

1. Implement a simple sequential executor over the canonical DAG.
2. Execute only when dependencies are satisfied.
3. Record deterministic node start/completion ordering for tests.
4. Add benchmark-only synthetic task definitions for controlled sleep/I-O-like and CPU-like work.
5. Do not expose synthetic task types to the product planner/node registry.
6. Implement deterministic seeded workload generation.
7. Use this sequential implementation as a reference, not as a replacement for Trigger.dev.
8. Add tests that compare expected ordering and outputs on representative DAGs.
9. Before editing, write a short local plan listing the exact files expected to change.
10. Prefer the smallest interface that satisfies this milestone and leaves room for later milestones.
11. Keep names consistent with existing repository conventions; do not introduce a parallel naming universe.
12. If an existing helper already implements part of the behavior, reuse it rather than creating a near-duplicate.
13. Keep product-facing behavior backwards compatible unless the milestone explicitly introduces a new opt-in Evo path.
14. When behavior crosses a trust boundary, validate input before mutating durable state.
15. When behavior crosses a thread/process boundary, define ownership, lifetime, timeout, and failure semantics explicitly.
16. When behavior emits a timestamp, state whether it is wall-clock or steady-clock and use it consistently.
17. Add tests in the same milestone as the behavior; do not defer all testing to Milestone 39.
18. Update `docs/phase2/PROGRESS.md` with the observed result, not a future-tense promise.

## Phase-1 preservation checklist

- [ ] AI planner still requires explicit user Run before execution.
- [ ] Generated graph remains editable React Flow / Liveblocks state.
- [ ] Legacy Trigger.dev engine remains available unless a later explicit final decision says otherwise.
- [ ] Clerk auth/org enforcement remains server-side.
- [ ] Agent Pro-plan gate remains fail-closed.
- [ ] Live browser credentials remain server/worker only.
- [ ] Legacy Run/Stop/results/replay/rerun behavior must not regress.
- [ ] Existing node registry/executors are not duplicated without a compelling reason.
- [ ] No test is removed merely because Phase 2 changes an internal representation.

## Concurrency / distributed correctness questions to answer

- [ ] Which component owns the mutable state introduced here?
- [ ] Can two threads/processes modify it concurrently?
- [ ] What lock/transaction/unique constraint protects the invariant?
- [ ] What happens if an event/message is delivered twice?
- [ ] What happens if the process crashes after the first durable write but before the second?
- [ ] What happens when cancellation races completion?
- [ ] What happens when shutdown starts while another thread is waiting?
- [ ] What happens when a worker is slow rather than dead?
- [ ] Does this change browser-session affinity or ownership?
- [ ] Can a late result overwrite a newer logical state?

## Negative / adversarial scenarios

- [ ] Exercise or explicitly mark N/A: invalid/empty identifiers.
- [ ] Exercise or explicitly mark N/A: duplicate request/event.
- [ ] Exercise or explicitly mark N/A: unknown enum or node type.
- [ ] Exercise or explicitly mark N/A: timeout at external boundary.
- [ ] Exercise or explicitly mark N/A: cancellation at an inconvenient moment.
- [ ] Exercise or explicitly mark N/A: process shutdown while work is pending.
- [ ] Exercise or explicitly mark N/A: stale data or stale attempt result.
- [ ] Exercise or explicitly mark N/A: tenant mismatch / unauthorized request when relevant.
- [ ] Exercise or explicitly mark N/A: resource exhaustion / queue full when relevant.
- [ ] Exercise or explicitly mark N/A: malformed serialized payload when relevant.

## Explicit no-go items for this milestone

- No production integration.
- Do not call this the Phase-1 runtime in documentation; call it the C++ sequential reference scheduler.
- Do not invent performance numbers.
- Do not mark a future component as implemented.
- Do not silently change Phase-1 default behavior.
- Do not commit secrets or local credential values.

## Required validation

- [ ] `reference scheduler unit tests`
- [ ] `Release build smoke`
- [ ] Run current Phase-1 `npm test` when production app code is touched.
- [ ] Run `npm run typecheck` when TypeScript is touched.
- [ ] Run `npm run lint` when TypeScript/React is touched.
- [ ] Run CMake/CTest when C++ is touched.
- [ ] Run production build at integration/high-risk milestones.

## Performance evidence rules for this milestone

- If this milestone is not explicitly a benchmark milestone, do not add a resume number just because a timer was printed.
- If timing is collected for a test, label it diagnostic unless benchmark methodology requirements are satisfied.
- If this milestone changes a previously benchmarked hot path, note that old results are no longer final for the new SHA.
- Store benchmark raw data only in the defined benchmark result structure, not ad-hoc README prose.
- A result must identify workload, build mode, hardware, sample count, and commit before it becomes evidence-grade.

## Human intervention triggers

- If A required secret/account is missing. -> report `STATUS: BLOCKED` with exact manual action; do not guess.
- If A system-level prerequisite such as Docker/CMake/compiler requires user installation. -> report `STATUS: BLOCKED` with exact manual action; do not guess.
- If A shared/remote database migration would be applied. -> report `STATUS: BLOCKED` with exact manual action; do not guess.
- If Git author configuration is missing. -> report `STATUS: BLOCKED` with exact manual action; do not guess.
- If A destructive operation would touch user data or history. -> report `STATUS: BLOCKED` with exact manual action; do not guess.
- If A paid external test would incur nontrivial cost and the user has not already authorized it. -> report `STATUS: BLOCKED` with exact manual action; do not guess.

## Diff-review checklist before commit

- [ ] Review every changed file with `git diff`.
- [ ] Run `git diff --check`.
- [ ] Remove accidental formatting churn outside the milestone scope.
- [ ] Confirm no secret or `.env.local` value appears in the diff.
- [ ] Confirm no generated build tree, node_modules, vcpkg cache, Docker volume, or large transient trace has been staged.
- [ ] Confirm comments/README wording describe implemented behavior, not planned behavior.

## Commit requirement

Create one local commit with subject:

`phase2(m06): add sequential scheduler reference`

After commit:

- Record the new SHA in `docs/phase2/PROGRESS.md`.
- Record commands/results for the milestone.
- Do not push unless push permission/policy has already been explicitly authorized.
- If all exit criteria are green, proceed automatically to the next milestone.

## Exit criteria

- [ ] All milestone-specific implementation items are complete.
- [ ] All relevant new tests pass.
- [ ] Required Phase-1 regression gates pass.
- [ ] No unresolved race/data-corruption/security issue is knowingly introduced.
- [ ] No secret or accidental generated artifact is staged.
- [ ] Documentation describes current behavior accurately.
- [ ] The milestone has exactly one intended local commit or a clearly documented reason for a split required by repository tooling.

## Mandatory milestone report format

```text
STATUS: DONE or BLOCKED
MILESTONE: exact milestone number and title
BASE_SHA: SHA before milestone
END_SHA: SHA after commit when DONE
WHAT_I_INSPECTED: files/docs/commands read before changes
WHAT_CHANGED: concise implementation summary
FILES_CHANGED: exact paths
TESTS: command -> result
PHASE1_REGRESSION: command -> result or N/A with reason
CPP_VALIDATION: command -> result or N/A
DISTRIBUTED_VALIDATION: command -> result or N/A
METRICS_CAPTURED: actual measured metrics only; otherwise `None`
HUMAN_ACTION: None or exact required action
KNOWN_LIMITATIONS: explicit remaining limitations
COMMIT: commit SHA + subject
NEXT: next milestone number; continue automatically if not blocked
```

---

# MILESTONE 07 — Implement scheduler state machines and dependency counters

## Objective

Separate logical scheduling state from execution threads before parallelism.

## Why this milestone exists

- This milestone isolates **implement scheduler state machines and dependency counters** so failures are attributable and reviewable.
- The milestone is intentionally narrower than the final Phase-2 architecture.
- Do not implement later milestone responsibilities early unless a tiny interface stub is required for compilation.
- Any stub must be clearly marked as non-production and must not be described as a completed feature.

## Mandatory pre-read and repository inspection

- Read `AGENTS.md` and obey repository-local instructions relevant to files you will touch.
- Run `git status --short` and confirm no unrelated user work will be overwritten.
- Read every existing source file that the milestone will modify before editing it.
- Read the nearest tests covering the behavior before changing implementation.
- Inspect current package/library versions rather than assuming APIs from memory.
- Consult current official documentation before using a third-party API that may have changed.

## Entry conditions

- Milestone 06 is DONE, unless this is Milestone 01.
- Working tree contains no unexpected unrelated modifications.
- The current Phase-1 baseline gate is known and available.
- Any dependency introduced in this milestone has a verified version/source and license acceptable for the repository.

## Detailed implementation procedure

1. Define explicit run/node states and legal transitions.
2. Initialize each node's remaining dependency count from predecessors.
3. Mark zero-dependency/root work ready according to start-node semantics.
4. On one logical predecessor completion, decrement each successor exactly once.
5. Make duplicate completion events idempotent at the scheduler state layer.
6. Ensure fan-in nodes become ready only after all logical predecessors succeed.
7. Propagate predecessor failure according to an explicit policy rather than silently running downstream work.
8. Add state-transition invariant tests and duplicate-event tests.
9. Before editing, write a short local plan listing the exact files expected to change.
10. Prefer the smallest interface that satisfies this milestone and leaves room for later milestones.
11. Keep names consistent with existing repository conventions; do not introduce a parallel naming universe.
12. If an existing helper already implements part of the behavior, reuse it rather than creating a near-duplicate.
13. Keep product-facing behavior backwards compatible unless the milestone explicitly introduces a new opt-in Evo path.
14. When behavior crosses a trust boundary, validate input before mutating durable state.
15. When behavior crosses a thread/process boundary, define ownership, lifetime, timeout, and failure semantics explicitly.
16. When behavior emits a timestamp, state whether it is wall-clock or steady-clock and use it consistently.
17. Add tests in the same milestone as the behavior; do not defer all testing to Milestone 39.
18. Update `docs/phase2/PROGRESS.md` with the observed result, not a future-tense promise.

## Phase-1 preservation checklist

- [ ] AI planner still requires explicit user Run before execution.
- [ ] Generated graph remains editable React Flow / Liveblocks state.
- [ ] Legacy Trigger.dev engine remains available unless a later explicit final decision says otherwise.
- [ ] Clerk auth/org enforcement remains server-side.
- [ ] Agent Pro-plan gate remains fail-closed.
- [ ] Live browser credentials remain server/worker only.
- [ ] Legacy Run/Stop/results/replay/rerun behavior must not regress.
- [ ] Existing node registry/executors are not duplicated without a compelling reason.
- [ ] No test is removed merely because Phase 2 changes an internal representation.

## Concurrency / distributed correctness questions to answer

- [ ] Which component owns the mutable state introduced here?
- [ ] Can two threads/processes modify it concurrently?
- [ ] What lock/transaction/unique constraint protects the invariant?
- [ ] What happens if an event/message is delivered twice?
- [ ] What happens if the process crashes after the first durable write but before the second?
- [ ] What happens when cancellation races completion?
- [ ] What happens when shutdown starts while another thread is waiting?
- [ ] What happens when a worker is slow rather than dead?
- [ ] Does this change browser-session affinity or ownership?
- [ ] Can a late result overwrite a newer logical state?

## Negative / adversarial scenarios

- [ ] Exercise or explicitly mark N/A: invalid/empty identifiers.
- [ ] Exercise or explicitly mark N/A: duplicate request/event.
- [ ] Exercise or explicitly mark N/A: unknown enum or node type.
- [ ] Exercise or explicitly mark N/A: timeout at external boundary.
- [ ] Exercise or explicitly mark N/A: cancellation at an inconvenient moment.
- [ ] Exercise or explicitly mark N/A: process shutdown while work is pending.
- [ ] Exercise or explicitly mark N/A: stale data or stale attempt result.
- [ ] Exercise or explicitly mark N/A: tenant mismatch / unauthorized request when relevant.
- [ ] Exercise or explicitly mark N/A: resource exhaustion / queue full when relevant.
- [ ] Exercise or explicitly mark N/A: malformed serialized payload when relevant.

## Explicit no-go items for this milestone

- Do not use atomics just because counters exist; first decide which thread owns the state.
- Illegal transitions must be detected in tests/debug assertions.
- Do not invent performance numbers.
- Do not mark a future component as implemented.
- Do not silently change Phase-1 default behavior.
- Do not commit secrets or local credential values.

## Required validation

- [ ] `state-machine tests`
- [ ] `fan-in/fan-out tests`
- [ ] `duplicate completion tests`
- [ ] Run current Phase-1 `npm test` when production app code is touched.
- [ ] Run `npm run typecheck` when TypeScript is touched.
- [ ] Run `npm run lint` when TypeScript/React is touched.
- [ ] Run CMake/CTest when C++ is touched.
- [ ] Run production build at integration/high-risk milestones.

## Performance evidence rules for this milestone

- If this milestone is not explicitly a benchmark milestone, do not add a resume number just because a timer was printed.
- If timing is collected for a test, label it diagnostic unless benchmark methodology requirements are satisfied.
- If this milestone changes a previously benchmarked hot path, note that old results are no longer final for the new SHA.
- Store benchmark raw data only in the defined benchmark result structure, not ad-hoc README prose.
- A result must identify workload, build mode, hardware, sample count, and commit before it becomes evidence-grade.

## Human intervention triggers

- If A required secret/account is missing. -> report `STATUS: BLOCKED` with exact manual action; do not guess.
- If A system-level prerequisite such as Docker/CMake/compiler requires user installation. -> report `STATUS: BLOCKED` with exact manual action; do not guess.
- If A shared/remote database migration would be applied. -> report `STATUS: BLOCKED` with exact manual action; do not guess.
- If Git author configuration is missing. -> report `STATUS: BLOCKED` with exact manual action; do not guess.
- If A destructive operation would touch user data or history. -> report `STATUS: BLOCKED` with exact manual action; do not guess.
- If A paid external test would incur nontrivial cost and the user has not already authorized it. -> report `STATUS: BLOCKED` with exact manual action; do not guess.

## Diff-review checklist before commit

- [ ] Review every changed file with `git diff`.
- [ ] Run `git diff --check`.
- [ ] Remove accidental formatting churn outside the milestone scope.
- [ ] Confirm no secret or `.env.local` value appears in the diff.
- [ ] Confirm no generated build tree, node_modules, vcpkg cache, Docker volume, or large transient trace has been staged.
- [ ] Confirm comments/README wording describe implemented behavior, not planned behavior.

## Commit requirement

Create one local commit with subject:

`phase2(m07): add scheduler state machine and dependency accounting`

After commit:

- Record the new SHA in `docs/phase2/PROGRESS.md`.
- Record commands/results for the milestone.
- Do not push unless push permission/policy has already been explicitly authorized.
- If all exit criteria are green, proceed automatically to the next milestone.

## Exit criteria

- [ ] All milestone-specific implementation items are complete.
- [ ] All relevant new tests pass.
- [ ] Required Phase-1 regression gates pass.
- [ ] No unresolved race/data-corruption/security issue is knowingly introduced.
- [ ] No secret or accidental generated artifact is staged.
- [ ] Documentation describes current behavior accurately.
- [ ] The milestone has exactly one intended local commit or a clearly documented reason for a split required by repository tooling.

## Mandatory milestone report format

```text
STATUS: DONE or BLOCKED
MILESTONE: exact milestone number and title
BASE_SHA: SHA before milestone
END_SHA: SHA after commit when DONE
WHAT_I_INSPECTED: files/docs/commands read before changes
WHAT_CHANGED: concise implementation summary
FILES_CHANGED: exact paths
TESTS: command -> result
PHASE1_REGRESSION: command -> result or N/A with reason
CPP_VALIDATION: command -> result or N/A
DISTRIBUTED_VALIDATION: command -> result or N/A
METRICS_CAPTURED: actual measured metrics only; otherwise `None`
HUMAN_ACTION: None or exact required action
KNOWN_LIMITATIONS: explicit remaining limitations
COMMIT: commit SHA + subject
NEXT: next milestone number; continue automatically if not blocked
```

---

# MILESTONE 08 — Implement the thread-safe ready queue

## Objective

Provide a correct blocking producer/consumer primitive for local concurrent scheduling.

## Why this milestone exists

- This milestone isolates **implement the thread-safe ready queue** so failures are attributable and reviewable.
- The milestone is intentionally narrower than the final Phase-2 architecture.
- Do not implement later milestone responsibilities early unless a tiny interface stub is required for compilation.
- Any stub must be clearly marked as non-production and must not be described as a completed feature.

## Mandatory pre-read and repository inspection

- Read `AGENTS.md` and obey repository-local instructions relevant to files you will touch.
- Run `git status --short` and confirm no unrelated user work will be overwritten.
- Read every existing source file that the milestone will modify before editing it.
- Read the nearest tests covering the behavior before changing implementation.
- Inspect current package/library versions rather than assuming APIs from memory.
- Consult current official documentation before using a third-party API that may have changed.

## Entry conditions

- Milestone 07 is DONE, unless this is Milestone 01.
- Working tree contains no unexpected unrelated modifications.
- The current Phase-1 baseline gate is known and available.
- Any dependency introduced in this milestone has a verified version/source and license acceptable for the repository.

## Detailed implementation procedure

1. Implement a bounded or policy-bounded ready queue with clear ownership and shutdown behavior.
2. Use mutex + condition variable unless profiling later proves a different structure is necessary.
3. Support blocking pop, enqueue, close/shutdown, and stop-aware wakeup.
4. Define whether queue order is FIFO and test it.
5. Handle spurious wakeups correctly.
6. Ensure shutdown wakes all waiting threads.
7. Add producer/consumer stress tests.
8. Add tests for close while empty, close while blocked, many producers, many consumers, and no lost task IDs.
9. Before editing, write a short local plan listing the exact files expected to change.
10. Prefer the smallest interface that satisfies this milestone and leaves room for later milestones.
11. Keep names consistent with existing repository conventions; do not introduce a parallel naming universe.
12. If an existing helper already implements part of the behavior, reuse it rather than creating a near-duplicate.
13. Keep product-facing behavior backwards compatible unless the milestone explicitly introduces a new opt-in Evo path.
14. When behavior crosses a trust boundary, validate input before mutating durable state.
15. When behavior crosses a thread/process boundary, define ownership, lifetime, timeout, and failure semantics explicitly.
16. When behavior emits a timestamp, state whether it is wall-clock or steady-clock and use it consistently.
17. Add tests in the same milestone as the behavior; do not defer all testing to Milestone 39.
18. Update `docs/phase2/PROGRESS.md` with the observed result, not a future-tense promise.

## Phase-1 preservation checklist

- [ ] AI planner still requires explicit user Run before execution.
- [ ] Generated graph remains editable React Flow / Liveblocks state.
- [ ] Legacy Trigger.dev engine remains available unless a later explicit final decision says otherwise.
- [ ] Clerk auth/org enforcement remains server-side.
- [ ] Agent Pro-plan gate remains fail-closed.
- [ ] Live browser credentials remain server/worker only.
- [ ] Legacy Run/Stop/results/replay/rerun behavior must not regress.
- [ ] Existing node registry/executors are not duplicated without a compelling reason.
- [ ] No test is removed merely because Phase 2 changes an internal representation.

## Concurrency / distributed correctness questions to answer

- [ ] Which component owns the mutable state introduced here?
- [ ] Can two threads/processes modify it concurrently?
- [ ] What lock/transaction/unique constraint protects the invariant?
- [ ] What happens if an event/message is delivered twice?
- [ ] What happens if the process crashes after the first durable write but before the second?
- [ ] What happens when cancellation races completion?
- [ ] What happens when shutdown starts while another thread is waiting?
- [ ] What happens when a worker is slow rather than dead?
- [ ] Does this change browser-session affinity or ownership?
- [ ] Can a late result overwrite a newer logical state?

## Negative / adversarial scenarios

- [ ] Exercise or explicitly mark N/A: invalid/empty identifiers.
- [ ] Exercise or explicitly mark N/A: duplicate request/event.
- [ ] Exercise or explicitly mark N/A: unknown enum or node type.
- [ ] Exercise or explicitly mark N/A: timeout at external boundary.
- [ ] Exercise or explicitly mark N/A: cancellation at an inconvenient moment.
- [ ] Exercise or explicitly mark N/A: process shutdown while work is pending.
- [ ] Exercise or explicitly mark N/A: stale data or stale attempt result.
- [ ] Exercise or explicitly mark N/A: tenant mismatch / unauthorized request when relevant.
- [ ] Exercise or explicitly mark N/A: resource exhaustion / queue full when relevant.
- [ ] Exercise or explicitly mark N/A: malformed serialized payload when relevant.

## Explicit no-go items for this milestone

- No detached threads.
- No busy-loop polling.
- Do not invent performance numbers.
- Do not mark a future component as implemented.
- Do not silently change Phase-1 default behavior.
- Do not commit secrets or local credential values.

## Required validation

- [ ] `ready queue tests`
- [ ] `stress loop repeated many times`
- [ ] `sanitizer-ready build`
- [ ] Run current Phase-1 `npm test` when production app code is touched.
- [ ] Run `npm run typecheck` when TypeScript is touched.
- [ ] Run `npm run lint` when TypeScript/React is touched.
- [ ] Run CMake/CTest when C++ is touched.
- [ ] Run production build at integration/high-risk milestones.

## Performance evidence rules for this milestone

- If this milestone is not explicitly a benchmark milestone, do not add a resume number just because a timer was printed.
- If timing is collected for a test, label it diagnostic unless benchmark methodology requirements are satisfied.
- If this milestone changes a previously benchmarked hot path, note that old results are no longer final for the new SHA.
- Store benchmark raw data only in the defined benchmark result structure, not ad-hoc README prose.
- A result must identify workload, build mode, hardware, sample count, and commit before it becomes evidence-grade.

## Human intervention triggers

- If A required secret/account is missing. -> report `STATUS: BLOCKED` with exact manual action; do not guess.
- If A system-level prerequisite such as Docker/CMake/compiler requires user installation. -> report `STATUS: BLOCKED` with exact manual action; do not guess.
- If A shared/remote database migration would be applied. -> report `STATUS: BLOCKED` with exact manual action; do not guess.
- If Git author configuration is missing. -> report `STATUS: BLOCKED` with exact manual action; do not guess.
- If A destructive operation would touch user data or history. -> report `STATUS: BLOCKED` with exact manual action; do not guess.
- If A paid external test would incur nontrivial cost and the user has not already authorized it. -> report `STATUS: BLOCKED` with exact manual action; do not guess.

## Diff-review checklist before commit

- [ ] Review every changed file with `git diff`.
- [ ] Run `git diff --check`.
- [ ] Remove accidental formatting churn outside the milestone scope.
- [ ] Confirm no secret or `.env.local` value appears in the diff.
- [ ] Confirm no generated build tree, node_modules, vcpkg cache, Docker volume, or large transient trace has been staged.
- [ ] Confirm comments/README wording describe implemented behavior, not planned behavior.

## Commit requirement

Create one local commit with subject:

`phase2(m08): implement blocking ready queue`

After commit:

- Record the new SHA in `docs/phase2/PROGRESS.md`.
- Record commands/results for the milestone.
- Do not push unless push permission/policy has already been explicitly authorized.
- If all exit criteria are green, proceed automatically to the next milestone.

## Exit criteria

- [ ] All milestone-specific implementation items are complete.
- [ ] All relevant new tests pass.
- [ ] Required Phase-1 regression gates pass.
- [ ] No unresolved race/data-corruption/security issue is knowingly introduced.
- [ ] No secret or accidental generated artifact is staged.
- [ ] Documentation describes current behavior accurately.
- [ ] The milestone has exactly one intended local commit or a clearly documented reason for a split required by repository tooling.

## Mandatory milestone report format

```text
STATUS: DONE or BLOCKED
MILESTONE: exact milestone number and title
BASE_SHA: SHA before milestone
END_SHA: SHA after commit when DONE
WHAT_I_INSPECTED: files/docs/commands read before changes
WHAT_CHANGED: concise implementation summary
FILES_CHANGED: exact paths
TESTS: command -> result
PHASE1_REGRESSION: command -> result or N/A with reason
CPP_VALIDATION: command -> result or N/A
DISTRIBUTED_VALIDATION: command -> result or N/A
METRICS_CAPTURED: actual measured metrics only; otherwise `None`
HUMAN_ACTION: None or exact required action
KNOWN_LIMITATIONS: explicit remaining limitations
COMMIT: commit SHA + subject
NEXT: next milestone number; continue automatically if not blocked
```

---

# MILESTONE 09 — Implement the bounded std::jthread worker pool

## Objective

Add genuine C++20 multithreading with deterministic shutdown and exception handling.

## Why this milestone exists

- This milestone isolates **implement the bounded std::jthread worker pool** so failures are attributable and reviewable.
- The milestone is intentionally narrower than the final Phase-2 architecture.
- Do not implement later milestone responsibilities early unless a tiny interface stub is required for compilation.
- Any stub must be clearly marked as non-production and must not be described as a completed feature.

## Mandatory pre-read and repository inspection

- Read `AGENTS.md` and obey repository-local instructions relevant to files you will touch.
- Run `git status --short` and confirm no unrelated user work will be overwritten.
- Read every existing source file that the milestone will modify before editing it.
- Read the nearest tests covering the behavior before changing implementation.
- Inspect current package/library versions rather than assuming APIs from memory.
- Consult current official documentation before using a third-party API that may have changed.

## Entry conditions

- Milestone 08 is DONE, unless this is Milestone 01.
- Working tree contains no unexpected unrelated modifications.
- The current Phase-1 baseline gate is known and available.
- Any dependency introduced in this milestone has a verified version/source and license acceptable for the repository.

## Detailed implementation procedure

1. Create a bounded thread pool based on `std::jthread`.
2. Use stop tokens for shutdown/cancellation-aware waits where appropriate.
3. Catch task exceptions at thread boundaries and surface them through a defined result channel.
4. Track active thread count and task count for later metrics.
5. Prevent task submission after shutdown.
6. Implement graceful drain and immediate-stop semantics separately if both are needed.
7. Add tests for 1/N worker counts, task exceptions, stop during wait, repeated construction/destruction, and zero leaked/hanging threads.
8. Do not create a new thread per submitted task.
9. Before editing, write a short local plan listing the exact files expected to change.
10. Prefer the smallest interface that satisfies this milestone and leaves room for later milestones.
11. Keep names consistent with existing repository conventions; do not introduce a parallel naming universe.
12. If an existing helper already implements part of the behavior, reuse it rather than creating a near-duplicate.
13. Keep product-facing behavior backwards compatible unless the milestone explicitly introduces a new opt-in Evo path.
14. When behavior crosses a trust boundary, validate input before mutating durable state.
15. When behavior crosses a thread/process boundary, define ownership, lifetime, timeout, and failure semantics explicitly.
16. When behavior emits a timestamp, state whether it is wall-clock or steady-clock and use it consistently.
17. Add tests in the same milestone as the behavior; do not defer all testing to Milestone 39.
18. Update `docs/phase2/PROGRESS.md` with the observed result, not a future-tense promise.

## Phase-1 preservation checklist

- [ ] AI planner still requires explicit user Run before execution.
- [ ] Generated graph remains editable React Flow / Liveblocks state.
- [ ] Legacy Trigger.dev engine remains available unless a later explicit final decision says otherwise.
- [ ] Clerk auth/org enforcement remains server-side.
- [ ] Agent Pro-plan gate remains fail-closed.
- [ ] Live browser credentials remain server/worker only.
- [ ] Legacy Run/Stop/results/replay/rerun behavior must not regress.
- [ ] Existing node registry/executors are not duplicated without a compelling reason.
- [ ] No test is removed merely because Phase 2 changes an internal representation.

## Concurrency / distributed correctness questions to answer

- [ ] Which component owns the mutable state introduced here?
- [ ] Can two threads/processes modify it concurrently?
- [ ] What lock/transaction/unique constraint protects the invariant?
- [ ] What happens if an event/message is delivered twice?
- [ ] What happens if the process crashes after the first durable write but before the second?
- [ ] What happens when cancellation races completion?
- [ ] What happens when shutdown starts while another thread is waiting?
- [ ] What happens when a worker is slow rather than dead?
- [ ] Does this change browser-session affinity or ownership?
- [ ] Can a late result overwrite a newer logical state?

## Negative / adversarial scenarios

- [ ] Exercise or explicitly mark N/A: invalid/empty identifiers.
- [ ] Exercise or explicitly mark N/A: duplicate request/event.
- [ ] Exercise or explicitly mark N/A: unknown enum or node type.
- [ ] Exercise or explicitly mark N/A: timeout at external boundary.
- [ ] Exercise or explicitly mark N/A: cancellation at an inconvenient moment.
- [ ] Exercise or explicitly mark N/A: process shutdown while work is pending.
- [ ] Exercise or explicitly mark N/A: stale data or stale attempt result.
- [ ] Exercise or explicitly mark N/A: tenant mismatch / unauthorized request when relevant.
- [ ] Exercise or explicitly mark N/A: resource exhaustion / queue full when relevant.
- [ ] Exercise or explicitly mark N/A: malformed serialized payload when relevant.

## Explicit no-go items for this milestone

- The destructor must not hang indefinitely.
- No exception may escape a worker thread to terminate the process unexpectedly.
- Do not invent performance numbers.
- Do not mark a future component as implemented.
- Do not silently change Phase-1 default behavior.
- Do not commit secrets or local credential values.

## Required validation

- [ ] `thread-pool unit tests`
- [ ] `stress tests`
- [ ] `ASan/UBSan where supported`
- [ ] Run current Phase-1 `npm test` when production app code is touched.
- [ ] Run `npm run typecheck` when TypeScript is touched.
- [ ] Run `npm run lint` when TypeScript/React is touched.
- [ ] Run CMake/CTest when C++ is touched.
- [ ] Run production build at integration/high-risk milestones.

## Performance evidence rules for this milestone

- If this milestone is not explicitly a benchmark milestone, do not add a resume number just because a timer was printed.
- If timing is collected for a test, label it diagnostic unless benchmark methodology requirements are satisfied.
- If this milestone changes a previously benchmarked hot path, note that old results are no longer final for the new SHA.
- Store benchmark raw data only in the defined benchmark result structure, not ad-hoc README prose.
- A result must identify workload, build mode, hardware, sample count, and commit before it becomes evidence-grade.

## Human intervention triggers

- If A required secret/account is missing. -> report `STATUS: BLOCKED` with exact manual action; do not guess.
- If A system-level prerequisite such as Docker/CMake/compiler requires user installation. -> report `STATUS: BLOCKED` with exact manual action; do not guess.
- If A shared/remote database migration would be applied. -> report `STATUS: BLOCKED` with exact manual action; do not guess.
- If Git author configuration is missing. -> report `STATUS: BLOCKED` with exact manual action; do not guess.
- If A destructive operation would touch user data or history. -> report `STATUS: BLOCKED` with exact manual action; do not guess.
- If A paid external test would incur nontrivial cost and the user has not already authorized it. -> report `STATUS: BLOCKED` with exact manual action; do not guess.

## Diff-review checklist before commit

- [ ] Review every changed file with `git diff`.
- [ ] Run `git diff --check`.
- [ ] Remove accidental formatting churn outside the milestone scope.
- [ ] Confirm no secret or `.env.local` value appears in the diff.
- [ ] Confirm no generated build tree, node_modules, vcpkg cache, Docker volume, or large transient trace has been staged.
- [ ] Confirm comments/README wording describe implemented behavior, not planned behavior.

## Commit requirement

Create one local commit with subject:

`phase2(m09): implement bounded jthread pool`

After commit:

- Record the new SHA in `docs/phase2/PROGRESS.md`.
- Record commands/results for the milestone.
- Do not push unless push permission/policy has already been explicitly authorized.
- If all exit criteria are green, proceed automatically to the next milestone.

## Exit criteria

- [ ] All milestone-specific implementation items are complete.
- [ ] All relevant new tests pass.
- [ ] Required Phase-1 regression gates pass.
- [ ] No unresolved race/data-corruption/security issue is knowingly introduced.
- [ ] No secret or accidental generated artifact is staged.
- [ ] Documentation describes current behavior accurately.
- [ ] The milestone has exactly one intended local commit or a clearly documented reason for a split required by repository tooling.

## Mandatory milestone report format

```text
STATUS: DONE or BLOCKED
MILESTONE: exact milestone number and title
BASE_SHA: SHA before milestone
END_SHA: SHA after commit when DONE
WHAT_I_INSPECTED: files/docs/commands read before changes
WHAT_CHANGED: concise implementation summary
FILES_CHANGED: exact paths
TESTS: command -> result
PHASE1_REGRESSION: command -> result or N/A with reason
CPP_VALIDATION: command -> result or N/A
DISTRIBUTED_VALIDATION: command -> result or N/A
METRICS_CAPTURED: actual measured metrics only; otherwise `None`
HUMAN_ACTION: None or exact required action
KNOWN_LIMITATIONS: explicit remaining limitations
COMMIT: commit SHA + subject
NEXT: next milestone number; continue automatically if not blocked
```

---

# MILESTONE 10 — Build the local concurrent dependency-aware DAG scheduler

## Objective

Combine dependency readiness and the thread pool into the first real concurrent scheduler.

## Why this milestone exists

- This milestone isolates **build the local concurrent dependency-aware dag scheduler** so failures are attributable and reviewable.
- The milestone is intentionally narrower than the final Phase-2 architecture.
- Do not implement later milestone responsibilities early unless a tiny interface stub is required for compilation.
- Any stub must be clearly marked as non-production and must not be described as a completed feature.

## Mandatory pre-read and repository inspection

- Read `AGENTS.md` and obey repository-local instructions relevant to files you will touch.
- Run `git status --short` and confirm no unrelated user work will be overwritten.
- Read every existing source file that the milestone will modify before editing it.
- Read the nearest tests covering the behavior before changing implementation.
- Inspect current package/library versions rather than assuming APIs from memory.
- Consult current official documentation before using a third-party API that may have changed.

## Entry conditions

- Milestone 09 is DONE, unless this is Milestone 01.
- Working tree contains no unexpected unrelated modifications.
- The current Phase-1 baseline gate is known and available.
- Any dependency introduced in this milestone has a verified version/source and license acceptable for the repository.

## Detailed implementation procedure

1. Dispatch all currently ready independent synthetic nodes up to configured worker capacity.
2. On completion, unlock successors safely.
3. Prevent the same logical node from executing twice.
4. Propagate failure deterministically.
5. Collect per-node ready/start/finish timestamps.
6. Support linear, diamond, wide, layered, and seeded random DAGs.
7. Compare logical results against the sequential reference scheduler.
8. Add concurrency tests demonstrating independent branches overlap while dependencies do not.
9. Before editing, write a short local plan listing the exact files expected to change.
10. Prefer the smallest interface that satisfies this milestone and leaves room for later milestones.
11. Keep names consistent with existing repository conventions; do not introduce a parallel naming universe.
12. If an existing helper already implements part of the behavior, reuse it rather than creating a near-duplicate.
13. Keep product-facing behavior backwards compatible unless the milestone explicitly introduces a new opt-in Evo path.
14. When behavior crosses a trust boundary, validate input before mutating durable state.
15. When behavior crosses a thread/process boundary, define ownership, lifetime, timeout, and failure semantics explicitly.
16. When behavior emits a timestamp, state whether it is wall-clock or steady-clock and use it consistently.
17. Add tests in the same milestone as the behavior; do not defer all testing to Milestone 39.
18. Update `docs/phase2/PROGRESS.md` with the observed result, not a future-tense promise.

## Phase-1 preservation checklist

- [ ] AI planner still requires explicit user Run before execution.
- [ ] Generated graph remains editable React Flow / Liveblocks state.
- [ ] Legacy Trigger.dev engine remains available unless a later explicit final decision says otherwise.
- [ ] Clerk auth/org enforcement remains server-side.
- [ ] Agent Pro-plan gate remains fail-closed.
- [ ] Live browser credentials remain server/worker only.
- [ ] Legacy Run/Stop/results/replay/rerun behavior must not regress.
- [ ] Existing node registry/executors are not duplicated without a compelling reason.
- [ ] No test is removed merely because Phase 2 changes an internal representation.

## Concurrency / distributed correctness questions to answer

- [ ] Which component owns the mutable state introduced here?
- [ ] Can two threads/processes modify it concurrently?
- [ ] What lock/transaction/unique constraint protects the invariant?
- [ ] What happens if an event/message is delivered twice?
- [ ] What happens if the process crashes after the first durable write but before the second?
- [ ] What happens when cancellation races completion?
- [ ] What happens when shutdown starts while another thread is waiting?
- [ ] What happens when a worker is slow rather than dead?
- [ ] Does this change browser-session affinity or ownership?
- [ ] Can a late result overwrite a newer logical state?

## Negative / adversarial scenarios

- [ ] Exercise or explicitly mark N/A: invalid/empty identifiers.
- [ ] Exercise or explicitly mark N/A: duplicate request/event.
- [ ] Exercise or explicitly mark N/A: unknown enum or node type.
- [ ] Exercise or explicitly mark N/A: timeout at external boundary.
- [ ] Exercise or explicitly mark N/A: cancellation at an inconvenient moment.
- [ ] Exercise or explicitly mark N/A: process shutdown while work is pending.
- [ ] Exercise or explicitly mark N/A: stale data or stale attempt result.
- [ ] Exercise or explicitly mark N/A: tenant mismatch / unauthorized request when relevant.
- [ ] Exercise or explicitly mark N/A: resource exhaustion / queue full when relevant.
- [ ] Exercise or explicitly mark N/A: malformed serialized payload when relevant.

## Explicit no-go items for this milestone

- Do not call sleep overlap proof a universal application speedup.
- Correctness equivalence to the sequential reference is mandatory.
- Do not invent performance numbers.
- Do not mark a future component as implemented.
- Do not silently change Phase-1 default behavior.
- Do not commit secrets or local credential values.

## Required validation

- [ ] `equivalence tests`
- [ ] `concurrency overlap tests`
- [ ] `stress random DAG tests`
- [ ] `Release build`
- [ ] Run current Phase-1 `npm test` when production app code is touched.
- [ ] Run `npm run typecheck` when TypeScript is touched.
- [ ] Run `npm run lint` when TypeScript/React is touched.
- [ ] Run CMake/CTest when C++ is touched.
- [ ] Run production build at integration/high-risk milestones.

## Performance evidence rules for this milestone

- If this milestone is not explicitly a benchmark milestone, do not add a resume number just because a timer was printed.
- If timing is collected for a test, label it diagnostic unless benchmark methodology requirements are satisfied.
- If this milestone changes a previously benchmarked hot path, note that old results are no longer final for the new SHA.
- Store benchmark raw data only in the defined benchmark result structure, not ad-hoc README prose.
- A result must identify workload, build mode, hardware, sample count, and commit before it becomes evidence-grade.

## Human intervention triggers

- If A required secret/account is missing. -> report `STATUS: BLOCKED` with exact manual action; do not guess.
- If A system-level prerequisite such as Docker/CMake/compiler requires user installation. -> report `STATUS: BLOCKED` with exact manual action; do not guess.
- If A shared/remote database migration would be applied. -> report `STATUS: BLOCKED` with exact manual action; do not guess.
- If Git author configuration is missing. -> report `STATUS: BLOCKED` with exact manual action; do not guess.
- If A destructive operation would touch user data or history. -> report `STATUS: BLOCKED` with exact manual action; do not guess.
- If A paid external test would incur nontrivial cost and the user has not already authorized it. -> report `STATUS: BLOCKED` with exact manual action; do not guess.

## Diff-review checklist before commit

- [ ] Review every changed file with `git diff`.
- [ ] Run `git diff --check`.
- [ ] Remove accidental formatting churn outside the milestone scope.
- [ ] Confirm no secret or `.env.local` value appears in the diff.
- [ ] Confirm no generated build tree, node_modules, vcpkg cache, Docker volume, or large transient trace has been staged.
- [ ] Confirm comments/README wording describe implemented behavior, not planned behavior.

## Commit requirement

Create one local commit with subject:

`phase2(m10): implement concurrent dag scheduler`

After commit:

- Record the new SHA in `docs/phase2/PROGRESS.md`.
- Record commands/results for the milestone.
- Do not push unless push permission/policy has already been explicitly authorized.
- If all exit criteria are green, proceed automatically to the next milestone.

## Exit criteria

- [ ] All milestone-specific implementation items are complete.
- [ ] All relevant new tests pass.
- [ ] Required Phase-1 regression gates pass.
- [ ] No unresolved race/data-corruption/security issue is knowingly introduced.
- [ ] No secret or accidental generated artifact is staged.
- [ ] Documentation describes current behavior accurately.
- [ ] The milestone has exactly one intended local commit or a clearly documented reason for a split required by repository tooling.

## Mandatory milestone report format

```text
STATUS: DONE or BLOCKED
MILESTONE: exact milestone number and title
BASE_SHA: SHA before milestone
END_SHA: SHA after commit when DONE
WHAT_I_INSPECTED: files/docs/commands read before changes
WHAT_CHANGED: concise implementation summary
FILES_CHANGED: exact paths
TESTS: command -> result
PHASE1_REGRESSION: command -> result or N/A with reason
CPP_VALIDATION: command -> result or N/A
DISTRIBUTED_VALIDATION: command -> result or N/A
METRICS_CAPTURED: actual measured metrics only; otherwise `None`
HUMAN_ACTION: None or exact required action
KNOWN_LIMITATIONS: explicit remaining limitations
COMMIT: commit SHA + subject
NEXT: next milestone number; continue automatically if not blocked
```

---

# MILESTONE 11 — Add cooperative cancellation and graceful shutdown

## Objective

Make the local scheduler stoppable without corrupting graph state.

## Why this milestone exists

- This milestone isolates **add cooperative cancellation and graceful shutdown** so failures are attributable and reviewable.
- The milestone is intentionally narrower than the final Phase-2 architecture.
- Do not implement later milestone responsibilities early unless a tiny interface stub is required for compilation.
- Any stub must be clearly marked as non-production and must not be described as a completed feature.

## Mandatory pre-read and repository inspection

- Read `AGENTS.md` and obey repository-local instructions relevant to files you will touch.
- Run `git status --short` and confirm no unrelated user work will be overwritten.
- Read every existing source file that the milestone will modify before editing it.
- Read the nearest tests covering the behavior before changing implementation.
- Inspect current package/library versions rather than assuming APIs from memory.
- Consult current official documentation before using a third-party API that may have changed.

## Entry conditions

- Milestone 10 is DONE, unless this is Milestone 01.
- Working tree contains no unexpected unrelated modifications.
- The current Phase-1 baseline gate is known and available.
- Any dependency introduced in this milestone has a verified version/source and license acceptable for the repository.

## Detailed implementation procedure

1. Add run-level stop state and propagate stop tokens into local synthetic tasks that support cooperative cancellation.
2. Prevent new nodes from dispatching after cancellation.
3. Define state of pending/blocked nodes when the run is canceled.
4. Ensure worker threads shut down cleanly.
5. Record cancellation request and terminal timestamps for later latency metrics.
6. Test cancellation before start, during many ready tasks, during blocked dependencies, and during worker-pool shutdown.
7. Verify no tasks start after the scheduler has committed to terminal cancellation.
8. Before editing, write a short local plan listing the exact files expected to change.
9. Prefer the smallest interface that satisfies this milestone and leaves room for later milestones.
10. Keep names consistent with existing repository conventions; do not introduce a parallel naming universe.
11. If an existing helper already implements part of the behavior, reuse it rather than creating a near-duplicate.
12. Keep product-facing behavior backwards compatible unless the milestone explicitly introduces a new opt-in Evo path.
13. When behavior crosses a trust boundary, validate input before mutating durable state.
14. When behavior crosses a thread/process boundary, define ownership, lifetime, timeout, and failure semantics explicitly.
15. When behavior emits a timestamp, state whether it is wall-clock or steady-clock and use it consistently.
16. Add tests in the same milestone as the behavior; do not defer all testing to Milestone 39.
17. Update `docs/phase2/PROGRESS.md` with the observed result, not a future-tense promise.

## Phase-1 preservation checklist

- [ ] AI planner still requires explicit user Run before execution.
- [ ] Generated graph remains editable React Flow / Liveblocks state.
- [ ] Legacy Trigger.dev engine remains available unless a later explicit final decision says otherwise.
- [ ] Clerk auth/org enforcement remains server-side.
- [ ] Agent Pro-plan gate remains fail-closed.
- [ ] Live browser credentials remain server/worker only.
- [ ] Legacy Run/Stop/results/replay/rerun behavior must not regress.
- [ ] Existing node registry/executors are not duplicated without a compelling reason.
- [ ] No test is removed merely because Phase 2 changes an internal representation.

## Concurrency / distributed correctness questions to answer

- [ ] Which component owns the mutable state introduced here?
- [ ] Can two threads/processes modify it concurrently?
- [ ] What lock/transaction/unique constraint protects the invariant?
- [ ] What happens if an event/message is delivered twice?
- [ ] What happens if the process crashes after the first durable write but before the second?
- [ ] What happens when cancellation races completion?
- [ ] What happens when shutdown starts while another thread is waiting?
- [ ] What happens when a worker is slow rather than dead?
- [ ] Does this change browser-session affinity or ownership?
- [ ] Can a late result overwrite a newer logical state?

## Negative / adversarial scenarios

- [ ] Exercise or explicitly mark N/A: invalid/empty identifiers.
- [ ] Exercise or explicitly mark N/A: duplicate request/event.
- [ ] Exercise or explicitly mark N/A: unknown enum or node type.
- [ ] Exercise or explicitly mark N/A: timeout at external boundary.
- [ ] Exercise or explicitly mark N/A: cancellation at an inconvenient moment.
- [ ] Exercise or explicitly mark N/A: process shutdown while work is pending.
- [ ] Exercise or explicitly mark N/A: stale data or stale attempt result.
- [ ] Exercise or explicitly mark N/A: tenant mismatch / unauthorized request when relevant.
- [ ] Exercise or explicitly mark N/A: resource exhaustion / queue full when relevant.
- [ ] Exercise or explicitly mark N/A: malformed serialized payload when relevant.

## Explicit no-go items for this milestone

- Do not claim immediate cancellation for arbitrary external calls.
- Document cooperative vs forced cleanup semantics.
- Do not invent performance numbers.
- Do not mark a future component as implemented.
- Do not silently change Phase-1 default behavior.
- Do not commit secrets or local credential values.

## Required validation

- [ ] `cancellation tests`
- [ ] `repeat-cancel idempotency test`
- [ ] `thread leak/hang test`
- [ ] Run current Phase-1 `npm test` when production app code is touched.
- [ ] Run `npm run typecheck` when TypeScript is touched.
- [ ] Run `npm run lint` when TypeScript/React is touched.
- [ ] Run CMake/CTest when C++ is touched.
- [ ] Run production build at integration/high-risk milestones.

## Performance evidence rules for this milestone

- If this milestone is not explicitly a benchmark milestone, do not add a resume number just because a timer was printed.
- If timing is collected for a test, label it diagnostic unless benchmark methodology requirements are satisfied.
- If this milestone changes a previously benchmarked hot path, note that old results are no longer final for the new SHA.
- Store benchmark raw data only in the defined benchmark result structure, not ad-hoc README prose.
- A result must identify workload, build mode, hardware, sample count, and commit before it becomes evidence-grade.

## Human intervention triggers

- If A required secret/account is missing. -> report `STATUS: BLOCKED` with exact manual action; do not guess.
- If A system-level prerequisite such as Docker/CMake/compiler requires user installation. -> report `STATUS: BLOCKED` with exact manual action; do not guess.
- If A shared/remote database migration would be applied. -> report `STATUS: BLOCKED` with exact manual action; do not guess.
- If Git author configuration is missing. -> report `STATUS: BLOCKED` with exact manual action; do not guess.
- If A destructive operation would touch user data or history. -> report `STATUS: BLOCKED` with exact manual action; do not guess.
- If A paid external test would incur nontrivial cost and the user has not already authorized it. -> report `STATUS: BLOCKED` with exact manual action; do not guess.

## Diff-review checklist before commit

- [ ] Review every changed file with `git diff`.
- [ ] Run `git diff --check`.
- [ ] Remove accidental formatting churn outside the milestone scope.
- [ ] Confirm no secret or `.env.local` value appears in the diff.
- [ ] Confirm no generated build tree, node_modules, vcpkg cache, Docker volume, or large transient trace has been staged.
- [ ] Confirm comments/README wording describe implemented behavior, not planned behavior.

## Commit requirement

Create one local commit with subject:

`phase2(m11): add scheduler cancellation semantics`

After commit:

- Record the new SHA in `docs/phase2/PROGRESS.md`.
- Record commands/results for the milestone.
- Do not push unless push permission/policy has already been explicitly authorized.
- If all exit criteria are green, proceed automatically to the next milestone.

## Exit criteria

- [ ] All milestone-specific implementation items are complete.
- [ ] All relevant new tests pass.
- [ ] Required Phase-1 regression gates pass.
- [ ] No unresolved race/data-corruption/security issue is knowingly introduced.
- [ ] No secret or accidental generated artifact is staged.
- [ ] Documentation describes current behavior accurately.
- [ ] The milestone has exactly one intended local commit or a clearly documented reason for a split required by repository tooling.

## Mandatory milestone report format

```text
STATUS: DONE or BLOCKED
MILESTONE: exact milestone number and title
BASE_SHA: SHA before milestone
END_SHA: SHA after commit when DONE
WHAT_I_INSPECTED: files/docs/commands read before changes
WHAT_CHANGED: concise implementation summary
FILES_CHANGED: exact paths
TESTS: command -> result
PHASE1_REGRESSION: command -> result or N/A with reason
CPP_VALIDATION: command -> result or N/A
DISTRIBUTED_VALIDATION: command -> result or N/A
METRICS_CAPTURED: actual measured metrics only; otherwise `None`
HUMAN_ACTION: None or exact required action
KNOWN_LIMITATIONS: explicit remaining limitations
COMMIT: commit SHA + subject
NEXT: next milestone number; continue automatically if not blocked
```

---

# MILESTONE 12 — Add execution resource classes and browser affinity policy

## Objective

Prevent invalid parallelism and prepare the scheduler for real EvoBrowser tasks.

## Why this milestone exists

- This milestone isolates **add execution resource classes and browser affinity policy** so failures are attributable and reviewable.
- The milestone is intentionally narrower than the final Phase-2 architecture.
- Do not implement later milestone responsibilities early unless a tiny interface stub is required for compilation.
- Any stub must be clearly marked as non-production and must not be described as a completed feature.

## Mandatory pre-read and repository inspection

- Read `AGENTS.md` and obey repository-local instructions relevant to files you will touch.
- Run `git status --short` and confirm no unrelated user work will be overwritten.
- Read every existing source file that the milestone will modify before editing it.
- Read the nearest tests covering the behavior before changing implementation.
- Inspect current package/library versions rather than assuming APIs from memory.
- Consult current official documentation before using a third-party API that may have changed.

## Entry conditions

- Milestone 11 is DONE, unless this is Milestone 01.
- Working tree contains no unexpected unrelated modifications.
- The current Phase-1 baseline gate is known and available.
- Any dependency introduced in this milestone has a verified version/source and license acceptable for the repository.

## Detailed implementation procedure

1. Create a Phase-2 execution-policy mapping keyed by existing `NodeType` without changing planner behavior.
2. Classify start as internal, browser nodes as browser-affinity work, and send-email as external side-effect/I-O work.
3. Derive a default browser affinity key from run ID so all Phase-1 browser nodes in one run serialize and stay on one worker/session.
4. Add resource-capacity accounting independent of dependency readiness.
5. Test two ready browser nodes sharing an affinity key never run concurrently.
6. Test independent affinity keys can progress concurrently when capacity permits.
7. Test non-browser work can overlap browser waiting when dependencies permit.
8. Document that Phase 2 initially gains concurrency mainly across independent non-browser branches and across workflows while preserving browser state semantics.
9. Before editing, write a short local plan listing the exact files expected to change.
10. Prefer the smallest interface that satisfies this milestone and leaves room for later milestones.
11. Keep names consistent with existing repository conventions; do not introduce a parallel naming universe.
12. If an existing helper already implements part of the behavior, reuse it rather than creating a near-duplicate.
13. Keep product-facing behavior backwards compatible unless the milestone explicitly introduces a new opt-in Evo path.
14. When behavior crosses a trust boundary, validate input before mutating durable state.
15. When behavior crosses a thread/process boundary, define ownership, lifetime, timeout, and failure semantics explicitly.
16. When behavior emits a timestamp, state whether it is wall-clock or steady-clock and use it consistently.
17. Add tests in the same milestone as the behavior; do not defer all testing to Milestone 39.
18. Update `docs/phase2/PROGRESS.md` with the observed result, not a future-tense promise.

## Phase-1 preservation checklist

- [ ] AI planner still requires explicit user Run before execution.
- [ ] Generated graph remains editable React Flow / Liveblocks state.
- [ ] Legacy Trigger.dev engine remains available unless a later explicit final decision says otherwise.
- [ ] Clerk auth/org enforcement remains server-side.
- [ ] Agent Pro-plan gate remains fail-closed.
- [ ] Live browser credentials remain server/worker only.
- [ ] Legacy Run/Stop/results/replay/rerun behavior must not regress.
- [ ] Existing node registry/executors are not duplicated without a compelling reason.
- [ ] No test is removed merely because Phase 2 changes an internal representation.

## Concurrency / distributed correctness questions to answer

- [ ] Which component owns the mutable state introduced here?
- [ ] Can two threads/processes modify it concurrently?
- [ ] What lock/transaction/unique constraint protects the invariant?
- [ ] What happens if an event/message is delivered twice?
- [ ] What happens if the process crashes after the first durable write but before the second?
- [ ] What happens when cancellation races completion?
- [ ] What happens when shutdown starts while another thread is waiting?
- [ ] What happens when a worker is slow rather than dead?
- [ ] Does this change browser-session affinity or ownership?
- [ ] Can a late result overwrite a newer logical state?

## Negative / adversarial scenarios

- [ ] Exercise or explicitly mark N/A: invalid/empty identifiers.
- [ ] Exercise or explicitly mark N/A: duplicate request/event.
- [ ] Exercise or explicitly mark N/A: unknown enum or node type.
- [ ] Exercise or explicitly mark N/A: timeout at external boundary.
- [ ] Exercise or explicitly mark N/A: cancellation at an inconvenient moment.
- [ ] Exercise or explicitly mark N/A: process shutdown while work is pending.
- [ ] Exercise or explicitly mark N/A: stale data or stale attempt result.
- [ ] Exercise or explicitly mark N/A: tenant mismatch / unauthorized request when relevant.
- [ ] Exercise or explicitly mark N/A: resource exhaustion / queue full when relevant.
- [ ] Exercise or explicitly mark N/A: malformed serialized payload when relevant.

## Explicit no-go items for this milestone

- Do not silently create multiple Browserbase sessions per Phase-1 workflow.
- Do not modify planner output schema solely for affinity.
- Do not invent performance numbers.
- Do not mark a future component as implemented.
- Do not silently change Phase-1 default behavior.
- Do not commit secrets or local credential values.

## Required validation

- [ ] `resource policy compile-time completeness`
- [ ] `affinity scheduler tests`
- [ ] `Phase-1 tests`
- [ ] Run current Phase-1 `npm test` when production app code is touched.
- [ ] Run `npm run typecheck` when TypeScript is touched.
- [ ] Run `npm run lint` when TypeScript/React is touched.
- [ ] Run CMake/CTest when C++ is touched.
- [ ] Run production build at integration/high-risk milestones.

## Performance evidence rules for this milestone

- If this milestone is not explicitly a benchmark milestone, do not add a resume number just because a timer was printed.
- If timing is collected for a test, label it diagnostic unless benchmark methodology requirements are satisfied.
- If this milestone changes a previously benchmarked hot path, note that old results are no longer final for the new SHA.
- Store benchmark raw data only in the defined benchmark result structure, not ad-hoc README prose.
- A result must identify workload, build mode, hardware, sample count, and commit before it becomes evidence-grade.

## Human intervention triggers

- If A required secret/account is missing. -> report `STATUS: BLOCKED` with exact manual action; do not guess.
- If A system-level prerequisite such as Docker/CMake/compiler requires user installation. -> report `STATUS: BLOCKED` with exact manual action; do not guess.
- If A shared/remote database migration would be applied. -> report `STATUS: BLOCKED` with exact manual action; do not guess.
- If Git author configuration is missing. -> report `STATUS: BLOCKED` with exact manual action; do not guess.
- If A destructive operation would touch user data or history. -> report `STATUS: BLOCKED` with exact manual action; do not guess.
- If A paid external test would incur nontrivial cost and the user has not already authorized it. -> report `STATUS: BLOCKED` with exact manual action; do not guess.

## Diff-review checklist before commit

- [ ] Review every changed file with `git diff`.
- [ ] Run `git diff --check`.
- [ ] Remove accidental formatting churn outside the milestone scope.
- [ ] Confirm no secret or `.env.local` value appears in the diff.
- [ ] Confirm no generated build tree, node_modules, vcpkg cache, Docker volume, or large transient trace has been staged.
- [ ] Confirm comments/README wording describe implemented behavior, not planned behavior.

## Commit requirement

Create one local commit with subject:

`phase2(m12): add resource-aware scheduling and browser affinity`

After commit:

- Record the new SHA in `docs/phase2/PROGRESS.md`.
- Record commands/results for the milestone.
- Do not push unless push permission/policy has already been explicitly authorized.
- If all exit criteria are green, proceed automatically to the next milestone.

## Exit criteria

- [ ] All milestone-specific implementation items are complete.
- [ ] All relevant new tests pass.
- [ ] Required Phase-1 regression gates pass.
- [ ] No unresolved race/data-corruption/security issue is knowingly introduced.
- [ ] No secret or accidental generated artifact is staged.
- [ ] Documentation describes current behavior accurately.
- [ ] The milestone has exactly one intended local commit or a clearly documented reason for a split required by repository tooling.

## Mandatory milestone report format

```text
STATUS: DONE or BLOCKED
MILESTONE: exact milestone number and title
BASE_SHA: SHA before milestone
END_SHA: SHA after commit when DONE
WHAT_I_INSPECTED: files/docs/commands read before changes
WHAT_CHANGED: concise implementation summary
FILES_CHANGED: exact paths
TESTS: command -> result
PHASE1_REGRESSION: command -> result or N/A with reason
CPP_VALIDATION: command -> result or N/A
DISTRIBUTED_VALIDATION: command -> result or N/A
METRICS_CAPTURED: actual measured metrics only; otherwise `None`
HUMAN_ACTION: None or exact required action
KNOWN_LIMITATIONS: explicit remaining limitations
COMMIT: commit SHA + subject
NEXT: next milestone number; continue automatically if not blocked
```

---

# MILESTONE 13 — Instrument the scheduler core with evidence-grade timestamps and counters

## Objective

Collect metrics at the source before distributed complexity arrives.

## Why this milestone exists

- This milestone isolates **instrument the scheduler core with evidence-grade timestamps and counters** so failures are attributable and reviewable.
- The milestone is intentionally narrower than the final Phase-2 architecture.
- Do not implement later milestone responsibilities early unless a tiny interface stub is required for compilation.
- Any stub must be clearly marked as non-production and must not be described as a completed feature.

## Mandatory pre-read and repository inspection

- Read `AGENTS.md` and obey repository-local instructions relevant to files you will touch.
- Run `git status --short` and confirm no unrelated user work will be overwritten.
- Read every existing source file that the milestone will modify before editing it.
- Read the nearest tests covering the behavior before changing implementation.
- Inspect current package/library versions rather than assuming APIs from memory.
- Consult current official documentation before using a third-party API that may have changed.

## Entry conditions

- Milestone 12 is DONE, unless this is Milestone 01.
- Working tree contains no unexpected unrelated modifications.
- The current Phase-1 baseline gate is known and available.
- Any dependency introduced in this milestone has a verified version/source and license acceptable for the repository.

## Detailed implementation procedure

1. Add per-run and per-node timestamps using steady_clock for durations.
2. Track ready time, dispatch time, start time, finish time, queue wait, active workers, queue depth high-water mark, retry counters placeholder, and scheduler event counts.
3. Separate measurement from presentation.
4. Make metrics exportable as structured JSON for benchmark harnesses.
5. Avoid locks on metrics hot paths unless needed; correctness remains more important than micro-optimization.
6. Add tests for monotonic timestamp relationships and exact logical counters on deterministic tasks.
7. Document metric definitions in code/docs using the authoritative formulas.
8. Before editing, write a short local plan listing the exact files expected to change.
9. Prefer the smallest interface that satisfies this milestone and leaves room for later milestones.
10. Keep names consistent with existing repository conventions; do not introduce a parallel naming universe.
11. If an existing helper already implements part of the behavior, reuse it rather than creating a near-duplicate.
12. Keep product-facing behavior backwards compatible unless the milestone explicitly introduces a new opt-in Evo path.
13. When behavior crosses a trust boundary, validate input before mutating durable state.
14. When behavior crosses a thread/process boundary, define ownership, lifetime, timeout, and failure semantics explicitly.
15. When behavior emits a timestamp, state whether it is wall-clock or steady-clock and use it consistently.
16. Add tests in the same milestone as the behavior; do not defer all testing to Milestone 39.
17. Update `docs/phase2/PROGRESS.md` with the observed result, not a future-tense promise.

## Phase-1 preservation checklist

- [ ] AI planner still requires explicit user Run before execution.
- [ ] Generated graph remains editable React Flow / Liveblocks state.
- [ ] Legacy Trigger.dev engine remains available unless a later explicit final decision says otherwise.
- [ ] Clerk auth/org enforcement remains server-side.
- [ ] Agent Pro-plan gate remains fail-closed.
- [ ] Live browser credentials remain server/worker only.
- [ ] Legacy Run/Stop/results/replay/rerun behavior must not regress.
- [ ] Existing node registry/executors are not duplicated without a compelling reason.
- [ ] No test is removed merely because Phase 2 changes an internal representation.

## Concurrency / distributed correctness questions to answer

- [ ] Which component owns the mutable state introduced here?
- [ ] Can two threads/processes modify it concurrently?
- [ ] What lock/transaction/unique constraint protects the invariant?
- [ ] What happens if an event/message is delivered twice?
- [ ] What happens if the process crashes after the first durable write but before the second?
- [ ] What happens when cancellation races completion?
- [ ] What happens when shutdown starts while another thread is waiting?
- [ ] What happens when a worker is slow rather than dead?
- [ ] Does this change browser-session affinity or ownership?
- [ ] Can a late result overwrite a newer logical state?

## Negative / adversarial scenarios

- [ ] Exercise or explicitly mark N/A: invalid/empty identifiers.
- [ ] Exercise or explicitly mark N/A: duplicate request/event.
- [ ] Exercise or explicitly mark N/A: unknown enum or node type.
- [ ] Exercise or explicitly mark N/A: timeout at external boundary.
- [ ] Exercise or explicitly mark N/A: cancellation at an inconvenient moment.
- [ ] Exercise or explicitly mark N/A: process shutdown while work is pending.
- [ ] Exercise or explicitly mark N/A: stale data or stale attempt result.
- [ ] Exercise or explicitly mark N/A: tenant mismatch / unauthorized request when relevant.
- [ ] Exercise or explicitly mark N/A: resource exhaustion / queue full when relevant.
- [ ] Exercise or explicitly mark N/A: malformed serialized payload when relevant.

## Explicit no-go items for this milestone

- Do not claim CPU/memory metrics until an actual collector exists.
- Do not use system_clock for elapsed durations.
- Do not invent performance numbers.
- Do not mark a future component as implemented.
- Do not silently change Phase-1 default behavior.
- Do not commit secrets or local credential values.

## Required validation

- [ ] `metrics unit tests`
- [ ] `JSON schema/shape test`
- [ ] `Release smoke`
- [ ] Run current Phase-1 `npm test` when production app code is touched.
- [ ] Run `npm run typecheck` when TypeScript is touched.
- [ ] Run `npm run lint` when TypeScript/React is touched.
- [ ] Run CMake/CTest when C++ is touched.
- [ ] Run production build at integration/high-risk milestones.

## Performance evidence rules for this milestone

- If this milestone is not explicitly a benchmark milestone, do not add a resume number just because a timer was printed.
- If timing is collected for a test, label it diagnostic unless benchmark methodology requirements are satisfied.
- If this milestone changes a previously benchmarked hot path, note that old results are no longer final for the new SHA.
- Store benchmark raw data only in the defined benchmark result structure, not ad-hoc README prose.
- A result must identify workload, build mode, hardware, sample count, and commit before it becomes evidence-grade.

## Human intervention triggers

- If A required secret/account is missing. -> report `STATUS: BLOCKED` with exact manual action; do not guess.
- If A system-level prerequisite such as Docker/CMake/compiler requires user installation. -> report `STATUS: BLOCKED` with exact manual action; do not guess.
- If A shared/remote database migration would be applied. -> report `STATUS: BLOCKED` with exact manual action; do not guess.
- If Git author configuration is missing. -> report `STATUS: BLOCKED` with exact manual action; do not guess.
- If A destructive operation would touch user data or history. -> report `STATUS: BLOCKED` with exact manual action; do not guess.
- If A paid external test would incur nontrivial cost and the user has not already authorized it. -> report `STATUS: BLOCKED` with exact manual action; do not guess.

## Diff-review checklist before commit

- [ ] Review every changed file with `git diff`.
- [ ] Run `git diff --check`.
- [ ] Remove accidental formatting churn outside the milestone scope.
- [ ] Confirm no secret or `.env.local` value appears in the diff.
- [ ] Confirm no generated build tree, node_modules, vcpkg cache, Docker volume, or large transient trace has been staged.
- [ ] Confirm comments/README wording describe implemented behavior, not planned behavior.

## Commit requirement

Create one local commit with subject:

`phase2(m13): instrument scheduler metrics`

After commit:

- Record the new SHA in `docs/phase2/PROGRESS.md`.
- Record commands/results for the milestone.
- Do not push unless push permission/policy has already been explicitly authorized.
- If all exit criteria are green, proceed automatically to the next milestone.

## Exit criteria

- [ ] All milestone-specific implementation items are complete.
- [ ] All relevant new tests pass.
- [ ] Required Phase-1 regression gates pass.
- [ ] No unresolved race/data-corruption/security issue is knowingly introduced.
- [ ] No secret or accidental generated artifact is staged.
- [ ] Documentation describes current behavior accurately.
- [ ] The milestone has exactly one intended local commit or a clearly documented reason for a split required by repository tooling.

## Mandatory milestone report format

```text
STATUS: DONE or BLOCKED
MILESTONE: exact milestone number and title
BASE_SHA: SHA before milestone
END_SHA: SHA after commit when DONE
WHAT_I_INSPECTED: files/docs/commands read before changes
WHAT_CHANGED: concise implementation summary
FILES_CHANGED: exact paths
TESTS: command -> result
PHASE1_REGRESSION: command -> result or N/A with reason
CPP_VALIDATION: command -> result or N/A
DISTRIBUTED_VALIDATION: command -> result or N/A
METRICS_CAPTURED: actual measured metrics only; otherwise `None`
HUMAN_ACTION: None or exact required action
KNOWN_LIMITATIONS: explicit remaining limitations
COMMIT: commit SHA + subject
NEXT: next milestone number; continue automatically if not blocked
```

---

# MILESTONE 14 — Harden C++ correctness with sanitizers and concurrency stress

## Objective

Find races/UB before network integration hides them.

## Why this milestone exists

- This milestone isolates **harden c++ correctness with sanitizers and concurrency stress** so failures are attributable and reviewable.
- The milestone is intentionally narrower than the final Phase-2 architecture.
- Do not implement later milestone responsibilities early unless a tiny interface stub is required for compilation.
- Any stub must be clearly marked as non-production and must not be described as a completed feature.

## Mandatory pre-read and repository inspection

- Read `AGENTS.md` and obey repository-local instructions relevant to files you will touch.
- Run `git status --short` and confirm no unrelated user work will be overwritten.
- Read every existing source file that the milestone will modify before editing it.
- Read the nearest tests covering the behavior before changing implementation.
- Inspect current package/library versions rather than assuming APIs from memory.
- Consult current official documentation before using a third-party API that may have changed.

## Entry conditions

- Milestone 13 is DONE, unless this is Milestone 01.
- Working tree contains no unexpected unrelated modifications.
- The current Phase-1 baseline gate is known and available.
- Any dependency introduced in this milestone has a verified version/source and license acceptable for the repository.

## Detailed implementation procedure

1. Add build options for AddressSanitizer and UndefinedBehaviorSanitizer where supported.
2. Add ThreadSanitizer configuration where the platform/compiler supports it.
3. Do not mark unsupported sanitizer tooling as a product failure; document platform limits.
4. Run repeated random DAG stress tests with deterministic seed logging.
5. Run repeated queue/pool construction and shutdown cycles.
6. Run failure and cancellation stress tests.
7. Add strong warning flags to project-owned C++ targets.
8. Document any sanitizer suppressions; do not suppress real project bugs.
9. Before editing, write a short local plan listing the exact files expected to change.
10. Prefer the smallest interface that satisfies this milestone and leaves room for later milestones.
11. Keep names consistent with existing repository conventions; do not introduce a parallel naming universe.
12. If an existing helper already implements part of the behavior, reuse it rather than creating a near-duplicate.
13. Keep product-facing behavior backwards compatible unless the milestone explicitly introduces a new opt-in Evo path.
14. When behavior crosses a trust boundary, validate input before mutating durable state.
15. When behavior crosses a thread/process boundary, define ownership, lifetime, timeout, and failure semantics explicitly.
16. When behavior emits a timestamp, state whether it is wall-clock or steady-clock and use it consistently.
17. Add tests in the same milestone as the behavior; do not defer all testing to Milestone 39.
18. Update `docs/phase2/PROGRESS.md` with the observed result, not a future-tense promise.

## Phase-1 preservation checklist

- [ ] AI planner still requires explicit user Run before execution.
- [ ] Generated graph remains editable React Flow / Liveblocks state.
- [ ] Legacy Trigger.dev engine remains available unless a later explicit final decision says otherwise.
- [ ] Clerk auth/org enforcement remains server-side.
- [ ] Agent Pro-plan gate remains fail-closed.
- [ ] Live browser credentials remain server/worker only.
- [ ] Legacy Run/Stop/results/replay/rerun behavior must not regress.
- [ ] Existing node registry/executors are not duplicated without a compelling reason.
- [ ] No test is removed merely because Phase 2 changes an internal representation.

## Concurrency / distributed correctness questions to answer

- [ ] Which component owns the mutable state introduced here?
- [ ] Can two threads/processes modify it concurrently?
- [ ] What lock/transaction/unique constraint protects the invariant?
- [ ] What happens if an event/message is delivered twice?
- [ ] What happens if the process crashes after the first durable write but before the second?
- [ ] What happens when cancellation races completion?
- [ ] What happens when shutdown starts while another thread is waiting?
- [ ] What happens when a worker is slow rather than dead?
- [ ] Does this change browser-session affinity or ownership?
- [ ] Can a late result overwrite a newer logical state?

## Negative / adversarial scenarios

- [ ] Exercise or explicitly mark N/A: invalid/empty identifiers.
- [ ] Exercise or explicitly mark N/A: duplicate request/event.
- [ ] Exercise or explicitly mark N/A: unknown enum or node type.
- [ ] Exercise or explicitly mark N/A: timeout at external boundary.
- [ ] Exercise or explicitly mark N/A: cancellation at an inconvenient moment.
- [ ] Exercise or explicitly mark N/A: process shutdown while work is pending.
- [ ] Exercise or explicitly mark N/A: stale data or stale attempt result.
- [ ] Exercise or explicitly mark N/A: tenant mismatch / unauthorized request when relevant.
- [ ] Exercise or explicitly mark N/A: resource exhaustion / queue full when relevant.
- [ ] Exercise or explicitly mark N/A: malformed serialized payload when relevant.

## Explicit no-go items for this milestone

- Do not enable incompatible sanitizers together without checking toolchain support.
- No `-Werror` on third-party headers.
- Do not invent performance numbers.
- Do not mark a future component as implemented.
- Do not silently change Phase-1 default behavior.
- Do not commit secrets or local credential values.

## Required validation

- [ ] `ASan/UBSan suite`
- [ ] `TSan suite if supported`
- [ ] `100+ deterministic stress iterations or justified equivalent`
- [ ] Run current Phase-1 `npm test` when production app code is touched.
- [ ] Run `npm run typecheck` when TypeScript is touched.
- [ ] Run `npm run lint` when TypeScript/React is touched.
- [ ] Run CMake/CTest when C++ is touched.
- [ ] Run production build at integration/high-risk milestones.

## Performance evidence rules for this milestone

- If this milestone is not explicitly a benchmark milestone, do not add a resume number just because a timer was printed.
- If timing is collected for a test, label it diagnostic unless benchmark methodology requirements are satisfied.
- If this milestone changes a previously benchmarked hot path, note that old results are no longer final for the new SHA.
- Store benchmark raw data only in the defined benchmark result structure, not ad-hoc README prose.
- A result must identify workload, build mode, hardware, sample count, and commit before it becomes evidence-grade.

## Human intervention triggers

- If A required secret/account is missing. -> report `STATUS: BLOCKED` with exact manual action; do not guess.
- If A system-level prerequisite such as Docker/CMake/compiler requires user installation. -> report `STATUS: BLOCKED` with exact manual action; do not guess.
- If A shared/remote database migration would be applied. -> report `STATUS: BLOCKED` with exact manual action; do not guess.
- If Git author configuration is missing. -> report `STATUS: BLOCKED` with exact manual action; do not guess.
- If A destructive operation would touch user data or history. -> report `STATUS: BLOCKED` with exact manual action; do not guess.
- If A paid external test would incur nontrivial cost and the user has not already authorized it. -> report `STATUS: BLOCKED` with exact manual action; do not guess.

## Diff-review checklist before commit

- [ ] Review every changed file with `git diff`.
- [ ] Run `git diff --check`.
- [ ] Remove accidental formatting churn outside the milestone scope.
- [ ] Confirm no secret or `.env.local` value appears in the diff.
- [ ] Confirm no generated build tree, node_modules, vcpkg cache, Docker volume, or large transient trace has been staged.
- [ ] Confirm comments/README wording describe implemented behavior, not planned behavior.

## Commit requirement

Create one local commit with subject:

`phase2(m14): harden concurrent core with sanitizers`

After commit:

- Record the new SHA in `docs/phase2/PROGRESS.md`.
- Record commands/results for the milestone.
- Do not push unless push permission/policy has already been explicitly authorized.
- If all exit criteria are green, proceed automatically to the next milestone.

## Exit criteria

- [ ] All milestone-specific implementation items are complete.
- [ ] All relevant new tests pass.
- [ ] Required Phase-1 regression gates pass.
- [ ] No unresolved race/data-corruption/security issue is knowingly introduced.
- [ ] No secret or accidental generated artifact is staged.
- [ ] Documentation describes current behavior accurately.
- [ ] The milestone has exactly one intended local commit or a clearly documented reason for a split required by repository tooling.

## Mandatory milestone report format

```text
STATUS: DONE or BLOCKED
MILESTONE: exact milestone number and title
BASE_SHA: SHA before milestone
END_SHA: SHA after commit when DONE
WHAT_I_INSPECTED: files/docs/commands read before changes
WHAT_CHANGED: concise implementation summary
FILES_CHANGED: exact paths
TESTS: command -> result
PHASE1_REGRESSION: command -> result or N/A with reason
CPP_VALIDATION: command -> result or N/A
DISTRIBUTED_VALIDATION: command -> result or N/A
METRICS_CAPTURED: actual measured metrics only; otherwise `None`
HUMAN_ACTION: None or exact required action
KNOWN_LIMITATIONS: explicit remaining limitations
COMMIT: commit SHA + subject
NEXT: next milestone number; continue automatically if not blocked
```

---

# MILESTONE 15 — Create the first local benchmark corpus and sequential-vs-concurrent evidence

## Objective

Measure the C++ scheduler before introducing distributed transport.

## Why this milestone exists

- This milestone isolates **create the first local benchmark corpus and sequential-vs-concurrent evidence** so failures are attributable and reviewable.
- The milestone is intentionally narrower than the final Phase-2 architecture.
- Do not implement later milestone responsibilities early unless a tiny interface stub is required for compilation.
- Any stub must be clearly marked as non-production and must not be described as a completed feature.

## Mandatory pre-read and repository inspection

- Read `AGENTS.md` and obey repository-local instructions relevant to files you will touch.
- Run `git status --short` and confirm no unrelated user work will be overwritten.
- Read every existing source file that the milestone will modify before editing it.
- Read the nearest tests covering the behavior before changing implementation.
- Inspect current package/library versions rather than assuming APIs from memory.
- Consult current official documentation before using a third-party API that may have changed.

## Entry conditions

- Milestone 14 is DONE, unless this is Milestone 01.
- Working tree contains no unexpected unrelated modifications.
- The current Phase-1 baseline gate is known and available.
- Any dependency introduced in this milestone has a verified version/source and license acceptable for the repository.

## Detailed implementation procedure

1. Create benchmark workload files for linear, diamond, wide fan-out/fan-in, layered, and seeded random DAGs.
2. Include controlled I/O-like sleep workloads and deterministic CPU-like workloads.
3. Create a runner that records machine/compiler/build metadata and raw samples.
4. Use Release builds, warmups, and repeated trials.
5. Benchmark worker/thread counts such as 1,2,4,8 and additional sensible counts based on hardware.
6. Generate raw CSV/JSON and a summary with p50/p95/p99 where sample count justifies it.
7. Calculate speedup only against the identical C++ sequential reference workload.
8. Create `docs/phase2/LOCAL_SCHEDULER_BENCHMARK.md` with methodology and measured results.
9. Do not write resume bullets yet; put candidate evidence status in a temporary evidence table.
10. Before editing, write a short local plan listing the exact files expected to change.
11. Prefer the smallest interface that satisfies this milestone and leaves room for later milestones.
12. Keep names consistent with existing repository conventions; do not introduce a parallel naming universe.
13. If an existing helper already implements part of the behavior, reuse it rather than creating a near-duplicate.
14. Keep product-facing behavior backwards compatible unless the milestone explicitly introduces a new opt-in Evo path.
15. When behavior crosses a trust boundary, validate input before mutating durable state.
16. When behavior crosses a thread/process boundary, define ownership, lifetime, timeout, and failure semantics explicitly.
17. When behavior emits a timestamp, state whether it is wall-clock or steady-clock and use it consistently.
18. Add tests in the same milestone as the behavior; do not defer all testing to Milestone 39.
19. Update `docs/phase2/PROGRESS.md` with the observed result, not a future-tense promise.

## Phase-1 preservation checklist

- [ ] AI planner still requires explicit user Run before execution.
- [ ] Generated graph remains editable React Flow / Liveblocks state.
- [ ] Legacy Trigger.dev engine remains available unless a later explicit final decision says otherwise.
- [ ] Clerk auth/org enforcement remains server-side.
- [ ] Agent Pro-plan gate remains fail-closed.
- [ ] Live browser credentials remain server/worker only.
- [ ] Legacy Run/Stop/results/replay/rerun behavior must not regress.
- [ ] Existing node registry/executors are not duplicated without a compelling reason.
- [ ] No test is removed merely because Phase 2 changes an internal representation.

## Concurrency / distributed correctness questions to answer

- [ ] Which component owns the mutable state introduced here?
- [ ] Can two threads/processes modify it concurrently?
- [ ] What lock/transaction/unique constraint protects the invariant?
- [ ] What happens if an event/message is delivered twice?
- [ ] What happens if the process crashes after the first durable write but before the second?
- [ ] What happens when cancellation races completion?
- [ ] What happens when shutdown starts while another thread is waiting?
- [ ] What happens when a worker is slow rather than dead?
- [ ] Does this change browser-session affinity or ownership?
- [ ] Can a late result overwrite a newer logical state?

## Negative / adversarial scenarios

- [ ] Exercise or explicitly mark N/A: invalid/empty identifiers.
- [ ] Exercise or explicitly mark N/A: duplicate request/event.
- [ ] Exercise or explicitly mark N/A: unknown enum or node type.
- [ ] Exercise or explicitly mark N/A: timeout at external boundary.
- [ ] Exercise or explicitly mark N/A: cancellation at an inconvenient moment.
- [ ] Exercise or explicitly mark N/A: process shutdown while work is pending.
- [ ] Exercise or explicitly mark N/A: stale data or stale attempt result.
- [ ] Exercise or explicitly mark N/A: tenant mismatch / unauthorized request when relevant.
- [ ] Exercise or explicitly mark N/A: resource exhaustion / queue full when relevant.
- [ ] Exercise or explicitly mark N/A: malformed serialized payload when relevant.

## Explicit no-go items for this milestone

- Do not benchmark Debug builds as final evidence.
- Do not hide slower cases; include them and explain saturation/overhead.
- Do not invent performance numbers.
- Do not mark a future component as implemented.
- Do not silently change Phase-1 default behavior.
- Do not commit secrets or local credential values.

## Required validation

- [ ] `benchmark reproducibility rerun`
- [ ] `raw artifact validation`
- [ ] `checksum/manifest generation`
- [ ] Run current Phase-1 `npm test` when production app code is touched.
- [ ] Run `npm run typecheck` when TypeScript is touched.
- [ ] Run `npm run lint` when TypeScript/React is touched.
- [ ] Run CMake/CTest when C++ is touched.
- [ ] Run production build at integration/high-risk milestones.

## Performance evidence rules for this milestone

- If this milestone is not explicitly a benchmark milestone, do not add a resume number just because a timer was printed.
- If timing is collected for a test, label it diagnostic unless benchmark methodology requirements are satisfied.
- If this milestone changes a previously benchmarked hot path, note that old results are no longer final for the new SHA.
- Store benchmark raw data only in the defined benchmark result structure, not ad-hoc README prose.
- A result must identify workload, build mode, hardware, sample count, and commit before it becomes evidence-grade.

## Human intervention triggers

- If A required secret/account is missing. -> report `STATUS: BLOCKED` with exact manual action; do not guess.
- If A system-level prerequisite such as Docker/CMake/compiler requires user installation. -> report `STATUS: BLOCKED` with exact manual action; do not guess.
- If A shared/remote database migration would be applied. -> report `STATUS: BLOCKED` with exact manual action; do not guess.
- If Git author configuration is missing. -> report `STATUS: BLOCKED` with exact manual action; do not guess.
- If A destructive operation would touch user data or history. -> report `STATUS: BLOCKED` with exact manual action; do not guess.
- If A paid external test would incur nontrivial cost and the user has not already authorized it. -> report `STATUS: BLOCKED` with exact manual action; do not guess.

## Diff-review checklist before commit

- [ ] Review every changed file with `git diff`.
- [ ] Run `git diff --check`.
- [ ] Remove accidental formatting churn outside the milestone scope.
- [ ] Confirm no secret or `.env.local` value appears in the diff.
- [ ] Confirm no generated build tree, node_modules, vcpkg cache, Docker volume, or large transient trace has been staged.
- [ ] Confirm comments/README wording describe implemented behavior, not planned behavior.

## Commit requirement

Create one local commit with subject:

`phase2(m15): benchmark local concurrent scheduler`

After commit:

- Record the new SHA in `docs/phase2/PROGRESS.md`.
- Record commands/results for the milestone.
- Do not push unless push permission/policy has already been explicitly authorized.
- If all exit criteria are green, proceed automatically to the next milestone.

## Exit criteria

- [ ] All milestone-specific implementation items are complete.
- [ ] All relevant new tests pass.
- [ ] Required Phase-1 regression gates pass.
- [ ] No unresolved race/data-corruption/security issue is knowingly introduced.
- [ ] No secret or accidental generated artifact is staged.
- [ ] Documentation describes current behavior accurately.
- [ ] The milestone has exactly one intended local commit or a clearly documented reason for a split required by repository tooling.

## Mandatory milestone report format

```text
STATUS: DONE or BLOCKED
MILESTONE: exact milestone number and title
BASE_SHA: SHA before milestone
END_SHA: SHA after commit when DONE
WHAT_I_INSPECTED: files/docs/commands read before changes
WHAT_CHANGED: concise implementation summary
FILES_CHANGED: exact paths
TESTS: command -> result
PHASE1_REGRESSION: command -> result or N/A with reason
CPP_VALIDATION: command -> result or N/A
DISTRIBUTED_VALIDATION: command -> result or N/A
METRICS_CAPTURED: actual measured metrics only; otherwise `None`
HUMAN_ACTION: None or exact required action
KNOWN_LIMITATIONS: explicit remaining limitations
COMMIT: commit SHA + subject
NEXT: next milestone number; continue automatically if not blocked
```

---

# MILESTONE 16 — Define the shared Protobuf/gRPC execution contract

## Objective

Create a versioned language-neutral control contract before networking code.

## Why this milestone exists

- This milestone isolates **define the shared protobuf/grpc execution contract** so failures are attributable and reviewable.
- The milestone is intentionally narrower than the final Phase-2 architecture.
- Do not implement later milestone responsibilities early unless a tiny interface stub is required for compilation.
- Any stub must be clearly marked as non-production and must not be described as a completed feature.

## Mandatory pre-read and repository inspection

- Read `AGENTS.md` and obey repository-local instructions relevant to files you will touch.
- Run `git status --short` and confirm no unrelated user work will be overwritten.
- Read every existing source file that the milestone will modify before editing it.
- Read the nearest tests covering the behavior before changing implementation.
- Inspect current package/library versions rather than assuming APIs from memory.
- Consult current official documentation before using a third-party API that may have changed.

## Entry conditions

- Milestone 15 is DONE, unless this is Milestone 01.
- Working tree contains no unexpected unrelated modifications.
- The current Phase-1 baseline gate is known and available.
- Any dependency introduced in this milestone has a verified version/source and license acceptable for the repository.

## Detailed implementation procedure

1. Create protocol messages for SubmitRun, CancelRun, GetRun, health, and event/status queries or streams.
2. Define TaskEnvelope and ResultEnvelope concepts separately from the control RPCs even if Redis transports them later.
3. Include run ID, workflow version ID, org ID, node ID, attempt number, resource class, affinity key, trace/correlation IDs, and timestamps where appropriate.
4. Do not put secrets or full auth tokens inside persisted task payloads.
5. Version the contract explicitly.
6. Define enum values for statuses and error classes.
7. Generate C++ and TypeScript bindings reproducibly.
8. Add compatibility/golden serialization tests.
9. Before editing, write a short local plan listing the exact files expected to change.
10. Prefer the smallest interface that satisfies this milestone and leaves room for later milestones.
11. Keep names consistent with existing repository conventions; do not introduce a parallel naming universe.
12. If an existing helper already implements part of the behavior, reuse it rather than creating a near-duplicate.
13. Keep product-facing behavior backwards compatible unless the milestone explicitly introduces a new opt-in Evo path.
14. When behavior crosses a trust boundary, validate input before mutating durable state.
15. When behavior crosses a thread/process boundary, define ownership, lifetime, timeout, and failure semantics explicitly.
16. When behavior emits a timestamp, state whether it is wall-clock or steady-clock and use it consistently.
17. Add tests in the same milestone as the behavior; do not defer all testing to Milestone 39.
18. Update `docs/phase2/PROGRESS.md` with the observed result, not a future-tense promise.

## Phase-1 preservation checklist

- [ ] AI planner still requires explicit user Run before execution.
- [ ] Generated graph remains editable React Flow / Liveblocks state.
- [ ] Legacy Trigger.dev engine remains available unless a later explicit final decision says otherwise.
- [ ] Clerk auth/org enforcement remains server-side.
- [ ] Agent Pro-plan gate remains fail-closed.
- [ ] Live browser credentials remain server/worker only.
- [ ] Legacy Run/Stop/results/replay/rerun behavior must not regress.
- [ ] Existing node registry/executors are not duplicated without a compelling reason.
- [ ] No test is removed merely because Phase 2 changes an internal representation.

## Concurrency / distributed correctness questions to answer

- [ ] Which component owns the mutable state introduced here?
- [ ] Can two threads/processes modify it concurrently?
- [ ] What lock/transaction/unique constraint protects the invariant?
- [ ] What happens if an event/message is delivered twice?
- [ ] What happens if the process crashes after the first durable write but before the second?
- [ ] What happens when cancellation races completion?
- [ ] What happens when shutdown starts while another thread is waiting?
- [ ] What happens when a worker is slow rather than dead?
- [ ] Does this change browser-session affinity or ownership?
- [ ] Can a late result overwrite a newer logical state?

## Negative / adversarial scenarios

- [ ] Exercise or explicitly mark N/A: invalid/empty identifiers.
- [ ] Exercise or explicitly mark N/A: duplicate request/event.
- [ ] Exercise or explicitly mark N/A: unknown enum or node type.
- [ ] Exercise or explicitly mark N/A: timeout at external boundary.
- [ ] Exercise or explicitly mark N/A: cancellation at an inconvenient moment.
- [ ] Exercise or explicitly mark N/A: process shutdown while work is pending.
- [ ] Exercise or explicitly mark N/A: stale data or stale attempt result.
- [ ] Exercise or explicitly mark N/A: tenant mismatch / unauthorized request when relevant.
- [ ] Exercise or explicitly mark N/A: resource exhaustion / queue full when relevant.
- [ ] Exercise or explicitly mark N/A: malformed serialized payload when relevant.

## Explicit no-go items for this milestone

- Do not expose React Flow UI coordinates to the scheduler protocol.
- Do not put browser API keys in protobuf.
- Do not invent performance numbers.
- Do not mark a future component as implemented.
- Do not silently change Phase-1 default behavior.
- Do not commit secrets or local credential values.

## Required validation

- [ ] `proto generation`
- [ ] `C++ compile`
- [ ] `TS typecheck`
- [ ] `golden round-trip test`
- [ ] Run current Phase-1 `npm test` when production app code is touched.
- [ ] Run `npm run typecheck` when TypeScript is touched.
- [ ] Run `npm run lint` when TypeScript/React is touched.
- [ ] Run CMake/CTest when C++ is touched.
- [ ] Run production build at integration/high-risk milestones.

## Performance evidence rules for this milestone

- If this milestone is not explicitly a benchmark milestone, do not add a resume number just because a timer was printed.
- If timing is collected for a test, label it diagnostic unless benchmark methodology requirements are satisfied.
- If this milestone changes a previously benchmarked hot path, note that old results are no longer final for the new SHA.
- Store benchmark raw data only in the defined benchmark result structure, not ad-hoc README prose.
- A result must identify workload, build mode, hardware, sample count, and commit before it becomes evidence-grade.

## Human intervention triggers

- If A required secret/account is missing. -> report `STATUS: BLOCKED` with exact manual action; do not guess.
- If A system-level prerequisite such as Docker/CMake/compiler requires user installation. -> report `STATUS: BLOCKED` with exact manual action; do not guess.
- If A shared/remote database migration would be applied. -> report `STATUS: BLOCKED` with exact manual action; do not guess.
- If Git author configuration is missing. -> report `STATUS: BLOCKED` with exact manual action; do not guess.
- If A destructive operation would touch user data or history. -> report `STATUS: BLOCKED` with exact manual action; do not guess.
- If A paid external test would incur nontrivial cost and the user has not already authorized it. -> report `STATUS: BLOCKED` with exact manual action; do not guess.

## Diff-review checklist before commit

- [ ] Review every changed file with `git diff`.
- [ ] Run `git diff --check`.
- [ ] Remove accidental formatting churn outside the milestone scope.
- [ ] Confirm no secret or `.env.local` value appears in the diff.
- [ ] Confirm no generated build tree, node_modules, vcpkg cache, Docker volume, or large transient trace has been staged.
- [ ] Confirm comments/README wording describe implemented behavior, not planned behavior.

## Commit requirement

Create one local commit with subject:

`phase2(m16): define versioned grpc protocol`

After commit:

- Record the new SHA in `docs/phase2/PROGRESS.md`.
- Record commands/results for the milestone.
- Do not push unless push permission/policy has already been explicitly authorized.
- If all exit criteria are green, proceed automatically to the next milestone.

## Exit criteria

- [ ] All milestone-specific implementation items are complete.
- [ ] All relevant new tests pass.
- [ ] Required Phase-1 regression gates pass.
- [ ] No unresolved race/data-corruption/security issue is knowingly introduced.
- [ ] No secret or accidental generated artifact is staged.
- [ ] Documentation describes current behavior accurately.
- [ ] The milestone has exactly one intended local commit or a clearly documented reason for a split required by repository tooling.

## Mandatory milestone report format

```text
STATUS: DONE or BLOCKED
MILESTONE: exact milestone number and title
BASE_SHA: SHA before milestone
END_SHA: SHA after commit when DONE
WHAT_I_INSPECTED: files/docs/commands read before changes
WHAT_CHANGED: concise implementation summary
FILES_CHANGED: exact paths
TESTS: command -> result
PHASE1_REGRESSION: command -> result or N/A with reason
CPP_VALIDATION: command -> result or N/A
DISTRIBUTED_VALIDATION: command -> result or N/A
METRICS_CAPTURED: actual measured metrics only; otherwise `None`
HUMAN_ACTION: None or exact required action
KNOWN_LIMITATIONS: explicit remaining limitations
COMMIT: commit SHA + subject
NEXT: next milestone number; continue automatically if not blocked
```

---

# MILESTONE 17 — Implement the C++ scheduler service over gRPC

## Objective

Make the scheduler a real process with a stable service boundary.

## Why this milestone exists

- This milestone isolates **implement the c++ scheduler service over grpc** so failures are attributable and reviewable.
- The milestone is intentionally narrower than the final Phase-2 architecture.
- Do not implement later milestone responsibilities early unless a tiny interface stub is required for compilation.
- Any stub must be clearly marked as non-production and must not be described as a completed feature.

## Mandatory pre-read and repository inspection

- Read `AGENTS.md` and obey repository-local instructions relevant to files you will touch.
- Run `git status --short` and confirm no unrelated user work will be overwritten.
- Read every existing source file that the milestone will modify before editing it.
- Read the nearest tests covering the behavior before changing implementation.
- Inspect current package/library versions rather than assuming APIs from memory.
- Consult current official documentation before using a third-party API that may have changed.

## Entry conditions

- Milestone 16 is DONE, unless this is Milestone 01.
- Working tree contains no unexpected unrelated modifications.
- The current Phase-1 baseline gate is known and available.
- Any dependency introduced in this milestone has a verified version/source and license acceptable for the repository.

## Detailed implementation procedure

1. Create a server executable wrapping the scheduler core.
2. Implement health/readiness behavior.
3. Implement SubmitRun validation and idempotent submission by run ID.
4. Implement CancelRun and GetRun for in-memory/local mode first.
5. Add structured logs with run/org identifiers but no secrets.
6. Add configurable listen address and safe local default.
7. Add graceful process shutdown on signals.
8. Add integration tests that start the service, submit a synthetic DAG, query status, cancel a run, and shut down cleanly.
9. Before editing, write a short local plan listing the exact files expected to change.
10. Prefer the smallest interface that satisfies this milestone and leaves room for later milestones.
11. Keep names consistent with existing repository conventions; do not introduce a parallel naming universe.
12. If an existing helper already implements part of the behavior, reuse it rather than creating a near-duplicate.
13. Keep product-facing behavior backwards compatible unless the milestone explicitly introduces a new opt-in Evo path.
14. When behavior crosses a trust boundary, validate input before mutating durable state.
15. When behavior crosses a thread/process boundary, define ownership, lifetime, timeout, and failure semantics explicitly.
16. When behavior emits a timestamp, state whether it is wall-clock or steady-clock and use it consistently.
17. Add tests in the same milestone as the behavior; do not defer all testing to Milestone 39.
18. Update `docs/phase2/PROGRESS.md` with the observed result, not a future-tense promise.

## Phase-1 preservation checklist

- [ ] AI planner still requires explicit user Run before execution.
- [ ] Generated graph remains editable React Flow / Liveblocks state.
- [ ] Legacy Trigger.dev engine remains available unless a later explicit final decision says otherwise.
- [ ] Clerk auth/org enforcement remains server-side.
- [ ] Agent Pro-plan gate remains fail-closed.
- [ ] Live browser credentials remain server/worker only.
- [ ] Legacy Run/Stop/results/replay/rerun behavior must not regress.
- [ ] Existing node registry/executors are not duplicated without a compelling reason.
- [ ] No test is removed merely because Phase 2 changes an internal representation.

## Concurrency / distributed correctness questions to answer

- [ ] Which component owns the mutable state introduced here?
- [ ] Can two threads/processes modify it concurrently?
- [ ] What lock/transaction/unique constraint protects the invariant?
- [ ] What happens if an event/message is delivered twice?
- [ ] What happens if the process crashes after the first durable write but before the second?
- [ ] What happens when cancellation races completion?
- [ ] What happens when shutdown starts while another thread is waiting?
- [ ] What happens when a worker is slow rather than dead?
- [ ] Does this change browser-session affinity or ownership?
- [ ] Can a late result overwrite a newer logical state?

## Negative / adversarial scenarios

- [ ] Exercise or explicitly mark N/A: invalid/empty identifiers.
- [ ] Exercise or explicitly mark N/A: duplicate request/event.
- [ ] Exercise or explicitly mark N/A: unknown enum or node type.
- [ ] Exercise or explicitly mark N/A: timeout at external boundary.
- [ ] Exercise or explicitly mark N/A: cancellation at an inconvenient moment.
- [ ] Exercise or explicitly mark N/A: process shutdown while work is pending.
- [ ] Exercise or explicitly mark N/A: stale data or stale attempt result.
- [ ] Exercise or explicitly mark N/A: tenant mismatch / unauthorized request when relevant.
- [ ] Exercise or explicitly mark N/A: resource exhaustion / queue full when relevant.
- [ ] Exercise or explicitly mark N/A: malformed serialized payload when relevant.

## Explicit no-go items for this milestone

- Do not integrate the Next.js app yet.
- Do not expose unauthenticated scheduler ports beyond local/dev by default.
- Do not invent performance numbers.
- Do not mark a future component as implemented.
- Do not silently change Phase-1 default behavior.
- Do not commit secrets or local credential values.

## Required validation

- [ ] `gRPC integration tests`
- [ ] `process shutdown test`
- [ ] `Phase-1 regression`
- [ ] Run current Phase-1 `npm test` when production app code is touched.
- [ ] Run `npm run typecheck` when TypeScript is touched.
- [ ] Run `npm run lint` when TypeScript/React is touched.
- [ ] Run CMake/CTest when C++ is touched.
- [ ] Run production build at integration/high-risk milestones.

## Performance evidence rules for this milestone

- If this milestone is not explicitly a benchmark milestone, do not add a resume number just because a timer was printed.
- If timing is collected for a test, label it diagnostic unless benchmark methodology requirements are satisfied.
- If this milestone changes a previously benchmarked hot path, note that old results are no longer final for the new SHA.
- Store benchmark raw data only in the defined benchmark result structure, not ad-hoc README prose.
- A result must identify workload, build mode, hardware, sample count, and commit before it becomes evidence-grade.

## Human intervention triggers

- If A required secret/account is missing. -> report `STATUS: BLOCKED` with exact manual action; do not guess.
- If A system-level prerequisite such as Docker/CMake/compiler requires user installation. -> report `STATUS: BLOCKED` with exact manual action; do not guess.
- If A shared/remote database migration would be applied. -> report `STATUS: BLOCKED` with exact manual action; do not guess.
- If Git author configuration is missing. -> report `STATUS: BLOCKED` with exact manual action; do not guess.
- If A destructive operation would touch user data or history. -> report `STATUS: BLOCKED` with exact manual action; do not guess.
- If A paid external test would incur nontrivial cost and the user has not already authorized it. -> report `STATUS: BLOCKED` with exact manual action; do not guess.

## Diff-review checklist before commit

- [ ] Review every changed file with `git diff`.
- [ ] Run `git diff --check`.
- [ ] Remove accidental formatting churn outside the milestone scope.
- [ ] Confirm no secret or `.env.local` value appears in the diff.
- [ ] Confirm no generated build tree, node_modules, vcpkg cache, Docker volume, or large transient trace has been staged.
- [ ] Confirm comments/README wording describe implemented behavior, not planned behavior.

## Commit requirement

Create one local commit with subject:

`phase2(m17): expose c++ scheduler grpc service`

After commit:

- Record the new SHA in `docs/phase2/PROGRESS.md`.
- Record commands/results for the milestone.
- Do not push unless push permission/policy has already been explicitly authorized.
- If all exit criteria are green, proceed automatically to the next milestone.

## Exit criteria

- [ ] All milestone-specific implementation items are complete.
- [ ] All relevant new tests pass.
- [ ] Required Phase-1 regression gates pass.
- [ ] No unresolved race/data-corruption/security issue is knowingly introduced.
- [ ] No secret or accidental generated artifact is staged.
- [ ] Documentation describes current behavior accurately.
- [ ] The milestone has exactly one intended local commit or a clearly documented reason for a split required by repository tooling.

## Mandatory milestone report format

```text
STATUS: DONE or BLOCKED
MILESTONE: exact milestone number and title
BASE_SHA: SHA before milestone
END_SHA: SHA after commit when DONE
WHAT_I_INSPECTED: files/docs/commands read before changes
WHAT_CHANGED: concise implementation summary
FILES_CHANGED: exact paths
TESTS: command -> result
PHASE1_REGRESSION: command -> result or N/A with reason
CPP_VALIDATION: command -> result or N/A
DISTRIBUTED_VALIDATION: command -> result or N/A
METRICS_CAPTURED: actual measured metrics only; otherwise `None`
HUMAN_ACTION: None or exact required action
KNOWN_LIMITATIONS: explicit remaining limitations
COMMIT: commit SHA + subject
NEXT: next milestone number; continue automatically if not blocked
```

---

# MILESTONE 18 — Create isolated local Redis + PostgreSQL infrastructure

## Objective

Make distributed integration reproducible without touching the user's cloud database.

## Why this milestone exists

- This milestone isolates **create isolated local redis + postgresql infrastructure** so failures are attributable and reviewable.
- The milestone is intentionally narrower than the final Phase-2 architecture.
- Do not implement later milestone responsibilities early unless a tiny interface stub is required for compilation.
- Any stub must be clearly marked as non-production and must not be described as a completed feature.

## Mandatory pre-read and repository inspection

- Read `AGENTS.md` and obey repository-local instructions relevant to files you will touch.
- Run `git status --short` and confirm no unrelated user work will be overwritten.
- Read every existing source file that the milestone will modify before editing it.
- Read the nearest tests covering the behavior before changing implementation.
- Inspect current package/library versions rather than assuming APIs from memory.
- Consult current official documentation before using a third-party API that may have changed.

## Entry conditions

- Milestone 17 is DONE, unless this is Milestone 01.
- Working tree contains no unexpected unrelated modifications.
- The current Phase-1 baseline gate is known and available.
- Any dependency introduced in this milestone has a verified version/source and license acceptable for the repository.

## Detailed implementation procedure

1. Create Phase-2 Docker Compose infrastructure for Redis and PostgreSQL used by integration tests.
2. Use local test credentials committed only as non-secret development defaults suitable for local containers.
3. Keep the existing Neon configuration intact for the application.
4. Add health checks and deterministic service names/ports configurable through env vars.
5. Add scripts to start/stop/reset only the Phase-2 local infrastructure.
6. Do not run destructive commands against `DATABASE_URL` or Neon in this milestone.
7. Document how the scheduler will later use the same schema against standard Postgres/Neon connections.
8. Before editing, write a short local plan listing the exact files expected to change.
9. Prefer the smallest interface that satisfies this milestone and leaves room for later milestones.
10. Keep names consistent with existing repository conventions; do not introduce a parallel naming universe.
11. If an existing helper already implements part of the behavior, reuse it rather than creating a near-duplicate.
12. Keep product-facing behavior backwards compatible unless the milestone explicitly introduces a new opt-in Evo path.
13. When behavior crosses a trust boundary, validate input before mutating durable state.
14. When behavior crosses a thread/process boundary, define ownership, lifetime, timeout, and failure semantics explicitly.
15. When behavior emits a timestamp, state whether it is wall-clock or steady-clock and use it consistently.
16. Add tests in the same milestone as the behavior; do not defer all testing to Milestone 39.
17. Update `docs/phase2/PROGRESS.md` with the observed result, not a future-tense promise.

## Phase-1 preservation checklist

- [ ] AI planner still requires explicit user Run before execution.
- [ ] Generated graph remains editable React Flow / Liveblocks state.
- [ ] Legacy Trigger.dev engine remains available unless a later explicit final decision says otherwise.
- [ ] Clerk auth/org enforcement remains server-side.
- [ ] Agent Pro-plan gate remains fail-closed.
- [ ] Live browser credentials remain server/worker only.
- [ ] Legacy Run/Stop/results/replay/rerun behavior must not regress.
- [ ] Existing node registry/executors are not duplicated without a compelling reason.
- [ ] No test is removed merely because Phase 2 changes an internal representation.

## Concurrency / distributed correctness questions to answer

- [ ] Which component owns the mutable state introduced here?
- [ ] Can two threads/processes modify it concurrently?
- [ ] What lock/transaction/unique constraint protects the invariant?
- [ ] What happens if an event/message is delivered twice?
- [ ] What happens if the process crashes after the first durable write but before the second?
- [ ] What happens when cancellation races completion?
- [ ] What happens when shutdown starts while another thread is waiting?
- [ ] What happens when a worker is slow rather than dead?
- [ ] Does this change browser-session affinity or ownership?
- [ ] Can a late result overwrite a newer logical state?

## Negative / adversarial scenarios

- [ ] Exercise or explicitly mark N/A: invalid/empty identifiers.
- [ ] Exercise or explicitly mark N/A: duplicate request/event.
- [ ] Exercise or explicitly mark N/A: unknown enum or node type.
- [ ] Exercise or explicitly mark N/A: timeout at external boundary.
- [ ] Exercise or explicitly mark N/A: cancellation at an inconvenient moment.
- [ ] Exercise or explicitly mark N/A: process shutdown while work is pending.
- [ ] Exercise or explicitly mark N/A: stale data or stale attempt result.
- [ ] Exercise or explicitly mark N/A: tenant mismatch / unauthorized request when relevant.
- [ ] Exercise or explicitly mark N/A: resource exhaustion / queue full when relevant.
- [ ] Exercise or explicitly mark N/A: malformed serialized payload when relevant.

## Explicit no-go items for this milestone

- If Docker is unavailable, stop with exact installation requirement.
- Never point reset scripts at a remote database.
- Do not invent performance numbers.
- Do not mark a future component as implemented.
- Do not silently change Phase-1 default behavior.
- Do not commit secrets or local credential values.

## Required validation

- [ ] `docker compose config validation`
- [ ] `Redis ping`
- [ ] `Postgres connectivity`
- [ ] `clean shutdown`
- [ ] Run current Phase-1 `npm test` when production app code is touched.
- [ ] Run `npm run typecheck` when TypeScript is touched.
- [ ] Run `npm run lint` when TypeScript/React is touched.
- [ ] Run CMake/CTest when C++ is touched.
- [ ] Run production build at integration/high-risk milestones.

## Performance evidence rules for this milestone

- If this milestone is not explicitly a benchmark milestone, do not add a resume number just because a timer was printed.
- If timing is collected for a test, label it diagnostic unless benchmark methodology requirements are satisfied.
- If this milestone changes a previously benchmarked hot path, note that old results are no longer final for the new SHA.
- Store benchmark raw data only in the defined benchmark result structure, not ad-hoc README prose.
- A result must identify workload, build mode, hardware, sample count, and commit before it becomes evidence-grade.

## Human intervention triggers

- If A required secret/account is missing. -> report `STATUS: BLOCKED` with exact manual action; do not guess.
- If A system-level prerequisite such as Docker/CMake/compiler requires user installation. -> report `STATUS: BLOCKED` with exact manual action; do not guess.
- If A shared/remote database migration would be applied. -> report `STATUS: BLOCKED` with exact manual action; do not guess.
- If Git author configuration is missing. -> report `STATUS: BLOCKED` with exact manual action; do not guess.
- If A destructive operation would touch user data or history. -> report `STATUS: BLOCKED` with exact manual action; do not guess.
- If A paid external test would incur nontrivial cost and the user has not already authorized it. -> report `STATUS: BLOCKED` with exact manual action; do not guess.

## Diff-review checklist before commit

- [ ] Review every changed file with `git diff`.
- [ ] Run `git diff --check`.
- [ ] Remove accidental formatting churn outside the milestone scope.
- [ ] Confirm no secret or `.env.local` value appears in the diff.
- [ ] Confirm no generated build tree, node_modules, vcpkg cache, Docker volume, or large transient trace has been staged.
- [ ] Confirm comments/README wording describe implemented behavior, not planned behavior.

## Commit requirement

Create one local commit with subject:

`phase2(m18): add isolated redis postgres dev stack`

After commit:

- Record the new SHA in `docs/phase2/PROGRESS.md`.
- Record commands/results for the milestone.
- Do not push unless push permission/policy has already been explicitly authorized.
- If all exit criteria are green, proceed automatically to the next milestone.

## Exit criteria

- [ ] All milestone-specific implementation items are complete.
- [ ] All relevant new tests pass.
- [ ] Required Phase-1 regression gates pass.
- [ ] No unresolved race/data-corruption/security issue is knowingly introduced.
- [ ] No secret or accidental generated artifact is staged.
- [ ] Documentation describes current behavior accurately.
- [ ] The milestone has exactly one intended local commit or a clearly documented reason for a split required by repository tooling.

## Mandatory milestone report format

```text
STATUS: DONE or BLOCKED
MILESTONE: exact milestone number and title
BASE_SHA: SHA before milestone
END_SHA: SHA after commit when DONE
WHAT_I_INSPECTED: files/docs/commands read before changes
WHAT_CHANGED: concise implementation summary
FILES_CHANGED: exact paths
TESTS: command -> result
PHASE1_REGRESSION: command -> result or N/A with reason
CPP_VALIDATION: command -> result or N/A
DISTRIBUTED_VALIDATION: command -> result or N/A
METRICS_CAPTURED: actual measured metrics only; otherwise `None`
HUMAN_ACTION: None or exact required action
KNOWN_LIMITATIONS: explicit remaining limitations
COMMIT: commit SHA + subject
NEXT: next milestone number; continue automatically if not blocked
```

---

# MILESTONE 19 — Add additive Phase-2 Drizzle schema and migrations

## Objective

Create durable engine-neutral audit state without breaking Phase 1.

## Why this milestone exists

- This milestone isolates **add additive phase-2 drizzle schema and migrations** so failures are attributable and reviewable.
- The milestone is intentionally narrower than the final Phase-2 architecture.
- Do not implement later milestone responsibilities early unless a tiny interface stub is required for compilation.
- Any stub must be clearly marked as non-production and must not be described as a completed feature.

## Mandatory pre-read and repository inspection

- Read `AGENTS.md` and obey repository-local instructions relevant to files you will touch.
- Run `git status --short` and confirm no unrelated user work will be overwritten.
- Read every existing source file that the milestone will modify before editing it.
- Read the nearest tests covering the behavior before changing implementation.
- Inspect current package/library versions rather than assuming APIs from memory.
- Consult current official documentation before using a third-party API that may have changed.

## Entry conditions

- Milestone 18 is DONE, unless this is Milestone 01.
- Working tree contains no unexpected unrelated modifications.
- The current Phase-1 baseline gate is known and available.
- Any dependency introduced in this milestone has a verified version/source and license acceptable for the repository.

## Detailed implementation procedure

1. Design additive tables for workflow versions, workflow runs, node runs, task attempts, and idempotency records.
2. Reuse existing workflows/live-view/run-artifact tables where sensible rather than duplicating them blindly.
3. Add engine discriminator fields so legacy and Evo runs can coexist.
4. Use immutable workflow-version graph snapshots and optional graph hash/version number.
5. Use indexes for org/workflow/run/status queries required by the engine.
6. Use unique constraints for run/node identity and attempt numbering where appropriate.
7. Generate a Drizzle migration.
8. Apply and test migration on local Docker Postgres only.
9. Verify legacy Phase-1 application paths still compile and tests pass.
10. Before editing, write a short local plan listing the exact files expected to change.
11. Prefer the smallest interface that satisfies this milestone and leaves room for later milestones.
12. Keep names consistent with existing repository conventions; do not introduce a parallel naming universe.
13. If an existing helper already implements part of the behavior, reuse it rather than creating a near-duplicate.
14. Keep product-facing behavior backwards compatible unless the milestone explicitly introduces a new opt-in Evo path.
15. When behavior crosses a trust boundary, validate input before mutating durable state.
16. When behavior crosses a thread/process boundary, define ownership, lifetime, timeout, and failure semantics explicitly.
17. When behavior emits a timestamp, state whether it is wall-clock or steady-clock and use it consistently.
18. Add tests in the same milestone as the behavior; do not defer all testing to Milestone 39.
19. Update `docs/phase2/PROGRESS.md` with the observed result, not a future-tense promise.

## Phase-1 preservation checklist

- [ ] AI planner still requires explicit user Run before execution.
- [ ] Generated graph remains editable React Flow / Liveblocks state.
- [ ] Legacy Trigger.dev engine remains available unless a later explicit final decision says otherwise.
- [ ] Clerk auth/org enforcement remains server-side.
- [ ] Agent Pro-plan gate remains fail-closed.
- [ ] Live browser credentials remain server/worker only.
- [ ] Legacy Run/Stop/results/replay/rerun behavior must not regress.
- [ ] Existing node registry/executors are not duplicated without a compelling reason.
- [ ] No test is removed merely because Phase 2 changes an internal representation.

## Concurrency / distributed correctness questions to answer

- [ ] Which component owns the mutable state introduced here?
- [ ] Can two threads/processes modify it concurrently?
- [ ] What lock/transaction/unique constraint protects the invariant?
- [ ] What happens if an event/message is delivered twice?
- [ ] What happens if the process crashes after the first durable write but before the second?
- [ ] What happens when cancellation races completion?
- [ ] What happens when shutdown starts while another thread is waiting?
- [ ] What happens when a worker is slow rather than dead?
- [ ] Does this change browser-session affinity or ownership?
- [ ] Can a late result overwrite a newer logical state?

## Negative / adversarial scenarios

- [ ] Exercise or explicitly mark N/A: invalid/empty identifiers.
- [ ] Exercise or explicitly mark N/A: duplicate request/event.
- [ ] Exercise or explicitly mark N/A: unknown enum or node type.
- [ ] Exercise or explicitly mark N/A: timeout at external boundary.
- [ ] Exercise or explicitly mark N/A: cancellation at an inconvenient moment.
- [ ] Exercise or explicitly mark N/A: process shutdown while work is pending.
- [ ] Exercise or explicitly mark N/A: stale data or stale attempt result.
- [ ] Exercise or explicitly mark N/A: tenant mismatch / unauthorized request when relevant.
- [ ] Exercise or explicitly mark N/A: resource exhaustion / queue full when relevant.
- [ ] Exercise or explicitly mark N/A: malformed serialized payload when relevant.

## Explicit no-go items for this milestone

- Do not apply migration to shared Neon without explicit user approval.
- Do not remove existing columns/tables.
- Do not invent performance numbers.
- Do not mark a future component as implemented.
- Do not silently change Phase-1 default behavior.
- Do not commit secrets or local credential values.

## Required validation

- [ ] `Drizzle migration generation`
- [ ] `local migration up`
- [ ] `schema query smoke`
- [ ] `Phase-1 tests/build`
- [ ] Run current Phase-1 `npm test` when production app code is touched.
- [ ] Run `npm run typecheck` when TypeScript is touched.
- [ ] Run `npm run lint` when TypeScript/React is touched.
- [ ] Run CMake/CTest when C++ is touched.
- [ ] Run production build at integration/high-risk milestones.

## Performance evidence rules for this milestone

- If this milestone is not explicitly a benchmark milestone, do not add a resume number just because a timer was printed.
- If timing is collected for a test, label it diagnostic unless benchmark methodology requirements are satisfied.
- If this milestone changes a previously benchmarked hot path, note that old results are no longer final for the new SHA.
- Store benchmark raw data only in the defined benchmark result structure, not ad-hoc README prose.
- A result must identify workload, build mode, hardware, sample count, and commit before it becomes evidence-grade.

## Human intervention triggers

- If A required secret/account is missing. -> report `STATUS: BLOCKED` with exact manual action; do not guess.
- If A system-level prerequisite such as Docker/CMake/compiler requires user installation. -> report `STATUS: BLOCKED` with exact manual action; do not guess.
- If A shared/remote database migration would be applied. -> report `STATUS: BLOCKED` with exact manual action; do not guess.
- If Git author configuration is missing. -> report `STATUS: BLOCKED` with exact manual action; do not guess.
- If A destructive operation would touch user data or history. -> report `STATUS: BLOCKED` with exact manual action; do not guess.
- If A paid external test would incur nontrivial cost and the user has not already authorized it. -> report `STATUS: BLOCKED` with exact manual action; do not guess.

## Diff-review checklist before commit

- [ ] Review every changed file with `git diff`.
- [ ] Run `git diff --check`.
- [ ] Remove accidental formatting churn outside the milestone scope.
- [ ] Confirm no secret or `.env.local` value appears in the diff.
- [ ] Confirm no generated build tree, node_modules, vcpkg cache, Docker volume, or large transient trace has been staged.
- [ ] Confirm comments/README wording describe implemented behavior, not planned behavior.

## Commit requirement

Create one local commit with subject:

`phase2(m19): add durable phase2 run schema`

After commit:

- Record the new SHA in `docs/phase2/PROGRESS.md`.
- Record commands/results for the milestone.
- Do not push unless push permission/policy has already been explicitly authorized.
- If all exit criteria are green, proceed automatically to the next milestone.

## Exit criteria

- [ ] All milestone-specific implementation items are complete.
- [ ] All relevant new tests pass.
- [ ] Required Phase-1 regression gates pass.
- [ ] No unresolved race/data-corruption/security issue is knowingly introduced.
- [ ] No secret or accidental generated artifact is staged.
- [ ] Documentation describes current behavior accurately.
- [ ] The milestone has exactly one intended local commit or a clearly documented reason for a split required by repository tooling.

## Mandatory milestone report format

```text
STATUS: DONE or BLOCKED
MILESTONE: exact milestone number and title
BASE_SHA: SHA before milestone
END_SHA: SHA after commit when DONE
WHAT_I_INSPECTED: files/docs/commands read before changes
WHAT_CHANGED: concise implementation summary
FILES_CHANGED: exact paths
TESTS: command -> result
PHASE1_REGRESSION: command -> result or N/A with reason
CPP_VALIDATION: command -> result or N/A
DISTRIBUTED_VALIDATION: command -> result or N/A
METRICS_CAPTURED: actual measured metrics only; otherwise `None`
HUMAN_ACTION: None or exact required action
KNOWN_LIMITATIONS: explicit remaining limitations
COMMIT: commit SHA + subject
NEXT: next milestone number; continue automatically if not blocked
```

---

# MILESTONE 20 — Implement immutable workflow versions and optimistic concurrency

## Objective

Guarantee a run executes the graph the user approved even while collaborators edit newer state.

## Why this milestone exists

- This milestone isolates **implement immutable workflow versions and optimistic concurrency** so failures are attributable and reviewable.
- The milestone is intentionally narrower than the final Phase-2 architecture.
- Do not implement later milestone responsibilities early unless a tiny interface stub is required for compilation.
- Any stub must be clearly marked as non-production and must not be described as a completed feature.

## Mandatory pre-read and repository inspection

- Read `AGENTS.md` and obey repository-local instructions relevant to files you will touch.
- Run `git status --short` and confirm no unrelated user work will be overwritten.
- Read every existing source file that the milestone will modify before editing it.
- Read the nearest tests covering the behavior before changing implementation.
- Inspect current package/library versions rather than assuming APIs from memory.
- Consult current official documentation before using a third-party API that may have changed.

## Entry conditions

- Milestone 19 is DONE, unless this is Milestone 01.
- Working tree contains no unexpected unrelated modifications.
- The current Phase-1 baseline gate is known and available.
- Any dependency introduced in this milestone has a verified version/source and license acceptable for the repository.

## Detailed implementation procedure

1. Create a workflow version/snapshot during Run submission before either engine executes.
2. Reference the immutable version ID from the engine-neutral run row.
3. Keep current Liveblocks editing behavior unchanged.
4. Add a monotonic version or optimistic concurrency token for canonical workflow saves.
5. Detect stale updates rather than silently overwriting a newer canonical version when the code path uses optimistic concurrency.
6. Ensure rerun after edits creates or references the correct new immutable snapshot.
7. Add tests for concurrent version creation, stale-save conflict, run snapshot immutability, and legacy compatibility.
8. Do not make the user-facing editor unusable when a conflict occurs; surface a clear conflict path.
9. Before editing, write a short local plan listing the exact files expected to change.
10. Prefer the smallest interface that satisfies this milestone and leaves room for later milestones.
11. Keep names consistent with existing repository conventions; do not introduce a parallel naming universe.
12. If an existing helper already implements part of the behavior, reuse it rather than creating a near-duplicate.
13. Keep product-facing behavior backwards compatible unless the milestone explicitly introduces a new opt-in Evo path.
14. When behavior crosses a trust boundary, validate input before mutating durable state.
15. When behavior crosses a thread/process boundary, define ownership, lifetime, timeout, and failure semantics explicitly.
16. When behavior emits a timestamp, state whether it is wall-clock or steady-clock and use it consistently.
17. Add tests in the same milestone as the behavior; do not defer all testing to Milestone 39.
18. Update `docs/phase2/PROGRESS.md` with the observed result, not a future-tense promise.

## Phase-1 preservation checklist

- [ ] AI planner still requires explicit user Run before execution.
- [ ] Generated graph remains editable React Flow / Liveblocks state.
- [ ] Legacy Trigger.dev engine remains available unless a later explicit final decision says otherwise.
- [ ] Clerk auth/org enforcement remains server-side.
- [ ] Agent Pro-plan gate remains fail-closed.
- [ ] Live browser credentials remain server/worker only.
- [ ] Legacy Run/Stop/results/replay/rerun behavior must not regress.
- [ ] Existing node registry/executors are not duplicated without a compelling reason.
- [ ] No test is removed merely because Phase 2 changes an internal representation.

## Concurrency / distributed correctness questions to answer

- [ ] Which component owns the mutable state introduced here?
- [ ] Can two threads/processes modify it concurrently?
- [ ] What lock/transaction/unique constraint protects the invariant?
- [ ] What happens if an event/message is delivered twice?
- [ ] What happens if the process crashes after the first durable write but before the second?
- [ ] What happens when cancellation races completion?
- [ ] What happens when shutdown starts while another thread is waiting?
- [ ] What happens when a worker is slow rather than dead?
- [ ] Does this change browser-session affinity or ownership?
- [ ] Can a late result overwrite a newer logical state?

## Negative / adversarial scenarios

- [ ] Exercise or explicitly mark N/A: invalid/empty identifiers.
- [ ] Exercise or explicitly mark N/A: duplicate request/event.
- [ ] Exercise or explicitly mark N/A: unknown enum or node type.
- [ ] Exercise or explicitly mark N/A: timeout at external boundary.
- [ ] Exercise or explicitly mark N/A: cancellation at an inconvenient moment.
- [ ] Exercise or explicitly mark N/A: process shutdown while work is pending.
- [ ] Exercise or explicitly mark N/A: stale data or stale attempt result.
- [ ] Exercise or explicitly mark N/A: tenant mismatch / unauthorized request when relevant.
- [ ] Exercise or explicitly mark N/A: resource exhaustion / queue full when relevant.
- [ ] Exercise or explicitly mark N/A: malformed serialized payload when relevant.

## Explicit no-go items for this milestone

- The exact Liveblocks CRDT behavior is separate from canonical run snapshot versioning.
- Do not mutate a workflow_version row after creation.
- Do not invent performance numbers.
- Do not mark a future component as implemented.
- Do not silently change Phase-1 default behavior.
- Do not commit secrets or local credential values.

## Required validation

- [ ] `versioning unit/integration tests`
- [ ] `migration tests`
- [ ] `Phase-1 lifecycle regression`
- [ ] Run current Phase-1 `npm test` when production app code is touched.
- [ ] Run `npm run typecheck` when TypeScript is touched.
- [ ] Run `npm run lint` when TypeScript/React is touched.
- [ ] Run CMake/CTest when C++ is touched.
- [ ] Run production build at integration/high-risk milestones.

## Performance evidence rules for this milestone

- If this milestone is not explicitly a benchmark milestone, do not add a resume number just because a timer was printed.
- If timing is collected for a test, label it diagnostic unless benchmark methodology requirements are satisfied.
- If this milestone changes a previously benchmarked hot path, note that old results are no longer final for the new SHA.
- Store benchmark raw data only in the defined benchmark result structure, not ad-hoc README prose.
- A result must identify workload, build mode, hardware, sample count, and commit before it becomes evidence-grade.

## Human intervention triggers

- If A required secret/account is missing. -> report `STATUS: BLOCKED` with exact manual action; do not guess.
- If A system-level prerequisite such as Docker/CMake/compiler requires user installation. -> report `STATUS: BLOCKED` with exact manual action; do not guess.
- If A shared/remote database migration would be applied. -> report `STATUS: BLOCKED` with exact manual action; do not guess.
- If Git author configuration is missing. -> report `STATUS: BLOCKED` with exact manual action; do not guess.
- If A destructive operation would touch user data or history. -> report `STATUS: BLOCKED` with exact manual action; do not guess.
- If A paid external test would incur nontrivial cost and the user has not already authorized it. -> report `STATUS: BLOCKED` with exact manual action; do not guess.

## Diff-review checklist before commit

- [ ] Review every changed file with `git diff`.
- [ ] Run `git diff --check`.
- [ ] Remove accidental formatting churn outside the milestone scope.
- [ ] Confirm no secret or `.env.local` value appears in the diff.
- [ ] Confirm no generated build tree, node_modules, vcpkg cache, Docker volume, or large transient trace has been staged.
- [ ] Confirm comments/README wording describe implemented behavior, not planned behavior.

## Commit requirement

Create one local commit with subject:

`phase2(m20): add immutable workflow run snapshots`

After commit:

- Record the new SHA in `docs/phase2/PROGRESS.md`.
- Record commands/results for the milestone.
- Do not push unless push permission/policy has already been explicitly authorized.
- If all exit criteria are green, proceed automatically to the next milestone.

## Exit criteria

- [ ] All milestone-specific implementation items are complete.
- [ ] All relevant new tests pass.
- [ ] Required Phase-1 regression gates pass.
- [ ] No unresolved race/data-corruption/security issue is knowingly introduced.
- [ ] No secret or accidental generated artifact is staged.
- [ ] Documentation describes current behavior accurately.
- [ ] The milestone has exactly one intended local commit or a clearly documented reason for a split required by repository tooling.

## Mandatory milestone report format

```text
STATUS: DONE or BLOCKED
MILESTONE: exact milestone number and title
BASE_SHA: SHA before milestone
END_SHA: SHA after commit when DONE
WHAT_I_INSPECTED: files/docs/commands read before changes
WHAT_CHANGED: concise implementation summary
FILES_CHANGED: exact paths
TESTS: command -> result
PHASE1_REGRESSION: command -> result or N/A with reason
CPP_VALIDATION: command -> result or N/A
DISTRIBUTED_VALIDATION: command -> result or N/A
METRICS_CAPTURED: actual measured metrics only; otherwise `None`
HUMAN_ACTION: None or exact required action
KNOWN_LIMITATIONS: explicit remaining limitations
COMMIT: commit SHA + subject
NEXT: next milestone number; continue automatically if not blocked
```

---

# MILESTONE 21 — Implement Redis Streams transport in the C++ scheduler

## Objective

Move task dispatch from local callbacks to a durable transport while keeping scheduler logic testable.

## Why this milestone exists

- This milestone isolates **implement redis streams transport in the c++ scheduler** so failures are attributable and reviewable.
- The milestone is intentionally narrower than the final Phase-2 architecture.
- Do not implement later milestone responsibilities early unless a tiny interface stub is required for compilation.
- Any stub must be clearly marked as non-production and must not be described as a completed feature.

## Mandatory pre-read and repository inspection

- Read `AGENTS.md` and obey repository-local instructions relevant to files you will touch.
- Run `git status --short` and confirm no unrelated user work will be overwritten.
- Read every existing source file that the milestone will modify before editing it.
- Read the nearest tests covering the behavior before changing implementation.
- Inspect current package/library versions rather than assuming APIs from memory.
- Consult current official documentation before using a third-party API that may have changed.

## Entry conditions

- Milestone 20 is DONE, unless this is Milestone 01.
- Working tree contains no unexpected unrelated modifications.
- The current Phase-1 baseline gate is known and available.
- Any dependency introduced in this milestone has a verified version/source and license acceptable for the repository.

## Detailed implementation procedure

1. Introduce a transport abstraction so scheduler-core tests can continue using an in-memory fake.
2. Implement Redis-backed task publishing using a verified current C++ Redis client/library.
3. Use namespaced keys/streams with an explicit environment/project prefix.
4. Create task stream(s), result stream, and control/heartbeat structures according to the documented design.
5. Encode TaskEnvelope deterministically.
6. Implement retryable Redis connection logic with bounded backoff.
7. Do not acknowledge work on behalf of workers.
8. Add integration tests against local Redis for enqueue/read/pending/ack behavior and duplicate message IDs/payload handling.
9. Before editing, write a short local plan listing the exact files expected to change.
10. Prefer the smallest interface that satisfies this milestone and leaves room for later milestones.
11. Keep names consistent with existing repository conventions; do not introduce a parallel naming universe.
12. If an existing helper already implements part of the behavior, reuse it rather than creating a near-duplicate.
13. Keep product-facing behavior backwards compatible unless the milestone explicitly introduces a new opt-in Evo path.
14. When behavior crosses a trust boundary, validate input before mutating durable state.
15. When behavior crosses a thread/process boundary, define ownership, lifetime, timeout, and failure semantics explicitly.
16. When behavior emits a timestamp, state whether it is wall-clock or steady-clock and use it consistently.
17. Add tests in the same milestone as the behavior; do not defer all testing to Milestone 39.
18. Update `docs/phase2/PROGRESS.md` with the observed result, not a future-tense promise.

## Phase-1 preservation checklist

- [ ] AI planner still requires explicit user Run before execution.
- [ ] Generated graph remains editable React Flow / Liveblocks state.
- [ ] Legacy Trigger.dev engine remains available unless a later explicit final decision says otherwise.
- [ ] Clerk auth/org enforcement remains server-side.
- [ ] Agent Pro-plan gate remains fail-closed.
- [ ] Live browser credentials remain server/worker only.
- [ ] Legacy Run/Stop/results/replay/rerun behavior must not regress.
- [ ] Existing node registry/executors are not duplicated without a compelling reason.
- [ ] No test is removed merely because Phase 2 changes an internal representation.

## Concurrency / distributed correctness questions to answer

- [ ] Which component owns the mutable state introduced here?
- [ ] Can two threads/processes modify it concurrently?
- [ ] What lock/transaction/unique constraint protects the invariant?
- [ ] What happens if an event/message is delivered twice?
- [ ] What happens if the process crashes after the first durable write but before the second?
- [ ] What happens when cancellation races completion?
- [ ] What happens when shutdown starts while another thread is waiting?
- [ ] What happens when a worker is slow rather than dead?
- [ ] Does this change browser-session affinity or ownership?
- [ ] Can a late result overwrite a newer logical state?

## Negative / adversarial scenarios

- [ ] Exercise or explicitly mark N/A: invalid/empty identifiers.
- [ ] Exercise or explicitly mark N/A: duplicate request/event.
- [ ] Exercise or explicitly mark N/A: unknown enum or node type.
- [ ] Exercise or explicitly mark N/A: timeout at external boundary.
- [ ] Exercise or explicitly mark N/A: cancellation at an inconvenient moment.
- [ ] Exercise or explicitly mark N/A: process shutdown while work is pending.
- [ ] Exercise or explicitly mark N/A: stale data or stale attempt result.
- [ ] Exercise or explicitly mark N/A: tenant mismatch / unauthorized request when relevant.
- [ ] Exercise or explicitly mark N/A: resource exhaustion / queue full when relevant.
- [ ] Exercise or explicitly mark N/A: malformed serialized payload when relevant.

## Explicit no-go items for this milestone

- Do not hard-code remote Redis credentials.
- Do not use Redis Pub/Sub as the sole durable task transport.
- Do not invent performance numbers.
- Do not mark a future component as implemented.
- Do not silently change Phase-1 default behavior.
- Do not commit secrets or local credential values.

## Required validation

- [ ] `Redis integration tests`
- [ ] `restart Redis during a controlled test`
- [ ] `core fake transport tests remain green`
- [ ] Run current Phase-1 `npm test` when production app code is touched.
- [ ] Run `npm run typecheck` when TypeScript is touched.
- [ ] Run `npm run lint` when TypeScript/React is touched.
- [ ] Run CMake/CTest when C++ is touched.
- [ ] Run production build at integration/high-risk milestones.

## Performance evidence rules for this milestone

- If this milestone is not explicitly a benchmark milestone, do not add a resume number just because a timer was printed.
- If timing is collected for a test, label it diagnostic unless benchmark methodology requirements are satisfied.
- If this milestone changes a previously benchmarked hot path, note that old results are no longer final for the new SHA.
- Store benchmark raw data only in the defined benchmark result structure, not ad-hoc README prose.
- A result must identify workload, build mode, hardware, sample count, and commit before it becomes evidence-grade.

## Human intervention triggers

- If A required secret/account is missing. -> report `STATUS: BLOCKED` with exact manual action; do not guess.
- If A system-level prerequisite such as Docker/CMake/compiler requires user installation. -> report `STATUS: BLOCKED` with exact manual action; do not guess.
- If A shared/remote database migration would be applied. -> report `STATUS: BLOCKED` with exact manual action; do not guess.
- If Git author configuration is missing. -> report `STATUS: BLOCKED` with exact manual action; do not guess.
- If A destructive operation would touch user data or history. -> report `STATUS: BLOCKED` with exact manual action; do not guess.
- If A paid external test would incur nontrivial cost and the user has not already authorized it. -> report `STATUS: BLOCKED` with exact manual action; do not guess.

## Diff-review checklist before commit

- [ ] Review every changed file with `git diff`.
- [ ] Run `git diff --check`.
- [ ] Remove accidental formatting churn outside the milestone scope.
- [ ] Confirm no secret or `.env.local` value appears in the diff.
- [ ] Confirm no generated build tree, node_modules, vcpkg cache, Docker volume, or large transient trace has been staged.
- [ ] Confirm comments/README wording describe implemented behavior, not planned behavior.

## Commit requirement

Create one local commit with subject:

`phase2(m21): add redis streams task transport`

After commit:

- Record the new SHA in `docs/phase2/PROGRESS.md`.
- Record commands/results for the milestone.
- Do not push unless push permission/policy has already been explicitly authorized.
- If all exit criteria are green, proceed automatically to the next milestone.

## Exit criteria

- [ ] All milestone-specific implementation items are complete.
- [ ] All relevant new tests pass.
- [ ] Required Phase-1 regression gates pass.
- [ ] No unresolved race/data-corruption/security issue is knowingly introduced.
- [ ] No secret or accidental generated artifact is staged.
- [ ] Documentation describes current behavior accurately.
- [ ] The milestone has exactly one intended local commit or a clearly documented reason for a split required by repository tooling.

## Mandatory milestone report format

```text
STATUS: DONE or BLOCKED
MILESTONE: exact milestone number and title
BASE_SHA: SHA before milestone
END_SHA: SHA after commit when DONE
WHAT_I_INSPECTED: files/docs/commands read before changes
WHAT_CHANGED: concise implementation summary
FILES_CHANGED: exact paths
TESTS: command -> result
PHASE1_REGRESSION: command -> result or N/A with reason
CPP_VALIDATION: command -> result or N/A
DISTRIBUTED_VALIDATION: command -> result or N/A
METRICS_CAPTURED: actual measured metrics only; otherwise `None`
HUMAN_ACTION: None or exact required action
KNOWN_LIMITATIONS: explicit remaining limitations
COMMIT: commit SHA + subject
NEXT: next milestone number; continue automatically if not blocked
```

---

# MILESTONE 22 — Finalize task/result envelope semantics and event transport

## Objective

Make distributed attempt semantics explicit before real workers execute product nodes.

## Why this milestone exists

- This milestone isolates **finalize task/result envelope semantics and event transport** so failures are attributable and reviewable.
- The milestone is intentionally narrower than the final Phase-2 architecture.
- Do not implement later milestone responsibilities early unless a tiny interface stub is required for compilation.
- Any stub must be clearly marked as non-production and must not be described as a completed feature.

## Mandatory pre-read and repository inspection

- Read `AGENTS.md` and obey repository-local instructions relevant to files you will touch.
- Run `git status --short` and confirm no unrelated user work will be overwritten.
- Read every existing source file that the milestone will modify before editing it.
- Read the nearest tests covering the behavior before changing implementation.
- Inspect current package/library versions rather than assuming APIs from memory.
- Consult current official documentation before using a third-party API that may have changed.

## Entry conditions

- Milestone 21 is DONE, unless this is Milestone 01.
- Working tree contains no unexpected unrelated modifications.
- The current Phase-1 baseline gate is known and available.
- Any dependency introduced in this milestone has a verified version/source and license acceptable for the repository.

## Detailed implementation procedure

1. Define logical task ID vs attempt ID.
2. Define payload fields needed by workers without duplicating entire UI state.
3. Define result statuses, error class, retryability hint, worker ID, attempt timing, and opaque JSON output.
4. Define a durable result/event stream consumed by the scheduler.
5. Define dedupe behavior for repeated result events.
6. Define late-result behavior from expired attempts.
7. Add protocol compatibility tests between C++ and TypeScript encoders/decoders.
8. Add malformed payload tests and size limits.
9. Before editing, write a short local plan listing the exact files expected to change.
10. Prefer the smallest interface that satisfies this milestone and leaves room for later milestones.
11. Keep names consistent with existing repository conventions; do not introduce a parallel naming universe.
12. If an existing helper already implements part of the behavior, reuse it rather than creating a near-duplicate.
13. Keep product-facing behavior backwards compatible unless the milestone explicitly introduces a new opt-in Evo path.
14. When behavior crosses a trust boundary, validate input before mutating durable state.
15. When behavior crosses a thread/process boundary, define ownership, lifetime, timeout, and failure semantics explicitly.
16. When behavior emits a timestamp, state whether it is wall-clock or steady-clock and use it consistently.
17. Add tests in the same milestone as the behavior; do not defer all testing to Milestone 39.
18. Update `docs/phase2/PROGRESS.md` with the observed result, not a future-tense promise.

## Phase-1 preservation checklist

- [ ] AI planner still requires explicit user Run before execution.
- [ ] Generated graph remains editable React Flow / Liveblocks state.
- [ ] Legacy Trigger.dev engine remains available unless a later explicit final decision says otherwise.
- [ ] Clerk auth/org enforcement remains server-side.
- [ ] Agent Pro-plan gate remains fail-closed.
- [ ] Live browser credentials remain server/worker only.
- [ ] Legacy Run/Stop/results/replay/rerun behavior must not regress.
- [ ] Existing node registry/executors are not duplicated without a compelling reason.
- [ ] No test is removed merely because Phase 2 changes an internal representation.

## Concurrency / distributed correctness questions to answer

- [ ] Which component owns the mutable state introduced here?
- [ ] Can two threads/processes modify it concurrently?
- [ ] What lock/transaction/unique constraint protects the invariant?
- [ ] What happens if an event/message is delivered twice?
- [ ] What happens if the process crashes after the first durable write but before the second?
- [ ] What happens when cancellation races completion?
- [ ] What happens when shutdown starts while another thread is waiting?
- [ ] What happens when a worker is slow rather than dead?
- [ ] Does this change browser-session affinity or ownership?
- [ ] Can a late result overwrite a newer logical state?

## Negative / adversarial scenarios

- [ ] Exercise or explicitly mark N/A: invalid/empty identifiers.
- [ ] Exercise or explicitly mark N/A: duplicate request/event.
- [ ] Exercise or explicitly mark N/A: unknown enum or node type.
- [ ] Exercise or explicitly mark N/A: timeout at external boundary.
- [ ] Exercise or explicitly mark N/A: cancellation at an inconvenient moment.
- [ ] Exercise or explicitly mark N/A: process shutdown while work is pending.
- [ ] Exercise or explicitly mark N/A: stale data or stale attempt result.
- [ ] Exercise or explicitly mark N/A: tenant mismatch / unauthorized request when relevant.
- [ ] Exercise or explicitly mark N/A: resource exhaustion / queue full when relevant.
- [ ] Exercise or explicitly mark N/A: malformed serialized payload when relevant.

## Explicit no-go items for this milestone

- Treat worker data as untrusted input to the scheduler.
- Do not accept unknown run/node IDs without validation.
- Do not invent performance numbers.
- Do not mark a future component as implemented.
- Do not silently change Phase-1 default behavior.
- Do not commit secrets or local credential values.

## Required validation

- [ ] `cross-language fixture tests`
- [ ] `malformed event tests`
- [ ] `size-limit tests`
- [ ] Run current Phase-1 `npm test` when production app code is touched.
- [ ] Run `npm run typecheck` when TypeScript is touched.
- [ ] Run `npm run lint` when TypeScript/React is touched.
- [ ] Run CMake/CTest when C++ is touched.
- [ ] Run production build at integration/high-risk milestones.

## Performance evidence rules for this milestone

- If this milestone is not explicitly a benchmark milestone, do not add a resume number just because a timer was printed.
- If timing is collected for a test, label it diagnostic unless benchmark methodology requirements are satisfied.
- If this milestone changes a previously benchmarked hot path, note that old results are no longer final for the new SHA.
- Store benchmark raw data only in the defined benchmark result structure, not ad-hoc README prose.
- A result must identify workload, build mode, hardware, sample count, and commit before it becomes evidence-grade.

## Human intervention triggers

- If A required secret/account is missing. -> report `STATUS: BLOCKED` with exact manual action; do not guess.
- If A system-level prerequisite such as Docker/CMake/compiler requires user installation. -> report `STATUS: BLOCKED` with exact manual action; do not guess.
- If A shared/remote database migration would be applied. -> report `STATUS: BLOCKED` with exact manual action; do not guess.
- If Git author configuration is missing. -> report `STATUS: BLOCKED` with exact manual action; do not guess.
- If A destructive operation would touch user data or history. -> report `STATUS: BLOCKED` with exact manual action; do not guess.
- If A paid external test would incur nontrivial cost and the user has not already authorized it. -> report `STATUS: BLOCKED` with exact manual action; do not guess.

## Diff-review checklist before commit

- [ ] Review every changed file with `git diff`.
- [ ] Run `git diff --check`.
- [ ] Remove accidental formatting churn outside the milestone scope.
- [ ] Confirm no secret or `.env.local` value appears in the diff.
- [ ] Confirm no generated build tree, node_modules, vcpkg cache, Docker volume, or large transient trace has been staged.
- [ ] Confirm comments/README wording describe implemented behavior, not planned behavior.

## Commit requirement

Create one local commit with subject:

`phase2(m22): harden distributed task result protocol`

After commit:

- Record the new SHA in `docs/phase2/PROGRESS.md`.
- Record commands/results for the milestone.
- Do not push unless push permission/policy has already been explicitly authorized.
- If all exit criteria are green, proceed automatically to the next milestone.

## Exit criteria

- [ ] All milestone-specific implementation items are complete.
- [ ] All relevant new tests pass.
- [ ] Required Phase-1 regression gates pass.
- [ ] No unresolved race/data-corruption/security issue is knowingly introduced.
- [ ] No secret or accidental generated artifact is staged.
- [ ] Documentation describes current behavior accurately.
- [ ] The milestone has exactly one intended local commit or a clearly documented reason for a split required by repository tooling.

## Mandatory milestone report format

```text
STATUS: DONE or BLOCKED
MILESTONE: exact milestone number and title
BASE_SHA: SHA before milestone
END_SHA: SHA after commit when DONE
WHAT_I_INSPECTED: files/docs/commands read before changes
WHAT_CHANGED: concise implementation summary
FILES_CHANGED: exact paths
TESTS: command -> result
PHASE1_REGRESSION: command -> result or N/A with reason
CPP_VALIDATION: command -> result or N/A
DISTRIBUTED_VALIDATION: command -> result or N/A
METRICS_CAPTURED: actual measured metrics only; otherwise `None`
HUMAN_ACTION: None or exact required action
KNOWN_LIMITATIONS: explicit remaining limitations
COMMIT: commit SHA + subject
NEXT: next milestone number; continue automatically if not blocked
```

---

# MILESTONE 23 — Create the TypeScript distributed worker service

## Objective

Create an independently scalable process that consumes Evo tasks but does not yet execute live Browserbase work.

## Why this milestone exists

- This milestone isolates **create the typescript distributed worker service** so failures are attributable and reviewable.
- The milestone is intentionally narrower than the final Phase-2 architecture.
- Do not implement later milestone responsibilities early unless a tiny interface stub is required for compilation.
- Any stub must be clearly marked as non-production and must not be described as a completed feature.

## Mandatory pre-read and repository inspection

- Read `AGENTS.md` and obey repository-local instructions relevant to files you will touch.
- Run `git status --short` and confirm no unrelated user work will be overwritten.
- Read every existing source file that the milestone will modify before editing it.
- Read the nearest tests covering the behavior before changing implementation.
- Inspect current package/library versions rather than assuming APIs from memory.
- Consult current official documentation before using a third-party API that may have changed.

## Entry conditions

- Milestone 22 is DONE, unless this is Milestone 01.
- Working tree contains no unexpected unrelated modifications.
- The current Phase-1 baseline gate is known and available.
- Any dependency introduced in this milestone has a verified version/source and license acceptable for the repository.

## Detailed implementation procedure

1. Create a worker service separate from Next.js and Trigger.dev.
2. Generate a stable worker ID on process start or via configured identity.
3. Join Redis consumer groups and claim task messages according to the Phase-2 protocol.
4. Implement graceful shutdown so in-progress claims are handled according to lease semantics rather than silently abandoned.
5. Implement a synthetic executor mode for integration and benchmarks.
6. Publish result envelopes to the result stream.
7. Do not acknowledge a task until the defined durable handoff/result condition is met.
8. Add Dockerfile and Compose scale support.
9. Add tests with 1,2,4 synthetic workers.
10. Before editing, write a short local plan listing the exact files expected to change.
11. Prefer the smallest interface that satisfies this milestone and leaves room for later milestones.
12. Keep names consistent with existing repository conventions; do not introduce a parallel naming universe.
13. If an existing helper already implements part of the behavior, reuse it rather than creating a near-duplicate.
14. Keep product-facing behavior backwards compatible unless the milestone explicitly introduces a new opt-in Evo path.
15. When behavior crosses a trust boundary, validate input before mutating durable state.
16. When behavior crosses a thread/process boundary, define ownership, lifetime, timeout, and failure semantics explicitly.
17. When behavior emits a timestamp, state whether it is wall-clock or steady-clock and use it consistently.
18. Add tests in the same milestone as the behavior; do not defer all testing to Milestone 39.
19. Update `docs/phase2/PROGRESS.md` with the observed result, not a future-tense promise.

## Phase-1 preservation checklist

- [ ] AI planner still requires explicit user Run before execution.
- [ ] Generated graph remains editable React Flow / Liveblocks state.
- [ ] Legacy Trigger.dev engine remains available unless a later explicit final decision says otherwise.
- [ ] Clerk auth/org enforcement remains server-side.
- [ ] Agent Pro-plan gate remains fail-closed.
- [ ] Live browser credentials remain server/worker only.
- [ ] Legacy Run/Stop/results/replay/rerun behavior must not regress.
- [ ] Existing node registry/executors are not duplicated without a compelling reason.
- [ ] No test is removed merely because Phase 2 changes an internal representation.

## Concurrency / distributed correctness questions to answer

- [ ] Which component owns the mutable state introduced here?
- [ ] Can two threads/processes modify it concurrently?
- [ ] What lock/transaction/unique constraint protects the invariant?
- [ ] What happens if an event/message is delivered twice?
- [ ] What happens if the process crashes after the first durable write but before the second?
- [ ] What happens when cancellation races completion?
- [ ] What happens when shutdown starts while another thread is waiting?
- [ ] What happens when a worker is slow rather than dead?
- [ ] Does this change browser-session affinity or ownership?
- [ ] Can a late result overwrite a newer logical state?

## Negative / adversarial scenarios

- [ ] Exercise or explicitly mark N/A: invalid/empty identifiers.
- [ ] Exercise or explicitly mark N/A: duplicate request/event.
- [ ] Exercise or explicitly mark N/A: unknown enum or node type.
- [ ] Exercise or explicitly mark N/A: timeout at external boundary.
- [ ] Exercise or explicitly mark N/A: cancellation at an inconvenient moment.
- [ ] Exercise or explicitly mark N/A: process shutdown while work is pending.
- [ ] Exercise or explicitly mark N/A: stale data or stale attempt result.
- [ ] Exercise or explicitly mark N/A: tenant mismatch / unauthorized request when relevant.
- [ ] Exercise or explicitly mark N/A: resource exhaustion / queue full when relevant.
- [ ] Exercise or explicitly mark N/A: malformed serialized payload when relevant.

## Explicit no-go items for this milestone

- Do not call existing Browserbase node executors yet.
- No external secrets are required for synthetic worker tests.
- Do not invent performance numbers.
- Do not mark a future component as implemented.
- Do not silently change Phase-1 default behavior.
- Do not commit secrets or local credential values.

## Required validation

- [ ] `worker unit tests`
- [ ] `Redis integration`
- [ ] `multi-worker synthetic completion`
- [ ] `Phase-1 npm regression`
- [ ] Run current Phase-1 `npm test` when production app code is touched.
- [ ] Run `npm run typecheck` when TypeScript is touched.
- [ ] Run `npm run lint` when TypeScript/React is touched.
- [ ] Run CMake/CTest when C++ is touched.
- [ ] Run production build at integration/high-risk milestones.

## Performance evidence rules for this milestone

- If this milestone is not explicitly a benchmark milestone, do not add a resume number just because a timer was printed.
- If timing is collected for a test, label it diagnostic unless benchmark methodology requirements are satisfied.
- If this milestone changes a previously benchmarked hot path, note that old results are no longer final for the new SHA.
- Store benchmark raw data only in the defined benchmark result structure, not ad-hoc README prose.
- A result must identify workload, build mode, hardware, sample count, and commit before it becomes evidence-grade.

## Human intervention triggers

- If A required secret/account is missing. -> report `STATUS: BLOCKED` with exact manual action; do not guess.
- If A system-level prerequisite such as Docker/CMake/compiler requires user installation. -> report `STATUS: BLOCKED` with exact manual action; do not guess.
- If A shared/remote database migration would be applied. -> report `STATUS: BLOCKED` with exact manual action; do not guess.
- If Git author configuration is missing. -> report `STATUS: BLOCKED` with exact manual action; do not guess.
- If A destructive operation would touch user data or history. -> report `STATUS: BLOCKED` with exact manual action; do not guess.
- If A paid external test would incur nontrivial cost and the user has not already authorized it. -> report `STATUS: BLOCKED` with exact manual action; do not guess.

## Diff-review checklist before commit

- [ ] Review every changed file with `git diff`.
- [ ] Run `git diff --check`.
- [ ] Remove accidental formatting churn outside the milestone scope.
- [ ] Confirm no secret or `.env.local` value appears in the diff.
- [ ] Confirm no generated build tree, node_modules, vcpkg cache, Docker volume, or large transient trace has been staged.
- [ ] Confirm comments/README wording describe implemented behavior, not planned behavior.

## Commit requirement

Create one local commit with subject:

`phase2(m23): add scalable typescript worker service`

After commit:

- Record the new SHA in `docs/phase2/PROGRESS.md`.
- Record commands/results for the milestone.
- Do not push unless push permission/policy has already been explicitly authorized.
- If all exit criteria are green, proceed automatically to the next milestone.

## Exit criteria

- [ ] All milestone-specific implementation items are complete.
- [ ] All relevant new tests pass.
- [ ] Required Phase-1 regression gates pass.
- [ ] No unresolved race/data-corruption/security issue is knowingly introduced.
- [ ] No secret or accidental generated artifact is staged.
- [ ] Documentation describes current behavior accurately.
- [ ] The milestone has exactly one intended local commit or a clearly documented reason for a split required by repository tooling.

## Mandatory milestone report format

```text
STATUS: DONE or BLOCKED
MILESTONE: exact milestone number and title
BASE_SHA: SHA before milestone
END_SHA: SHA after commit when DONE
WHAT_I_INSPECTED: files/docs/commands read before changes
WHAT_CHANGED: concise implementation summary
FILES_CHANGED: exact paths
TESTS: command -> result
PHASE1_REGRESSION: command -> result or N/A with reason
CPP_VALIDATION: command -> result or N/A
DISTRIBUTED_VALIDATION: command -> result or N/A
METRICS_CAPTURED: actual measured metrics only; otherwise `None`
HUMAN_ACTION: None or exact required action
KNOWN_LIMITATIONS: explicit remaining limitations
COMMIT: commit SHA + subject
NEXT: next milestone number; continue automatically if not blocked
```

---

# MILESTONE 24 — Reuse existing interpolation and node executors inside distributed workers

## Objective

Execute real EvoBrowser node semantics without duplicating Stagehand logic in C++.

## Why this milestone exists

- This milestone isolates **reuse existing interpolation and node executors inside distributed workers** so failures are attributable and reviewable.
- The milestone is intentionally narrower than the final Phase-2 architecture.
- Do not implement later milestone responsibilities early unless a tiny interface stub is required for compilation.
- Any stub must be clearly marked as non-production and must not be described as a completed feature.

## Mandatory pre-read and repository inspection

- Read `AGENTS.md` and obey repository-local instructions relevant to files you will touch.
- Run `git status --short` and confirm no unrelated user work will be overwritten.
- Read every existing source file that the milestone will modify before editing it.
- Read the nearest tests covering the behavior before changing implementation.
- Inspect current package/library versions rather than assuming APIs from memory.
- Consult current official documentation before using a third-party API that may have changed.

## Entry conditions

- Milestone 23 is DONE, unless this is Milestone 01.
- Working tree contains no unexpected unrelated modifications.
- The current Phase-1 baseline gate is known and available.
- Any dependency introduced in this milestone has a verified version/source and license acceptable for the repository.

## Detailed implementation procedure

1. Create a worker-side execution adapter around the existing node registry/executors.
2. Load the immutable workflow version by run/version/node ID.
3. Load predecessor outputs required for interpolation from durable node-run state.
4. Reuse the existing interpolation implementation.
5. Call the existing executor for supported node types.
6. Preserve server-only secret access.
7. Introduce a safe test sink/mock for side-effecting email execution in automated distributed tests.
8. Return opaque JSON output through the result envelope.
9. Test output compatibility between legacy executor and worker executor for mocked deterministic nodes.
10. Before editing, write a short local plan listing the exact files expected to change.
11. Prefer the smallest interface that satisfies this milestone and leaves room for later milestones.
12. Keep names consistent with existing repository conventions; do not introduce a parallel naming universe.
13. If an existing helper already implements part of the behavior, reuse it rather than creating a near-duplicate.
14. Keep product-facing behavior backwards compatible unless the milestone explicitly introduces a new opt-in Evo path.
15. When behavior crosses a trust boundary, validate input before mutating durable state.
16. When behavior crosses a thread/process boundary, define ownership, lifetime, timeout, and failure semantics explicitly.
17. When behavior emits a timestamp, state whether it is wall-clock or steady-clock and use it consistently.
18. Add tests in the same milestone as the behavior; do not defer all testing to Milestone 39.
19. Update `docs/phase2/PROGRESS.md` with the observed result, not a future-tense promise.

## Phase-1 preservation checklist

- [ ] AI planner still requires explicit user Run before execution.
- [ ] Generated graph remains editable React Flow / Liveblocks state.
- [ ] Legacy Trigger.dev engine remains available unless a later explicit final decision says otherwise.
- [ ] Clerk auth/org enforcement remains server-side.
- [ ] Agent Pro-plan gate remains fail-closed.
- [ ] Live browser credentials remain server/worker only.
- [ ] Legacy Run/Stop/results/replay/rerun behavior must not regress.
- [ ] Existing node registry/executors are not duplicated without a compelling reason.
- [ ] No test is removed merely because Phase 2 changes an internal representation.

## Concurrency / distributed correctness questions to answer

- [ ] Which component owns the mutable state introduced here?
- [ ] Can two threads/processes modify it concurrently?
- [ ] What lock/transaction/unique constraint protects the invariant?
- [ ] What happens if an event/message is delivered twice?
- [ ] What happens if the process crashes after the first durable write but before the second?
- [ ] What happens when cancellation races completion?
- [ ] What happens when shutdown starts while another thread is waiting?
- [ ] What happens when a worker is slow rather than dead?
- [ ] Does this change browser-session affinity or ownership?
- [ ] Can a late result overwrite a newer logical state?

## Negative / adversarial scenarios

- [ ] Exercise or explicitly mark N/A: invalid/empty identifiers.
- [ ] Exercise or explicitly mark N/A: duplicate request/event.
- [ ] Exercise or explicitly mark N/A: unknown enum or node type.
- [ ] Exercise or explicitly mark N/A: timeout at external boundary.
- [ ] Exercise or explicitly mark N/A: cancellation at an inconvenient moment.
- [ ] Exercise or explicitly mark N/A: process shutdown while work is pending.
- [ ] Exercise or explicitly mark N/A: stale data or stale attempt result.
- [ ] Exercise or explicitly mark N/A: tenant mismatch / unauthorized request when relevant.
- [ ] Exercise or explicitly mark N/A: resource exhaustion / queue full when relevant.
- [ ] Exercise or explicitly mark N/A: malformed serialized payload when relevant.

## Explicit no-go items for this milestone

- Do not copy/paste Stagehand node logic into a second implementation.
- Do not send real emails during automated tests.
- Do not invent performance numbers.
- Do not mark a future component as implemented.
- Do not silently change Phase-1 default behavior.
- Do not commit secrets or local credential values.

## Required validation

- [ ] `adapter unit tests`
- [ ] `mocked legacy-vs-worker output compatibility`
- [ ] `Phase-1 node tests`
- [ ] Run current Phase-1 `npm test` when production app code is touched.
- [ ] Run `npm run typecheck` when TypeScript is touched.
- [ ] Run `npm run lint` when TypeScript/React is touched.
- [ ] Run CMake/CTest when C++ is touched.
- [ ] Run production build at integration/high-risk milestones.

## Performance evidence rules for this milestone

- If this milestone is not explicitly a benchmark milestone, do not add a resume number just because a timer was printed.
- If timing is collected for a test, label it diagnostic unless benchmark methodology requirements are satisfied.
- If this milestone changes a previously benchmarked hot path, note that old results are no longer final for the new SHA.
- Store benchmark raw data only in the defined benchmark result structure, not ad-hoc README prose.
- A result must identify workload, build mode, hardware, sample count, and commit before it becomes evidence-grade.

## Human intervention triggers

- If A required secret/account is missing. -> report `STATUS: BLOCKED` with exact manual action; do not guess.
- If A system-level prerequisite such as Docker/CMake/compiler requires user installation. -> report `STATUS: BLOCKED` with exact manual action; do not guess.
- If A shared/remote database migration would be applied. -> report `STATUS: BLOCKED` with exact manual action; do not guess.
- If Git author configuration is missing. -> report `STATUS: BLOCKED` with exact manual action; do not guess.
- If A destructive operation would touch user data or history. -> report `STATUS: BLOCKED` with exact manual action; do not guess.
- If A paid external test would incur nontrivial cost and the user has not already authorized it. -> report `STATUS: BLOCKED` with exact manual action; do not guess.

## Diff-review checklist before commit

- [ ] Review every changed file with `git diff`.
- [ ] Run `git diff --check`.
- [ ] Remove accidental formatting churn outside the milestone scope.
- [ ] Confirm no secret or `.env.local` value appears in the diff.
- [ ] Confirm no generated build tree, node_modules, vcpkg cache, Docker volume, or large transient trace has been staged.
- [ ] Confirm comments/README wording describe implemented behavior, not planned behavior.

## Commit requirement

Create one local commit with subject:

`phase2(m24): reuse existing node executors in worker`

After commit:

- Record the new SHA in `docs/phase2/PROGRESS.md`.
- Record commands/results for the milestone.
- Do not push unless push permission/policy has already been explicitly authorized.
- If all exit criteria are green, proceed automatically to the next milestone.

## Exit criteria

- [ ] All milestone-specific implementation items are complete.
- [ ] All relevant new tests pass.
- [ ] Required Phase-1 regression gates pass.
- [ ] No unresolved race/data-corruption/security issue is knowingly introduced.
- [ ] No secret or accidental generated artifact is staged.
- [ ] Documentation describes current behavior accurately.
- [ ] The milestone has exactly one intended local commit or a clearly documented reason for a split required by repository tooling.

## Mandatory milestone report format

```text
STATUS: DONE or BLOCKED
MILESTONE: exact milestone number and title
BASE_SHA: SHA before milestone
END_SHA: SHA after commit when DONE
WHAT_I_INSPECTED: files/docs/commands read before changes
WHAT_CHANGED: concise implementation summary
FILES_CHANGED: exact paths
TESTS: command -> result
PHASE1_REGRESSION: command -> result or N/A with reason
CPP_VALIDATION: command -> result or N/A
DISTRIBUTED_VALIDATION: command -> result or N/A
METRICS_CAPTURED: actual measured metrics only; otherwise `None`
HUMAN_ACTION: None or exact required action
KNOWN_LIMITATIONS: explicit remaining limitations
COMMIT: commit SHA + subject
NEXT: next milestone number; continue automatically if not blocked
```

---

# MILESTONE 25 — Implement distributed browser session ownership and live-view parity

## Objective

Preserve Phase-1 browser behavior while browser nodes are executed by a worker fleet.

## Why this milestone exists

- This milestone isolates **implement distributed browser session ownership and live-view parity** so failures are attributable and reviewable.
- The milestone is intentionally narrower than the final Phase-2 architecture.
- Do not implement later milestone responsibilities early unless a tiny interface stub is required for compilation.
- Any stub must be clearly marked as non-production and must not be described as a completed feature.

## Mandatory pre-read and repository inspection

- Read `AGENTS.md` and obey repository-local instructions relevant to files you will touch.
- Run `git status --short` and confirm no unrelated user work will be overwritten.
- Read every existing source file that the milestone will modify before editing it.
- Read the nearest tests covering the behavior before changing implementation.
- Inspect current package/library versions rather than assuming APIs from memory.
- Consult current official documentation before using a third-party API that may have changed.

## Entry conditions

- Milestone 24 is DONE, unless this is Milestone 01.
- Working tree contains no unexpected unrelated modifications.
- The current Phase-1 baseline gate is known and available.
- Any dependency introduced in this milestone has a verified version/source and license acceptable for the repository.

## Detailed implementation procedure

1. Create a worker-local BrowserSessionManager keyed by run/affinity key.
2. Open one Stagehand/Browserbase session for the run's default browser affinity group.
3. Publish browser session ID into the engine-neutral run event/state as soon as it exists.
4. Reuse the existing live-view connected handshake before the first browser action when a viewer is present/expected.
5. Reuse existing DOM highlight helpers where compatible.
6. Reuse final screenshot capture before session close.
7. Ensure same affinity key is pinned to the owning worker while the session is live.
8. Close session on success, failure, cancellation, and worker graceful shutdown.
9. Document worker-crash semantics separately; do not pretend the session survives yet.
10. Before editing, write a short local plan listing the exact files expected to change.
11. Prefer the smallest interface that satisfies this milestone and leaves room for later milestones.
12. Keep names consistent with existing repository conventions; do not introduce a parallel naming universe.
13. If an existing helper already implements part of the behavior, reuse it rather than creating a near-duplicate.
14. Keep product-facing behavior backwards compatible unless the milestone explicitly introduces a new opt-in Evo path.
15. When behavior crosses a trust boundary, validate input before mutating durable state.
16. When behavior crosses a thread/process boundary, define ownership, lifetime, timeout, and failure semantics explicitly.
17. When behavior emits a timestamp, state whether it is wall-clock or steady-clock and use it consistently.
18. Add tests in the same milestone as the behavior; do not defer all testing to Milestone 39.
19. Update `docs/phase2/PROGRESS.md` with the observed result, not a future-tense promise.

## Phase-1 preservation checklist

- [ ] AI planner still requires explicit user Run before execution.
- [ ] Generated graph remains editable React Flow / Liveblocks state.
- [ ] Legacy Trigger.dev engine remains available unless a later explicit final decision says otherwise.
- [ ] Clerk auth/org enforcement remains server-side.
- [ ] Agent Pro-plan gate remains fail-closed.
- [ ] Live browser credentials remain server/worker only.
- [ ] Legacy Run/Stop/results/replay/rerun behavior must not regress.
- [ ] Existing node registry/executors are not duplicated without a compelling reason.
- [ ] No test is removed merely because Phase 2 changes an internal representation.

## Concurrency / distributed correctness questions to answer

- [ ] Which component owns the mutable state introduced here?
- [ ] Can two threads/processes modify it concurrently?
- [ ] What lock/transaction/unique constraint protects the invariant?
- [ ] What happens if an event/message is delivered twice?
- [ ] What happens if the process crashes after the first durable write but before the second?
- [ ] What happens when cancellation races completion?
- [ ] What happens when shutdown starts while another thread is waiting?
- [ ] What happens when a worker is slow rather than dead?
- [ ] Does this change browser-session affinity or ownership?
- [ ] Can a late result overwrite a newer logical state?

## Negative / adversarial scenarios

- [ ] Exercise or explicitly mark N/A: invalid/empty identifiers.
- [ ] Exercise or explicitly mark N/A: duplicate request/event.
- [ ] Exercise or explicitly mark N/A: unknown enum or node type.
- [ ] Exercise or explicitly mark N/A: timeout at external boundary.
- [ ] Exercise or explicitly mark N/A: cancellation at an inconvenient moment.
- [ ] Exercise or explicitly mark N/A: process shutdown while work is pending.
- [ ] Exercise or explicitly mark N/A: stale data or stale attempt result.
- [ ] Exercise or explicitly mark N/A: tenant mismatch / unauthorized request when relevant.
- [ ] Exercise or explicitly mark N/A: resource exhaustion / queue full when relevant.
- [ ] Exercise or explicitly mark N/A: malformed serialized payload when relevant.

## Explicit no-go items for this milestone

- No simultaneous browser-mutating tasks for the same affinity key.
- Do not expose Browserbase API key to C++ or frontend.
- Do not invent performance numbers.
- Do not mark a future component as implemented.
- Do not silently change Phase-1 default behavior.
- Do not commit secrets or local credential values.

## Required validation

- [ ] `mock session-manager tests`
- [ ] `live-key manual E2E only when BROWSERBASE_API_KEY is configured`
- [ ] `Phase-1 live-view regression`
- [ ] Run current Phase-1 `npm test` when production app code is touched.
- [ ] Run `npm run typecheck` when TypeScript is touched.
- [ ] Run `npm run lint` when TypeScript/React is touched.
- [ ] Run CMake/CTest when C++ is touched.
- [ ] Run production build at integration/high-risk milestones.

## Performance evidence rules for this milestone

- If this milestone is not explicitly a benchmark milestone, do not add a resume number just because a timer was printed.
- If timing is collected for a test, label it diagnostic unless benchmark methodology requirements are satisfied.
- If this milestone changes a previously benchmarked hot path, note that old results are no longer final for the new SHA.
- Store benchmark raw data only in the defined benchmark result structure, not ad-hoc README prose.
- A result must identify workload, build mode, hardware, sample count, and commit before it becomes evidence-grade.

## Human intervention triggers

- If A required secret/account is missing. -> report `STATUS: BLOCKED` with exact manual action; do not guess.
- If A system-level prerequisite such as Docker/CMake/compiler requires user installation. -> report `STATUS: BLOCKED` with exact manual action; do not guess.
- If A shared/remote database migration would be applied. -> report `STATUS: BLOCKED` with exact manual action; do not guess.
- If Git author configuration is missing. -> report `STATUS: BLOCKED` with exact manual action; do not guess.
- If A destructive operation would touch user data or history. -> report `STATUS: BLOCKED` with exact manual action; do not guess.
- If A paid external test would incur nontrivial cost and the user has not already authorized it. -> report `STATUS: BLOCKED` with exact manual action; do not guess.

## Diff-review checklist before commit

- [ ] Review every changed file with `git diff`.
- [ ] Run `git diff --check`.
- [ ] Remove accidental formatting churn outside the milestone scope.
- [ ] Confirm no secret or `.env.local` value appears in the diff.
- [ ] Confirm no generated build tree, node_modules, vcpkg cache, Docker volume, or large transient trace has been staged.
- [ ] Confirm comments/README wording describe implemented behavior, not planned behavior.

## Commit requirement

Create one local commit with subject:

`phase2(m25): preserve browser session semantics in workers`

After commit:

- Record the new SHA in `docs/phase2/PROGRESS.md`.
- Record commands/results for the milestone.
- Do not push unless push permission/policy has already been explicitly authorized.
- If all exit criteria are green, proceed automatically to the next milestone.

## Exit criteria

- [ ] All milestone-specific implementation items are complete.
- [ ] All relevant new tests pass.
- [ ] Required Phase-1 regression gates pass.
- [ ] No unresolved race/data-corruption/security issue is knowingly introduced.
- [ ] No secret or accidental generated artifact is staged.
- [ ] Documentation describes current behavior accurately.
- [ ] The milestone has exactly one intended local commit or a clearly documented reason for a split required by repository tooling.

## Mandatory milestone report format

```text
STATUS: DONE or BLOCKED
MILESTONE: exact milestone number and title
BASE_SHA: SHA before milestone
END_SHA: SHA after commit when DONE
WHAT_I_INSPECTED: files/docs/commands read before changes
WHAT_CHANGED: concise implementation summary
FILES_CHANGED: exact paths
TESTS: command -> result
PHASE1_REGRESSION: command -> result or N/A with reason
CPP_VALIDATION: command -> result or N/A
DISTRIBUTED_VALIDATION: command -> result or N/A
METRICS_CAPTURED: actual measured metrics only; otherwise `None`
HUMAN_ACTION: None or exact required action
KNOWN_LIMITATIONS: explicit remaining limitations
COMMIT: commit SHA + subject
NEXT: next milestone number; continue automatically if not blocked
```

---

# MILESTONE 26 — Persist results and unlock dependencies through the distributed loop

## Objective

Complete the scheduler ↔ worker ↔ result ↔ successor cycle.

## Why this milestone exists

- This milestone isolates **persist results and unlock dependencies through the distributed loop** so failures are attributable and reviewable.
- The milestone is intentionally narrower than the final Phase-2 architecture.
- Do not implement later milestone responsibilities early unless a tiny interface stub is required for compilation.
- Any stub must be clearly marked as non-production and must not be described as a completed feature.

## Mandatory pre-read and repository inspection

- Read `AGENTS.md` and obey repository-local instructions relevant to files you will touch.
- Run `git status --short` and confirm no unrelated user work will be overwritten.
- Read every existing source file that the milestone will modify before editing it.
- Read the nearest tests covering the behavior before changing implementation.
- Inspect current package/library versions rather than assuming APIs from memory.
- Consult current official documentation before using a third-party API that may have changed.

## Entry conditions

- Milestone 25 is DONE, unless this is Milestone 01.
- Working tree contains no unexpected unrelated modifications.
- The current Phase-1 baseline gate is known and available.
- Any dependency introduced in this milestone has a verified version/source and license acceptable for the repository.

## Detailed implementation procedure

1. Consume result events in the C++ scheduler.
2. Validate run/node/attempt identity before applying a result.
3. Persist logical node output/status/timing to Postgres using parameterized queries or an equally safe verified persistence path.
4. Apply a successful logical result at most once.
5. Unlock successor dependency counters only after durable logical success.
6. Persist failure details and hand them to retry policy later without double-unlocking successors.
7. Publish normalized run events for UI consumers.
8. Run an end-to-end synthetic distributed diamond DAG through C++ scheduler + Redis + multiple TS workers + Postgres.
9. Before editing, write a short local plan listing the exact files expected to change.
10. Prefer the smallest interface that satisfies this milestone and leaves room for later milestones.
11. Keep names consistent with existing repository conventions; do not introduce a parallel naming universe.
12. If an existing helper already implements part of the behavior, reuse it rather than creating a near-duplicate.
13. Keep product-facing behavior backwards compatible unless the milestone explicitly introduces a new opt-in Evo path.
14. When behavior crosses a trust boundary, validate input before mutating durable state.
15. When behavior crosses a thread/process boundary, define ownership, lifetime, timeout, and failure semantics explicitly.
16. When behavior emits a timestamp, state whether it is wall-clock or steady-clock and use it consistently.
17. Add tests in the same milestone as the behavior; do not defer all testing to Milestone 39.
18. Update `docs/phase2/PROGRESS.md` with the observed result, not a future-tense promise.

## Phase-1 preservation checklist

- [ ] AI planner still requires explicit user Run before execution.
- [ ] Generated graph remains editable React Flow / Liveblocks state.
- [ ] Legacy Trigger.dev engine remains available unless a later explicit final decision says otherwise.
- [ ] Clerk auth/org enforcement remains server-side.
- [ ] Agent Pro-plan gate remains fail-closed.
- [ ] Live browser credentials remain server/worker only.
- [ ] Legacy Run/Stop/results/replay/rerun behavior must not regress.
- [ ] Existing node registry/executors are not duplicated without a compelling reason.
- [ ] No test is removed merely because Phase 2 changes an internal representation.

## Concurrency / distributed correctness questions to answer

- [ ] Which component owns the mutable state introduced here?
- [ ] Can two threads/processes modify it concurrently?
- [ ] What lock/transaction/unique constraint protects the invariant?
- [ ] What happens if an event/message is delivered twice?
- [ ] What happens if the process crashes after the first durable write but before the second?
- [ ] What happens when cancellation races completion?
- [ ] What happens when shutdown starts while another thread is waiting?
- [ ] What happens when a worker is slow rather than dead?
- [ ] Does this change browser-session affinity or ownership?
- [ ] Can a late result overwrite a newer logical state?

## Negative / adversarial scenarios

- [ ] Exercise or explicitly mark N/A: invalid/empty identifiers.
- [ ] Exercise or explicitly mark N/A: duplicate request/event.
- [ ] Exercise or explicitly mark N/A: unknown enum or node type.
- [ ] Exercise or explicitly mark N/A: timeout at external boundary.
- [ ] Exercise or explicitly mark N/A: cancellation at an inconvenient moment.
- [ ] Exercise or explicitly mark N/A: process shutdown while work is pending.
- [ ] Exercise or explicitly mark N/A: stale data or stale attempt result.
- [ ] Exercise or explicitly mark N/A: tenant mismatch / unauthorized request when relevant.
- [ ] Exercise or explicitly mark N/A: resource exhaustion / queue full when relevant.
- [ ] Exercise or explicitly mark N/A: malformed serialized payload when relevant.

## Explicit no-go items for this milestone

- A duplicate successful result must not decrement successors twice.
- A late result from an expired attempt must not overwrite a newer logical terminal state.
- Do not invent performance numbers.
- Do not mark a future component as implemented.
- Do not silently change Phase-1 default behavior.
- Do not commit secrets or local credential values.

## Required validation

- [ ] `distributed E2E synthetic DAG`
- [ ] `duplicate result injection`
- [ ] `Postgres audit assertions`
- [ ] Run current Phase-1 `npm test` when production app code is touched.
- [ ] Run `npm run typecheck` when TypeScript is touched.
- [ ] Run `npm run lint` when TypeScript/React is touched.
- [ ] Run CMake/CTest when C++ is touched.
- [ ] Run production build at integration/high-risk milestones.

## Performance evidence rules for this milestone

- If this milestone is not explicitly a benchmark milestone, do not add a resume number just because a timer was printed.
- If timing is collected for a test, label it diagnostic unless benchmark methodology requirements are satisfied.
- If this milestone changes a previously benchmarked hot path, note that old results are no longer final for the new SHA.
- Store benchmark raw data only in the defined benchmark result structure, not ad-hoc README prose.
- A result must identify workload, build mode, hardware, sample count, and commit before it becomes evidence-grade.

## Human intervention triggers

- If A required secret/account is missing. -> report `STATUS: BLOCKED` with exact manual action; do not guess.
- If A system-level prerequisite such as Docker/CMake/compiler requires user installation. -> report `STATUS: BLOCKED` with exact manual action; do not guess.
- If A shared/remote database migration would be applied. -> report `STATUS: BLOCKED` with exact manual action; do not guess.
- If Git author configuration is missing. -> report `STATUS: BLOCKED` with exact manual action; do not guess.
- If A destructive operation would touch user data or history. -> report `STATUS: BLOCKED` with exact manual action; do not guess.
- If A paid external test would incur nontrivial cost and the user has not already authorized it. -> report `STATUS: BLOCKED` with exact manual action; do not guess.

## Diff-review checklist before commit

- [ ] Review every changed file with `git diff`.
- [ ] Run `git diff --check`.
- [ ] Remove accidental formatting churn outside the milestone scope.
- [ ] Confirm no secret or `.env.local` value appears in the diff.
- [ ] Confirm no generated build tree, node_modules, vcpkg cache, Docker volume, or large transient trace has been staged.
- [ ] Confirm comments/README wording describe implemented behavior, not planned behavior.

## Commit requirement

Create one local commit with subject:

`phase2(m26): close distributed scheduling result loop`

After commit:

- Record the new SHA in `docs/phase2/PROGRESS.md`.
- Record commands/results for the milestone.
- Do not push unless push permission/policy has already been explicitly authorized.
- If all exit criteria are green, proceed automatically to the next milestone.

## Exit criteria

- [ ] All milestone-specific implementation items are complete.
- [ ] All relevant new tests pass.
- [ ] Required Phase-1 regression gates pass.
- [ ] No unresolved race/data-corruption/security issue is knowingly introduced.
- [ ] No secret or accidental generated artifact is staged.
- [ ] Documentation describes current behavior accurately.
- [ ] The milestone has exactly one intended local commit or a clearly documented reason for a split required by repository tooling.

## Mandatory milestone report format

```text
STATUS: DONE or BLOCKED
MILESTONE: exact milestone number and title
BASE_SHA: SHA before milestone
END_SHA: SHA after commit when DONE
WHAT_I_INSPECTED: files/docs/commands read before changes
WHAT_CHANGED: concise implementation summary
FILES_CHANGED: exact paths
TESTS: command -> result
PHASE1_REGRESSION: command -> result or N/A with reason
CPP_VALIDATION: command -> result or N/A
DISTRIBUTED_VALIDATION: command -> result or N/A
METRICS_CAPTURED: actual measured metrics only; otherwise `None`
HUMAN_ACTION: None or exact required action
KNOWN_LIMITATIONS: explicit remaining limitations
COMMIT: commit SHA + subject
NEXT: next milestone number; continue automatically if not blocked
```

---

# MILESTONE 27 — Introduce the Next.js execution-engine abstraction and feature flag

## Objective

Connect Phase 2 to the product without replacing Phase 1.

## Why this milestone exists

- This milestone isolates **introduce the next.js execution-engine abstraction and feature flag** so failures are attributable and reviewable.
- The milestone is intentionally narrower than the final Phase-2 architecture.
- Do not implement later milestone responsibilities early unless a tiny interface stub is required for compilation.
- Any stub must be clearly marked as non-production and must not be described as a completed feature.

## Mandatory pre-read and repository inspection

- Read `AGENTS.md` and obey repository-local instructions relevant to files you will touch.
- Run `git status --short` and confirm no unrelated user work will be overwritten.
- Read every existing source file that the milestone will modify before editing it.
- Read the nearest tests covering the behavior before changing implementation.
- Inspect current package/library versions rather than assuming APIs from memory.
- Consult current official documentation before using a third-party API that may have changed.

## Entry conditions

- Milestone 26 is DONE, unless this is Milestone 01.
- Working tree contains no unexpected unrelated modifications.
- The current Phase-1 baseline gate is known and available.
- Any dependency introduced in this milestone has a verified version/source and license acceptable for the repository.

## Detailed implementation procedure

1. Create an engine-neutral TypeScript interface for start/cancel/query semantics.
2. Wrap the existing Trigger.dev path in a legacy adapter without changing behavior.
3. Create an Evo adapter that talks to the C++ scheduler service.
4. Add a server-only feature flag/config such as `EXECUTION_ENGINE=legacy|evo` with legacy as default initially.
5. Ensure Clerk authorization and plan gating occur before either engine starts.
6. Create immutable workflow version + engine-neutral run row before submission.
7. Return an engine-neutral run handle to the UI boundary.
8. Do not remove Trigger.dev dependencies or the existing task.
9. Before editing, write a short local plan listing the exact files expected to change.
10. Prefer the smallest interface that satisfies this milestone and leaves room for later milestones.
11. Keep names consistent with existing repository conventions; do not introduce a parallel naming universe.
12. If an existing helper already implements part of the behavior, reuse it rather than creating a near-duplicate.
13. Keep product-facing behavior backwards compatible unless the milestone explicitly introduces a new opt-in Evo path.
14. When behavior crosses a trust boundary, validate input before mutating durable state.
15. When behavior crosses a thread/process boundary, define ownership, lifetime, timeout, and failure semantics explicitly.
16. When behavior emits a timestamp, state whether it is wall-clock or steady-clock and use it consistently.
17. Add tests in the same milestone as the behavior; do not defer all testing to Milestone 39.
18. Update `docs/phase2/PROGRESS.md` with the observed result, not a future-tense promise.

## Phase-1 preservation checklist

- [ ] AI planner still requires explicit user Run before execution.
- [ ] Generated graph remains editable React Flow / Liveblocks state.
- [ ] Legacy Trigger.dev engine remains available unless a later explicit final decision says otherwise.
- [ ] Clerk auth/org enforcement remains server-side.
- [ ] Agent Pro-plan gate remains fail-closed.
- [ ] Live browser credentials remain server/worker only.
- [ ] Legacy Run/Stop/results/replay/rerun behavior must not regress.
- [ ] Existing node registry/executors are not duplicated without a compelling reason.
- [ ] No test is removed merely because Phase 2 changes an internal representation.

## Concurrency / distributed correctness questions to answer

- [ ] Which component owns the mutable state introduced here?
- [ ] Can two threads/processes modify it concurrently?
- [ ] What lock/transaction/unique constraint protects the invariant?
- [ ] What happens if an event/message is delivered twice?
- [ ] What happens if the process crashes after the first durable write but before the second?
- [ ] What happens when cancellation races completion?
- [ ] What happens when shutdown starts while another thread is waiting?
- [ ] What happens when a worker is slow rather than dead?
- [ ] Does this change browser-session affinity or ownership?
- [ ] Can a late result overwrite a newer logical state?

## Negative / adversarial scenarios

- [ ] Exercise or explicitly mark N/A: invalid/empty identifiers.
- [ ] Exercise or explicitly mark N/A: duplicate request/event.
- [ ] Exercise or explicitly mark N/A: unknown enum or node type.
- [ ] Exercise or explicitly mark N/A: timeout at external boundary.
- [ ] Exercise or explicitly mark N/A: cancellation at an inconvenient moment.
- [ ] Exercise or explicitly mark N/A: process shutdown while work is pending.
- [ ] Exercise or explicitly mark N/A: stale data or stale attempt result.
- [ ] Exercise or explicitly mark N/A: tenant mismatch / unauthorized request when relevant.
- [ ] Exercise or explicitly mark N/A: resource exhaustion / queue full when relevant.
- [ ] Exercise or explicitly mark N/A: malformed serialized payload when relevant.

## Explicit no-go items for this milestone

- Planner generation still never auto-runs.
- The user-facing Run action remains explicit.
- Do not invent performance numbers.
- Do not mark a future component as implemented.
- Do not silently change Phase-1 default behavior.
- Do not commit secrets or local credential values.

## Required validation

- [ ] `legacy adapter regression`
- [ ] `Evo synthetic submission integration`
- [ ] `Phase-1 tests/build`
- [ ] Run current Phase-1 `npm test` when production app code is touched.
- [ ] Run `npm run typecheck` when TypeScript is touched.
- [ ] Run `npm run lint` when TypeScript/React is touched.
- [ ] Run CMake/CTest when C++ is touched.
- [ ] Run production build at integration/high-risk milestones.

## Performance evidence rules for this milestone

- If this milestone is not explicitly a benchmark milestone, do not add a resume number just because a timer was printed.
- If timing is collected for a test, label it diagnostic unless benchmark methodology requirements are satisfied.
- If this milestone changes a previously benchmarked hot path, note that old results are no longer final for the new SHA.
- Store benchmark raw data only in the defined benchmark result structure, not ad-hoc README prose.
- A result must identify workload, build mode, hardware, sample count, and commit before it becomes evidence-grade.

## Human intervention triggers

- If A required secret/account is missing. -> report `STATUS: BLOCKED` with exact manual action; do not guess.
- If A system-level prerequisite such as Docker/CMake/compiler requires user installation. -> report `STATUS: BLOCKED` with exact manual action; do not guess.
- If A shared/remote database migration would be applied. -> report `STATUS: BLOCKED` with exact manual action; do not guess.
- If Git author configuration is missing. -> report `STATUS: BLOCKED` with exact manual action; do not guess.
- If A destructive operation would touch user data or history. -> report `STATUS: BLOCKED` with exact manual action; do not guess.
- If A paid external test would incur nontrivial cost and the user has not already authorized it. -> report `STATUS: BLOCKED` with exact manual action; do not guess.

## Diff-review checklist before commit

- [ ] Review every changed file with `git diff`.
- [ ] Run `git diff --check`.
- [ ] Remove accidental formatting churn outside the milestone scope.
- [ ] Confirm no secret or `.env.local` value appears in the diff.
- [ ] Confirm no generated build tree, node_modules, vcpkg cache, Docker volume, or large transient trace has been staged.
- [ ] Confirm comments/README wording describe implemented behavior, not planned behavior.

## Commit requirement

Create one local commit with subject:

`phase2(m27): add dual execution engine abstraction`

After commit:

- Record the new SHA in `docs/phase2/PROGRESS.md`.
- Record commands/results for the milestone.
- Do not push unless push permission/policy has already been explicitly authorized.
- If all exit criteria are green, proceed automatically to the next milestone.

## Exit criteria

- [ ] All milestone-specific implementation items are complete.
- [ ] All relevant new tests pass.
- [ ] Required Phase-1 regression gates pass.
- [ ] No unresolved race/data-corruption/security issue is knowingly introduced.
- [ ] No secret or accidental generated artifact is staged.
- [ ] Documentation describes current behavior accurately.
- [ ] The milestone has exactly one intended local commit or a clearly documented reason for a split required by repository tooling.

## Mandatory milestone report format

```text
STATUS: DONE or BLOCKED
MILESTONE: exact milestone number and title
BASE_SHA: SHA before milestone
END_SHA: SHA after commit when DONE
WHAT_I_INSPECTED: files/docs/commands read before changes
WHAT_CHANGED: concise implementation summary
FILES_CHANGED: exact paths
TESTS: command -> result
PHASE1_REGRESSION: command -> result or N/A with reason
CPP_VALIDATION: command -> result or N/A
DISTRIBUTED_VALIDATION: command -> result or N/A
METRICS_CAPTURED: actual measured metrics only; otherwise `None`
HUMAN_ACTION: None or exact required action
KNOWN_LIMITATIONS: explicit remaining limitations
COMMIT: commit SHA + subject
NEXT: next milestone number; continue automatically if not blocked
```

---

# MILESTONE 28 — Build engine-neutral Evo run events and realtime frontend transport

## Objective

Allow existing UI concepts to consume Evo runs without pretending they are Trigger.dev runs.

## Why this milestone exists

- This milestone isolates **build engine-neutral evo run events and realtime frontend transport** so failures are attributable and reviewable.
- The milestone is intentionally narrower than the final Phase-2 architecture.
- Do not implement later milestone responsibilities early unless a tiny interface stub is required for compilation.
- Any stub must be clearly marked as non-production and must not be described as a completed feature.

## Mandatory pre-read and repository inspection

- Read `AGENTS.md` and obey repository-local instructions relevant to files you will touch.
- Run `git status --short` and confirm no unrelated user work will be overwritten.
- Read every existing source file that the milestone will modify before editing it.
- Read the nearest tests covering the behavior before changing implementation.
- Inspect current package/library versions rather than assuming APIs from memory.
- Consult current official documentation before using a third-party API that may have changed.

## Entry conditions

- Milestone 27 is DONE, unless this is Milestone 01.
- Working tree contains no unexpected unrelated modifications.
- The current Phase-1 baseline gate is known and available.
- Any dependency introduced in this milestone has a verified version/source and license acceptable for the repository.

## Detailed implementation procedure

1. Define a normalized frontend run view model covering statuses, steps, timing, browser session ID, final URL, output, and replay artifacts.
2. Keep a Trigger adapter that maps legacy realtime runs into this model.
3. Implement Evo event delivery using a durable/queryable mechanism such as Redis Streams plus an authorized server route/SSE layer; verify the chosen mechanism against the deployment runtime.
4. Require Clerk org/run ownership for Evo event access.
5. Handle reconnect by replaying missed events or reading current durable state rather than losing run progress.
6. Do not create one independent unbounded subscription per tiny UI component; preserve shared-provider architecture.
7. Add tests for event ordering, reconnect, duplicate events, and terminal state.
8. Before editing, write a short local plan listing the exact files expected to change.
9. Prefer the smallest interface that satisfies this milestone and leaves room for later milestones.
10. Keep names consistent with existing repository conventions; do not introduce a parallel naming universe.
11. If an existing helper already implements part of the behavior, reuse it rather than creating a near-duplicate.
12. Keep product-facing behavior backwards compatible unless the milestone explicitly introduces a new opt-in Evo path.
13. When behavior crosses a trust boundary, validate input before mutating durable state.
14. When behavior crosses a thread/process boundary, define ownership, lifetime, timeout, and failure semantics explicitly.
15. When behavior emits a timestamp, state whether it is wall-clock or steady-clock and use it consistently.
16. Add tests in the same milestone as the behavior; do not defer all testing to Milestone 39.
17. Update `docs/phase2/PROGRESS.md` with the observed result, not a future-tense promise.

## Phase-1 preservation checklist

- [ ] AI planner still requires explicit user Run before execution.
- [ ] Generated graph remains editable React Flow / Liveblocks state.
- [ ] Legacy Trigger.dev engine remains available unless a later explicit final decision says otherwise.
- [ ] Clerk auth/org enforcement remains server-side.
- [ ] Agent Pro-plan gate remains fail-closed.
- [ ] Live browser credentials remain server/worker only.
- [ ] Legacy Run/Stop/results/replay/rerun behavior must not regress.
- [ ] Existing node registry/executors are not duplicated without a compelling reason.
- [ ] No test is removed merely because Phase 2 changes an internal representation.

## Concurrency / distributed correctness questions to answer

- [ ] Which component owns the mutable state introduced here?
- [ ] Can two threads/processes modify it concurrently?
- [ ] What lock/transaction/unique constraint protects the invariant?
- [ ] What happens if an event/message is delivered twice?
- [ ] What happens if the process crashes after the first durable write but before the second?
- [ ] What happens when cancellation races completion?
- [ ] What happens when shutdown starts while another thread is waiting?
- [ ] What happens when a worker is slow rather than dead?
- [ ] Does this change browser-session affinity or ownership?
- [ ] Can a late result overwrite a newer logical state?

## Negative / adversarial scenarios

- [ ] Exercise or explicitly mark N/A: invalid/empty identifiers.
- [ ] Exercise or explicitly mark N/A: duplicate request/event.
- [ ] Exercise or explicitly mark N/A: unknown enum or node type.
- [ ] Exercise or explicitly mark N/A: timeout at external boundary.
- [ ] Exercise or explicitly mark N/A: cancellation at an inconvenient moment.
- [ ] Exercise or explicitly mark N/A: process shutdown while work is pending.
- [ ] Exercise or explicitly mark N/A: stale data or stale attempt result.
- [ ] Exercise or explicitly mark N/A: tenant mismatch / unauthorized request when relevant.
- [ ] Exercise or explicitly mark N/A: resource exhaustion / queue full when relevant.
- [ ] Exercise or explicitly mark N/A: malformed serialized payload when relevant.

## Explicit no-go items for this milestone

- Do not expose Redis credentials to the browser.
- Do not delete the Trigger realtime provider until legacy compatibility is intentionally retired in a future phase.
- Do not invent performance numbers.
- Do not mark a future component as implemented.
- Do not silently change Phase-1 default behavior.
- Do not commit secrets or local credential values.

## Required validation

- [ ] `normalized model tests`
- [ ] `authorized event-route tests`
- [ ] `reconnect simulation`
- [ ] `Phase-1 provider regression`
- [ ] Run current Phase-1 `npm test` when production app code is touched.
- [ ] Run `npm run typecheck` when TypeScript is touched.
- [ ] Run `npm run lint` when TypeScript/React is touched.
- [ ] Run CMake/CTest when C++ is touched.
- [ ] Run production build at integration/high-risk milestones.

## Performance evidence rules for this milestone

- If this milestone is not explicitly a benchmark milestone, do not add a resume number just because a timer was printed.
- If timing is collected for a test, label it diagnostic unless benchmark methodology requirements are satisfied.
- If this milestone changes a previously benchmarked hot path, note that old results are no longer final for the new SHA.
- Store benchmark raw data only in the defined benchmark result structure, not ad-hoc README prose.
- A result must identify workload, build mode, hardware, sample count, and commit before it becomes evidence-grade.

## Human intervention triggers

- If A required secret/account is missing. -> report `STATUS: BLOCKED` with exact manual action; do not guess.
- If A system-level prerequisite such as Docker/CMake/compiler requires user installation. -> report `STATUS: BLOCKED` with exact manual action; do not guess.
- If A shared/remote database migration would be applied. -> report `STATUS: BLOCKED` with exact manual action; do not guess.
- If Git author configuration is missing. -> report `STATUS: BLOCKED` with exact manual action; do not guess.
- If A destructive operation would touch user data or history. -> report `STATUS: BLOCKED` with exact manual action; do not guess.
- If A paid external test would incur nontrivial cost and the user has not already authorized it. -> report `STATUS: BLOCKED` with exact manual action; do not guess.

## Diff-review checklist before commit

- [ ] Review every changed file with `git diff`.
- [ ] Run `git diff --check`.
- [ ] Remove accidental formatting churn outside the milestone scope.
- [ ] Confirm no secret or `.env.local` value appears in the diff.
- [ ] Confirm no generated build tree, node_modules, vcpkg cache, Docker volume, or large transient trace has been staged.
- [ ] Confirm comments/README wording describe implemented behavior, not planned behavior.

## Commit requirement

Create one local commit with subject:

`phase2(m28): add engine-neutral realtime run model`

After commit:

- Record the new SHA in `docs/phase2/PROGRESS.md`.
- Record commands/results for the milestone.
- Do not push unless push permission/policy has already been explicitly authorized.
- If all exit criteria are green, proceed automatically to the next milestone.

## Exit criteria

- [ ] All milestone-specific implementation items are complete.
- [ ] All relevant new tests pass.
- [ ] Required Phase-1 regression gates pass.
- [ ] No unresolved race/data-corruption/security issue is knowingly introduced.
- [ ] No secret or accidental generated artifact is staged.
- [ ] Documentation describes current behavior accurately.
- [ ] The milestone has exactly one intended local commit or a clearly documented reason for a split required by repository tooling.

## Mandatory milestone report format

```text
STATUS: DONE or BLOCKED
MILESTONE: exact milestone number and title
BASE_SHA: SHA before milestone
END_SHA: SHA after commit when DONE
WHAT_I_INSPECTED: files/docs/commands read before changes
WHAT_CHANGED: concise implementation summary
FILES_CHANGED: exact paths
TESTS: command -> result
PHASE1_REGRESSION: command -> result or N/A with reason
CPP_VALIDATION: command -> result or N/A
DISTRIBUTED_VALIDATION: command -> result or N/A
METRICS_CAPTURED: actual measured metrics only; otherwise `None`
HUMAN_ACTION: None or exact required action
KNOWN_LIMITATIONS: explicit remaining limitations
COMMIT: commit SHA + subject
NEXT: next milestone number; continue automatically if not blocked
```

---

# MILESTONE 29 — Achieve UI parity for Evo runs

## Objective

Make Evo execution preserve the Phase-1 user experience before adding reliability features.

## Why this milestone exists

- This milestone isolates **achieve ui parity for evo runs** so failures are attributable and reviewable.
- The milestone is intentionally narrower than the final Phase-2 architecture.
- Do not implement later milestone responsibilities early unless a tiny interface stub is required for compilation.
- Any stub must be clearly marked as non-production and must not be described as a completed feature.

## Mandatory pre-read and repository inspection

- Read `AGENTS.md` and obey repository-local instructions relevant to files you will touch.
- Run `git status --short` and confirm no unrelated user work will be overwritten.
- Read every existing source file that the milestone will modify before editing it.
- Read the nearest tests covering the behavior before changing implementation.
- Inspect current package/library versions rather than assuming APIs from memory.
- Consult current official documentation before using a third-party API that may have changed.

## Entry conditions

- Milestone 28 is DONE, unless this is Milestone 01.
- Working tree contains no unexpected unrelated modifications.
- The current Phase-1 baseline gate is known and available.
- Any dependency introduced in this milestone has a verified version/source and license acceptable for the repository.

## Detailed implementation procedure

1. Run generated/manual workflows through Evo mode and show pending/running/done/failed states on nodes.
2. Show live Browserbase view when the distributed worker opens the session.
3. Show readable completion results from engine-neutral run/node data.
4. Serve final screenshot with org-checked authorization for Evo runs.
5. Make replay point to the correct Browserbase session for the selected run.
6. Keep workflow editable after completion/failure/cancel.
7. Ensure rerun gets a new engine-neutral run ID and new browser session.
8. Add parity regression tests for legacy vs Evo lifecycle semantics.
9. Before editing, write a short local plan listing the exact files expected to change.
10. Prefer the smallest interface that satisfies this milestone and leaves room for later milestones.
11. Keep names consistent with existing repository conventions; do not introduce a parallel naming universe.
12. If an existing helper already implements part of the behavior, reuse it rather than creating a near-duplicate.
13. Keep product-facing behavior backwards compatible unless the milestone explicitly introduces a new opt-in Evo path.
14. When behavior crosses a trust boundary, validate input before mutating durable state.
15. When behavior crosses a thread/process boundary, define ownership, lifetime, timeout, and failure semantics explicitly.
16. When behavior emits a timestamp, state whether it is wall-clock or steady-clock and use it consistently.
17. Add tests in the same milestone as the behavior; do not defer all testing to Milestone 39.
18. Update `docs/phase2/PROGRESS.md` with the observed result, not a future-tense promise.

## Phase-1 preservation checklist

- [ ] AI planner still requires explicit user Run before execution.
- [ ] Generated graph remains editable React Flow / Liveblocks state.
- [ ] Legacy Trigger.dev engine remains available unless a later explicit final decision says otherwise.
- [ ] Clerk auth/org enforcement remains server-side.
- [ ] Agent Pro-plan gate remains fail-closed.
- [ ] Live browser credentials remain server/worker only.
- [ ] Legacy Run/Stop/results/replay/rerun behavior must not regress.
- [ ] Existing node registry/executors are not duplicated without a compelling reason.
- [ ] No test is removed merely because Phase 2 changes an internal representation.

## Concurrency / distributed correctness questions to answer

- [ ] Which component owns the mutable state introduced here?
- [ ] Can two threads/processes modify it concurrently?
- [ ] What lock/transaction/unique constraint protects the invariant?
- [ ] What happens if an event/message is delivered twice?
- [ ] What happens if the process crashes after the first durable write but before the second?
- [ ] What happens when cancellation races completion?
- [ ] What happens when shutdown starts while another thread is waiting?
- [ ] What happens when a worker is slow rather than dead?
- [ ] Does this change browser-session affinity or ownership?
- [ ] Can a late result overwrite a newer logical state?

## Negative / adversarial scenarios

- [ ] Exercise or explicitly mark N/A: invalid/empty identifiers.
- [ ] Exercise or explicitly mark N/A: duplicate request/event.
- [ ] Exercise or explicitly mark N/A: unknown enum or node type.
- [ ] Exercise or explicitly mark N/A: timeout at external boundary.
- [ ] Exercise or explicitly mark N/A: cancellation at an inconvenient moment.
- [ ] Exercise or explicitly mark N/A: process shutdown while work is pending.
- [ ] Exercise or explicitly mark N/A: stale data or stale attempt result.
- [ ] Exercise or explicitly mark N/A: tenant mismatch / unauthorized request when relevant.
- [ ] Exercise or explicitly mark N/A: resource exhaustion / queue full when relevant.
- [ ] Exercise or explicitly mark N/A: malformed serialized payload when relevant.

## Explicit no-go items for this milestone

- Do not compare performance until semantic parity is established.
- Do not make Evo the default if parity tests are red.
- Do not invent performance numbers.
- Do not mark a future component as implemented.
- Do not silently change Phase-1 default behavior.
- Do not commit secrets or local credential values.

## Required validation

- [ ] `legacy lifecycle suite`
- [ ] `Evo lifecycle suite`
- [ ] `manual live E2E with configured keys`
- [ ] `production build`
- [ ] Run current Phase-1 `npm test` when production app code is touched.
- [ ] Run `npm run typecheck` when TypeScript is touched.
- [ ] Run `npm run lint` when TypeScript/React is touched.
- [ ] Run CMake/CTest when C++ is touched.
- [ ] Run production build at integration/high-risk milestones.

## Performance evidence rules for this milestone

- If this milestone is not explicitly a benchmark milestone, do not add a resume number just because a timer was printed.
- If timing is collected for a test, label it diagnostic unless benchmark methodology requirements are satisfied.
- If this milestone changes a previously benchmarked hot path, note that old results are no longer final for the new SHA.
- Store benchmark raw data only in the defined benchmark result structure, not ad-hoc README prose.
- A result must identify workload, build mode, hardware, sample count, and commit before it becomes evidence-grade.

## Human intervention triggers

- If A required secret/account is missing. -> report `STATUS: BLOCKED` with exact manual action; do not guess.
- If A system-level prerequisite such as Docker/CMake/compiler requires user installation. -> report `STATUS: BLOCKED` with exact manual action; do not guess.
- If A shared/remote database migration would be applied. -> report `STATUS: BLOCKED` with exact manual action; do not guess.
- If Git author configuration is missing. -> report `STATUS: BLOCKED` with exact manual action; do not guess.
- If A destructive operation would touch user data or history. -> report `STATUS: BLOCKED` with exact manual action; do not guess.
- If A paid external test would incur nontrivial cost and the user has not already authorized it. -> report `STATUS: BLOCKED` with exact manual action; do not guess.

## Diff-review checklist before commit

- [ ] Review every changed file with `git diff`.
- [ ] Run `git diff --check`.
- [ ] Remove accidental formatting churn outside the milestone scope.
- [ ] Confirm no secret or `.env.local` value appears in the diff.
- [ ] Confirm no generated build tree, node_modules, vcpkg cache, Docker volume, or large transient trace has been staged.
- [ ] Confirm comments/README wording describe implemented behavior, not planned behavior.

## Commit requirement

Create one local commit with subject:

`phase2(m29): reach evo engine ui parity`

After commit:

- Record the new SHA in `docs/phase2/PROGRESS.md`.
- Record commands/results for the milestone.
- Do not push unless push permission/policy has already been explicitly authorized.
- If all exit criteria are green, proceed automatically to the next milestone.

## Exit criteria

- [ ] All milestone-specific implementation items are complete.
- [ ] All relevant new tests pass.
- [ ] Required Phase-1 regression gates pass.
- [ ] No unresolved race/data-corruption/security issue is knowingly introduced.
- [ ] No secret or accidental generated artifact is staged.
- [ ] Documentation describes current behavior accurately.
- [ ] The milestone has exactly one intended local commit or a clearly documented reason for a split required by repository tooling.

## Mandatory milestone report format

```text
STATUS: DONE or BLOCKED
MILESTONE: exact milestone number and title
BASE_SHA: SHA before milestone
END_SHA: SHA after commit when DONE
WHAT_I_INSPECTED: files/docs/commands read before changes
WHAT_CHANGED: concise implementation summary
FILES_CHANGED: exact paths
TESTS: command -> result
PHASE1_REGRESSION: command -> result or N/A with reason
CPP_VALIDATION: command -> result or N/A
DISTRIBUTED_VALIDATION: command -> result or N/A
METRICS_CAPTURED: actual measured metrics only; otherwise `None`
HUMAN_ACTION: None or exact required action
KNOWN_LIMITATIONS: explicit remaining limitations
COMMIT: commit SHA + subject
NEXT: next milestone number; continue automatically if not blocked
```

---

# MILESTONE 30 — Implement end-to-end cancellation across app, scheduler, queue, worker, and browser

## Objective

Make Stop meaningful in the distributed system rather than only a UI flag.

## Why this milestone exists

- This milestone isolates **implement end-to-end cancellation across app, scheduler, queue, worker, and browser** so failures are attributable and reviewable.
- The milestone is intentionally narrower than the final Phase-2 architecture.
- Do not implement later milestone responsibilities early unless a tiny interface stub is required for compilation.
- Any stub must be clearly marked as non-production and must not be described as a completed feature.

## Mandatory pre-read and repository inspection

- Read `AGENTS.md` and obey repository-local instructions relevant to files you will touch.
- Run `git status --short` and confirm no unrelated user work will be overwritten.
- Read every existing source file that the milestone will modify before editing it.
- Read the nearest tests covering the behavior before changing implementation.
- Inspect current package/library versions rather than assuming APIs from memory.
- Consult current official documentation before using a third-party API that may have changed.

## Entry conditions

- Milestone 29 is DONE, unless this is Milestone 01.
- Working tree contains no unexpected unrelated modifications.
- The current Phase-1 baseline gate is known and available.
- Any dependency introduced in this milestone has a verified version/source and license acceptable for the repository.

## Detailed implementation procedure

1. Persist cancellation-request timestamp in the engine-neutral run state.
2. CancelRun must atomically prevent future dispatch for that run.
3. Publish/propagate cancellation to workers owning active attempts/affinity resources.
4. Workers must cooperate at node boundaries and close Stagehand/browser resources promptly where possible.
5. Mark not-started logical nodes canceled.
6. Reject late successful results after the run is terminal canceled according to documented rules.
7. Measure scheduler cancellation latency and actual worker/resource-stop latency separately.
8. Test repeated Stop requests and Stop-after-terminal as idempotent/no-op semantics.
9. Before editing, write a short local plan listing the exact files expected to change.
10. Prefer the smallest interface that satisfies this milestone and leaves room for later milestones.
11. Keep names consistent with existing repository conventions; do not introduce a parallel naming universe.
12. If an existing helper already implements part of the behavior, reuse it rather than creating a near-duplicate.
13. Keep product-facing behavior backwards compatible unless the milestone explicitly introduces a new opt-in Evo path.
14. When behavior crosses a trust boundary, validate input before mutating durable state.
15. When behavior crosses a thread/process boundary, define ownership, lifetime, timeout, and failure semantics explicitly.
16. When behavior emits a timestamp, state whether it is wall-clock or steady-clock and use it consistently.
17. Add tests in the same milestone as the behavior; do not defer all testing to Milestone 39.
18. Update `docs/phase2/PROGRESS.md` with the observed result, not a future-tense promise.

## Phase-1 preservation checklist

- [ ] AI planner still requires explicit user Run before execution.
- [ ] Generated graph remains editable React Flow / Liveblocks state.
- [ ] Legacy Trigger.dev engine remains available unless a later explicit final decision says otherwise.
- [ ] Clerk auth/org enforcement remains server-side.
- [ ] Agent Pro-plan gate remains fail-closed.
- [ ] Live browser credentials remain server/worker only.
- [ ] Legacy Run/Stop/results/replay/rerun behavior must not regress.
- [ ] Existing node registry/executors are not duplicated without a compelling reason.
- [ ] No test is removed merely because Phase 2 changes an internal representation.

## Concurrency / distributed correctness questions to answer

- [ ] Which component owns the mutable state introduced here?
- [ ] Can two threads/processes modify it concurrently?
- [ ] What lock/transaction/unique constraint protects the invariant?
- [ ] What happens if an event/message is delivered twice?
- [ ] What happens if the process crashes after the first durable write but before the second?
- [ ] What happens when cancellation races completion?
- [ ] What happens when shutdown starts while another thread is waiting?
- [ ] What happens when a worker is slow rather than dead?
- [ ] Does this change browser-session affinity or ownership?
- [ ] Can a late result overwrite a newer logical state?

## Negative / adversarial scenarios

- [ ] Exercise or explicitly mark N/A: invalid/empty identifiers.
- [ ] Exercise or explicitly mark N/A: duplicate request/event.
- [ ] Exercise or explicitly mark N/A: unknown enum or node type.
- [ ] Exercise or explicitly mark N/A: timeout at external boundary.
- [ ] Exercise or explicitly mark N/A: cancellation at an inconvenient moment.
- [ ] Exercise or explicitly mark N/A: process shutdown while work is pending.
- [ ] Exercise or explicitly mark N/A: stale data or stale attempt result.
- [ ] Exercise or explicitly mark N/A: tenant mismatch / unauthorized request when relevant.
- [ ] Exercise or explicitly mark N/A: resource exhaustion / queue full when relevant.
- [ ] Exercise or explicitly mark N/A: malformed serialized payload when relevant.

## Explicit no-go items for this milestone

- Do not promise instant interruption of an arbitrary in-flight third-party SDK call unless verified.
- No new tasks dispatch after terminal cancellation.
- Do not invent performance numbers.
- Do not mark a future component as implemented.
- Do not silently change Phase-1 default behavior.
- Do not commit secrets or local credential values.

## Required validation

- [ ] `cancel-before-dispatch`
- [ ] `cancel-during-synthetic`
- [ ] `cancel-during-mocked-browser`
- [ ] `manual live browser Stop`
- [ ] `latency raw samples`
- [ ] Run current Phase-1 `npm test` when production app code is touched.
- [ ] Run `npm run typecheck` when TypeScript is touched.
- [ ] Run `npm run lint` when TypeScript/React is touched.
- [ ] Run CMake/CTest when C++ is touched.
- [ ] Run production build at integration/high-risk milestones.

## Performance evidence rules for this milestone

- If this milestone is not explicitly a benchmark milestone, do not add a resume number just because a timer was printed.
- If timing is collected for a test, label it diagnostic unless benchmark methodology requirements are satisfied.
- If this milestone changes a previously benchmarked hot path, note that old results are no longer final for the new SHA.
- Store benchmark raw data only in the defined benchmark result structure, not ad-hoc README prose.
- A result must identify workload, build mode, hardware, sample count, and commit before it becomes evidence-grade.

## Human intervention triggers

- If A required secret/account is missing. -> report `STATUS: BLOCKED` with exact manual action; do not guess.
- If A system-level prerequisite such as Docker/CMake/compiler requires user installation. -> report `STATUS: BLOCKED` with exact manual action; do not guess.
- If A shared/remote database migration would be applied. -> report `STATUS: BLOCKED` with exact manual action; do not guess.
- If Git author configuration is missing. -> report `STATUS: BLOCKED` with exact manual action; do not guess.
- If A destructive operation would touch user data or history. -> report `STATUS: BLOCKED` with exact manual action; do not guess.
- If A paid external test would incur nontrivial cost and the user has not already authorized it. -> report `STATUS: BLOCKED` with exact manual action; do not guess.

## Diff-review checklist before commit

- [ ] Review every changed file with `git diff`.
- [ ] Run `git diff --check`.
- [ ] Remove accidental formatting churn outside the milestone scope.
- [ ] Confirm no secret or `.env.local` value appears in the diff.
- [ ] Confirm no generated build tree, node_modules, vcpkg cache, Docker volume, or large transient trace has been staged.
- [ ] Confirm comments/README wording describe implemented behavior, not planned behavior.

## Commit requirement

Create one local commit with subject:

`phase2(m30): implement distributed cancellation`

After commit:

- Record the new SHA in `docs/phase2/PROGRESS.md`.
- Record commands/results for the milestone.
- Do not push unless push permission/policy has already been explicitly authorized.
- If all exit criteria are green, proceed automatically to the next milestone.

## Exit criteria

- [ ] All milestone-specific implementation items are complete.
- [ ] All relevant new tests pass.
- [ ] Required Phase-1 regression gates pass.
- [ ] No unresolved race/data-corruption/security issue is knowingly introduced.
- [ ] No secret or accidental generated artifact is staged.
- [ ] Documentation describes current behavior accurately.
- [ ] The milestone has exactly one intended local commit or a clearly documented reason for a split required by repository tooling.

## Mandatory milestone report format

```text
STATUS: DONE or BLOCKED
MILESTONE: exact milestone number and title
BASE_SHA: SHA before milestone
END_SHA: SHA after commit when DONE
WHAT_I_INSPECTED: files/docs/commands read before changes
WHAT_CHANGED: concise implementation summary
FILES_CHANGED: exact paths
TESTS: command -> result
PHASE1_REGRESSION: command -> result or N/A with reason
CPP_VALIDATION: command -> result or N/A
DISTRIBUTED_VALIDATION: command -> result or N/A
METRICS_CAPTURED: actual measured metrics only; otherwise `None`
HUMAN_ACTION: None or exact required action
KNOWN_LIMITATIONS: explicit remaining limitations
COMMIT: commit SHA + subject
NEXT: next milestone number; continue automatically if not blocked
```

---

# MILESTONE 31 — Implement worker registry, leases, and heartbeats

## Objective

Detect lost workers/tasks without equating slowness with death.

## Why this milestone exists

- This milestone isolates **implement worker registry, leases, and heartbeats** so failures are attributable and reviewable.
- The milestone is intentionally narrower than the final Phase-2 architecture.
- Do not implement later milestone responsibilities early unless a tiny interface stub is required for compilation.
- Any stub must be clearly marked as non-production and must not be described as a completed feature.

## Mandatory pre-read and repository inspection

- Read `AGENTS.md` and obey repository-local instructions relevant to files you will touch.
- Run `git status --short` and confirm no unrelated user work will be overwritten.
- Read every existing source file that the milestone will modify before editing it.
- Read the nearest tests covering the behavior before changing implementation.
- Inspect current package/library versions rather than assuming APIs from memory.
- Consult current official documentation before using a third-party API that may have changed.

## Entry conditions

- Milestone 30 is DONE, unless this is Milestone 01.
- Working tree contains no unexpected unrelated modifications.
- The current Phase-1 baseline gate is known and available.
- Any dependency introduced in this milestone has a verified version/source and license acceptable for the repository.

## Detailed implementation procedure

1. Define worker heartbeat cadence and expiry separately from task lease duration.
2. Store worker liveness and task lease ownership in a durable/recoverable location.
3. Workers renew active leases while work is legitimately running.
4. Scheduler scans for expired leases using monotonic/durable timestamps as appropriate.
5. Transition expired attempts without immediately declaring the logical node permanently failed.
6. Record worker ID, lease acquired, renewed, expired timestamps in task-attempt evidence.
7. Add controlled slow-worker tests showing a renewing worker is not incorrectly reaped.
8. Add killed-worker tests showing lease expiration is detected.
9. Before editing, write a short local plan listing the exact files expected to change.
10. Prefer the smallest interface that satisfies this milestone and leaves room for later milestones.
11. Keep names consistent with existing repository conventions; do not introduce a parallel naming universe.
12. If an existing helper already implements part of the behavior, reuse it rather than creating a near-duplicate.
13. Keep product-facing behavior backwards compatible unless the milestone explicitly introduces a new opt-in Evo path.
14. When behavior crosses a trust boundary, validate input before mutating durable state.
15. When behavior crosses a thread/process boundary, define ownership, lifetime, timeout, and failure semantics explicitly.
16. When behavior emits a timestamp, state whether it is wall-clock or steady-clock and use it consistently.
17. Add tests in the same milestone as the behavior; do not defer all testing to Milestone 39.
18. Update `docs/phase2/PROGRESS.md` with the observed result, not a future-tense promise.

## Phase-1 preservation checklist

- [ ] AI planner still requires explicit user Run before execution.
- [ ] Generated graph remains editable React Flow / Liveblocks state.
- [ ] Legacy Trigger.dev engine remains available unless a later explicit final decision says otherwise.
- [ ] Clerk auth/org enforcement remains server-side.
- [ ] Agent Pro-plan gate remains fail-closed.
- [ ] Live browser credentials remain server/worker only.
- [ ] Legacy Run/Stop/results/replay/rerun behavior must not regress.
- [ ] Existing node registry/executors are not duplicated without a compelling reason.
- [ ] No test is removed merely because Phase 2 changes an internal representation.

## Concurrency / distributed correctness questions to answer

- [ ] Which component owns the mutable state introduced here?
- [ ] Can two threads/processes modify it concurrently?
- [ ] What lock/transaction/unique constraint protects the invariant?
- [ ] What happens if an event/message is delivered twice?
- [ ] What happens if the process crashes after the first durable write but before the second?
- [ ] What happens when cancellation races completion?
- [ ] What happens when shutdown starts while another thread is waiting?
- [ ] What happens when a worker is slow rather than dead?
- [ ] Does this change browser-session affinity or ownership?
- [ ] Can a late result overwrite a newer logical state?

## Negative / adversarial scenarios

- [ ] Exercise or explicitly mark N/A: invalid/empty identifiers.
- [ ] Exercise or explicitly mark N/A: duplicate request/event.
- [ ] Exercise or explicitly mark N/A: unknown enum or node type.
- [ ] Exercise or explicitly mark N/A: timeout at external boundary.
- [ ] Exercise or explicitly mark N/A: cancellation at an inconvenient moment.
- [ ] Exercise or explicitly mark N/A: process shutdown while work is pending.
- [ ] Exercise or explicitly mark N/A: stale data or stale attempt result.
- [ ] Exercise or explicitly mark N/A: tenant mismatch / unauthorized request when relevant.
- [ ] Exercise or explicitly mark N/A: resource exhaustion / queue full when relevant.
- [ ] Exercise or explicitly mark N/A: malformed serialized payload when relevant.

## Explicit no-go items for this milestone

- Heartbeat alone does not prove task progress; document what it proves.
- Lease expiry must not double-complete the logical node.
- Do not invent performance numbers.
- Do not mark a future component as implemented.
- Do not silently change Phase-1 default behavior.
- Do not commit secrets or local credential values.

## Required validation

- [ ] `heartbeat tests`
- [ ] `slow worker test`
- [ ] `kill worker lease-expiry test`
- [ ] `Postgres/Redis audit assertions`
- [ ] Run current Phase-1 `npm test` when production app code is touched.
- [ ] Run `npm run typecheck` when TypeScript is touched.
- [ ] Run `npm run lint` when TypeScript/React is touched.
- [ ] Run CMake/CTest when C++ is touched.
- [ ] Run production build at integration/high-risk milestones.

## Performance evidence rules for this milestone

- If this milestone is not explicitly a benchmark milestone, do not add a resume number just because a timer was printed.
- If timing is collected for a test, label it diagnostic unless benchmark methodology requirements are satisfied.
- If this milestone changes a previously benchmarked hot path, note that old results are no longer final for the new SHA.
- Store benchmark raw data only in the defined benchmark result structure, not ad-hoc README prose.
- A result must identify workload, build mode, hardware, sample count, and commit before it becomes evidence-grade.

## Human intervention triggers

- If A required secret/account is missing. -> report `STATUS: BLOCKED` with exact manual action; do not guess.
- If A system-level prerequisite such as Docker/CMake/compiler requires user installation. -> report `STATUS: BLOCKED` with exact manual action; do not guess.
- If A shared/remote database migration would be applied. -> report `STATUS: BLOCKED` with exact manual action; do not guess.
- If Git author configuration is missing. -> report `STATUS: BLOCKED` with exact manual action; do not guess.
- If A destructive operation would touch user data or history. -> report `STATUS: BLOCKED` with exact manual action; do not guess.
- If A paid external test would incur nontrivial cost and the user has not already authorized it. -> report `STATUS: BLOCKED` with exact manual action; do not guess.

## Diff-review checklist before commit

- [ ] Review every changed file with `git diff`.
- [ ] Run `git diff --check`.
- [ ] Remove accidental formatting churn outside the milestone scope.
- [ ] Confirm no secret or `.env.local` value appears in the diff.
- [ ] Confirm no generated build tree, node_modules, vcpkg cache, Docker volume, or large transient trace has been staged.
- [ ] Confirm comments/README wording describe implemented behavior, not planned behavior.

## Commit requirement

Create one local commit with subject:

`phase2(m31): add worker leases and heartbeats`

After commit:

- Record the new SHA in `docs/phase2/PROGRESS.md`.
- Record commands/results for the milestone.
- Do not push unless push permission/policy has already been explicitly authorized.
- If all exit criteria are green, proceed automatically to the next milestone.

## Exit criteria

- [ ] All milestone-specific implementation items are complete.
- [ ] All relevant new tests pass.
- [ ] Required Phase-1 regression gates pass.
- [ ] No unresolved race/data-corruption/security issue is knowingly introduced.
- [ ] No secret or accidental generated artifact is staged.
- [ ] Documentation describes current behavior accurately.
- [ ] The milestone has exactly one intended local commit or a clearly documented reason for a split required by repository tooling.

## Mandatory milestone report format

```text
STATUS: DONE or BLOCKED
MILESTONE: exact milestone number and title
BASE_SHA: SHA before milestone
END_SHA: SHA after commit when DONE
WHAT_I_INSPECTED: files/docs/commands read before changes
WHAT_CHANGED: concise implementation summary
FILES_CHANGED: exact paths
TESTS: command -> result
PHASE1_REGRESSION: command -> result or N/A with reason
CPP_VALIDATION: command -> result or N/A
DISTRIBUTED_VALIDATION: command -> result or N/A
METRICS_CAPTURED: actual measured metrics only; otherwise `None`
HUMAN_ACTION: None or exact required action
KNOWN_LIMITATIONS: explicit remaining limitations
COMMIT: commit SHA + subject
NEXT: next milestone number; continue automatically if not blocked
```

---

# MILESTONE 32 — Implement node-level retry policy, exponential backoff, jitter, and dead-lettering

## Objective

Move reliability from whole-workflow retry to explicit logical node attempts in Evo mode.

## Why this milestone exists

- This milestone isolates **implement node-level retry policy, exponential backoff, jitter, and dead-lettering** so failures are attributable and reviewable.
- The milestone is intentionally narrower than the final Phase-2 architecture.
- Do not implement later milestone responsibilities early unless a tiny interface stub is required for compilation.
- Any stub must be clearly marked as non-production and must not be described as a completed feature.

## Mandatory pre-read and repository inspection

- Read `AGENTS.md` and obey repository-local instructions relevant to files you will touch.
- Run `git status --short` and confirm no unrelated user work will be overwritten.
- Read every existing source file that the milestone will modify before editing it.
- Read the nearest tests covering the behavior before changing implementation.
- Inspect current package/library versions rather than assuming APIs from memory.
- Consult current official documentation before using a third-party API that may have changed.

## Entry conditions

- Milestone 31 is DONE, unless this is Milestone 01.
- Working tree contains no unexpected unrelated modifications.
- The current Phase-1 baseline gate is known and available.
- Any dependency introduced in this milestone has a verified version/source and license acceptable for the repository.

## Detailed implementation procedure

1. Define an error taxonomy: transient, permanent, canceled, resource-lost, validation, authorization, unknown.
2. Define default retry policy by node/resource class without changing legacy Trigger.dev behavior.
3. Implement attempt count limits.
4. Implement exponential backoff with bounded jitter using a deterministic seed in tests.
5. Persist next-attempt time and retry reason.
6. Do not block scheduler worker threads while waiting for backoff; use timers/priority scheduling or equivalent.
7. Move exhausted retryable failures to a dead-letter/logical failed state.
8. Expose retry attempt history to run results/diagnostics without overwhelming the normal UI.
9. Test transient-then-success, permanent fail-fast, repeated failure, cancellation during backoff, and retry after worker lease expiry.
10. Before editing, write a short local plan listing the exact files expected to change.
11. Prefer the smallest interface that satisfies this milestone and leaves room for later milestones.
12. Keep names consistent with existing repository conventions; do not introduce a parallel naming universe.
13. If an existing helper already implements part of the behavior, reuse it rather than creating a near-duplicate.
14. Keep product-facing behavior backwards compatible unless the milestone explicitly introduces a new opt-in Evo path.
15. When behavior crosses a trust boundary, validate input before mutating durable state.
16. When behavior crosses a thread/process boundary, define ownership, lifetime, timeout, and failure semantics explicitly.
17. When behavior emits a timestamp, state whether it is wall-clock or steady-clock and use it consistently.
18. Add tests in the same milestone as the behavior; do not defer all testing to Milestone 39.
19. Update `docs/phase2/PROGRESS.md` with the observed result, not a future-tense promise.

## Phase-1 preservation checklist

- [ ] AI planner still requires explicit user Run before execution.
- [ ] Generated graph remains editable React Flow / Liveblocks state.
- [ ] Legacy Trigger.dev engine remains available unless a later explicit final decision says otherwise.
- [ ] Clerk auth/org enforcement remains server-side.
- [ ] Agent Pro-plan gate remains fail-closed.
- [ ] Live browser credentials remain server/worker only.
- [ ] Legacy Run/Stop/results/replay/rerun behavior must not regress.
- [ ] Existing node registry/executors are not duplicated without a compelling reason.
- [ ] No test is removed merely because Phase 2 changes an internal representation.

## Concurrency / distributed correctness questions to answer

- [ ] Which component owns the mutable state introduced here?
- [ ] Can two threads/processes modify it concurrently?
- [ ] What lock/transaction/unique constraint protects the invariant?
- [ ] What happens if an event/message is delivered twice?
- [ ] What happens if the process crashes after the first durable write but before the second?
- [ ] What happens when cancellation races completion?
- [ ] What happens when shutdown starts while another thread is waiting?
- [ ] What happens when a worker is slow rather than dead?
- [ ] Does this change browser-session affinity or ownership?
- [ ] Can a late result overwrite a newer logical state?

## Negative / adversarial scenarios

- [ ] Exercise or explicitly mark N/A: invalid/empty identifiers.
- [ ] Exercise or explicitly mark N/A: duplicate request/event.
- [ ] Exercise or explicitly mark N/A: unknown enum or node type.
- [ ] Exercise or explicitly mark N/A: timeout at external boundary.
- [ ] Exercise or explicitly mark N/A: cancellation at an inconvenient moment.
- [ ] Exercise or explicitly mark N/A: process shutdown while work is pending.
- [ ] Exercise or explicitly mark N/A: stale data or stale attempt result.
- [ ] Exercise or explicitly mark N/A: tenant mismatch / unauthorized request when relevant.
- [ ] Exercise or explicitly mark N/A: resource exhaustion / queue full when relevant.
- [ ] Exercise or explicitly mark N/A: malformed serialized payload when relevant.

## Explicit no-go items for this milestone

- Do not retry authorization/validation errors blindly.
- Do not retry side effects without idempotency strategy.
- Do not invent performance numbers.
- Do not mark a future component as implemented.
- Do not silently change Phase-1 default behavior.
- Do not commit secrets or local credential values.

## Required validation

- [ ] `retry state tests`
- [ ] `backoff bounds test`
- [ ] `DLQ/exhaustion test`
- [ ] `distributed E2E`
- [ ] Run current Phase-1 `npm test` when production app code is touched.
- [ ] Run `npm run typecheck` when TypeScript is touched.
- [ ] Run `npm run lint` when TypeScript/React is touched.
- [ ] Run CMake/CTest when C++ is touched.
- [ ] Run production build at integration/high-risk milestones.

## Performance evidence rules for this milestone

- If this milestone is not explicitly a benchmark milestone, do not add a resume number just because a timer was printed.
- If timing is collected for a test, label it diagnostic unless benchmark methodology requirements are satisfied.
- If this milestone changes a previously benchmarked hot path, note that old results are no longer final for the new SHA.
- Store benchmark raw data only in the defined benchmark result structure, not ad-hoc README prose.
- A result must identify workload, build mode, hardware, sample count, and commit before it becomes evidence-grade.

## Human intervention triggers

- If A required secret/account is missing. -> report `STATUS: BLOCKED` with exact manual action; do not guess.
- If A system-level prerequisite such as Docker/CMake/compiler requires user installation. -> report `STATUS: BLOCKED` with exact manual action; do not guess.
- If A shared/remote database migration would be applied. -> report `STATUS: BLOCKED` with exact manual action; do not guess.
- If Git author configuration is missing. -> report `STATUS: BLOCKED` with exact manual action; do not guess.
- If A destructive operation would touch user data or history. -> report `STATUS: BLOCKED` with exact manual action; do not guess.
- If A paid external test would incur nontrivial cost and the user has not already authorized it. -> report `STATUS: BLOCKED` with exact manual action; do not guess.

## Diff-review checklist before commit

- [ ] Review every changed file with `git diff`.
- [ ] Run `git diff --check`.
- [ ] Remove accidental formatting churn outside the milestone scope.
- [ ] Confirm no secret or `.env.local` value appears in the diff.
- [ ] Confirm no generated build tree, node_modules, vcpkg cache, Docker volume, or large transient trace has been staged.
- [ ] Confirm comments/README wording describe implemented behavior, not planned behavior.

## Commit requirement

Create one local commit with subject:

`phase2(m32): add node retries backoff and dead lettering`

After commit:

- Record the new SHA in `docs/phase2/PROGRESS.md`.
- Record commands/results for the milestone.
- Do not push unless push permission/policy has already been explicitly authorized.
- If all exit criteria are green, proceed automatically to the next milestone.

## Exit criteria

- [ ] All milestone-specific implementation items are complete.
- [ ] All relevant new tests pass.
- [ ] Required Phase-1 regression gates pass.
- [ ] No unresolved race/data-corruption/security issue is knowingly introduced.
- [ ] No secret or accidental generated artifact is staged.
- [ ] Documentation describes current behavior accurately.
- [ ] The milestone has exactly one intended local commit or a clearly documented reason for a split required by repository tooling.

## Mandatory milestone report format

```text
STATUS: DONE or BLOCKED
MILESTONE: exact milestone number and title
BASE_SHA: SHA before milestone
END_SHA: SHA after commit when DONE
WHAT_I_INSPECTED: files/docs/commands read before changes
WHAT_CHANGED: concise implementation summary
FILES_CHANGED: exact paths
TESTS: command -> result
PHASE1_REGRESSION: command -> result or N/A with reason
CPP_VALIDATION: command -> result or N/A
DISTRIBUTED_VALIDATION: command -> result or N/A
METRICS_CAPTURED: actual measured metrics only; otherwise `None`
HUMAN_ACTION: None or exact required action
KNOWN_LIMITATIONS: explicit remaining limitations
COMMIT: commit SHA + subject
NEXT: next milestone number; continue automatically if not blocked
```

---

# MILESTONE 33 — Implement idempotency and duplicate suppression

## Objective

Make at-least-once delivery safe enough to discuss honestly.

## Why this milestone exists

- This milestone isolates **implement idempotency and duplicate suppression** so failures are attributable and reviewable.
- The milestone is intentionally narrower than the final Phase-2 architecture.
- Do not implement later milestone responsibilities early unless a tiny interface stub is required for compilation.
- Any stub must be clearly marked as non-production and must not be described as a completed feature.

## Mandatory pre-read and repository inspection

- Read `AGENTS.md` and obey repository-local instructions relevant to files you will touch.
- Run `git status --short` and confirm no unrelated user work will be overwritten.
- Read every existing source file that the milestone will modify before editing it.
- Read the nearest tests covering the behavior before changing implementation.
- Inspect current package/library versions rather than assuming APIs from memory.
- Consult current official documentation before using a third-party API that may have changed.

## Entry conditions

- Milestone 32 is DONE, unless this is Milestone 01.
- Working tree contains no unexpected unrelated modifications.
- The current Phase-1 baseline gate is known and available.
- Any dependency introduced in this milestone has a verified version/source and license acceptable for the repository.

## Detailed implementation procedure

1. Define a logical operation key, typically derived from run ID + node ID + operation semantics.
2. Create a durable idempotency record with unique constraint.
3. Prevent duplicate logical result application in the scheduler.
4. For pure/read operations, reuse previously committed successful output when a duplicate delivery arrives.
5. For `send-email`, inspect current Resend official support for provider-side idempotency before choosing the external-side-effect strategy.
6. If provider-side idempotency is unavailable, document the unavoidable crash window between external side effect and durable local commit.
7. Use a deterministic fake email sink in failure-injection tests so actual duplicate side effects can be counted.
8. Inject duplicate task deliveries, duplicate results, lost acknowledgements, and crash-after-result scenarios.
9. Create an evidence table showing exactly what duplicate classes are suppressed and which ambiguity remains.
10. Before editing, write a short local plan listing the exact files expected to change.
11. Prefer the smallest interface that satisfies this milestone and leaves room for later milestones.
12. Keep names consistent with existing repository conventions; do not introduce a parallel naming universe.
13. If an existing helper already implements part of the behavior, reuse it rather than creating a near-duplicate.
14. Keep product-facing behavior backwards compatible unless the milestone explicitly introduces a new opt-in Evo path.
15. When behavior crosses a trust boundary, validate input before mutating durable state.
16. When behavior crosses a thread/process boundary, define ownership, lifetime, timeout, and failure semantics explicitly.
17. When behavior emits a timestamp, state whether it is wall-clock or steady-clock and use it consistently.
18. Add tests in the same milestone as the behavior; do not defer all testing to Milestone 39.
19. Update `docs/phase2/PROGRESS.md` with the observed result, not a future-tense promise.

## Phase-1 preservation checklist

- [ ] AI planner still requires explicit user Run before execution.
- [ ] Generated graph remains editable React Flow / Liveblocks state.
- [ ] Legacy Trigger.dev engine remains available unless a later explicit final decision says otherwise.
- [ ] Clerk auth/org enforcement remains server-side.
- [ ] Agent Pro-plan gate remains fail-closed.
- [ ] Live browser credentials remain server/worker only.
- [ ] Legacy Run/Stop/results/replay/rerun behavior must not regress.
- [ ] Existing node registry/executors are not duplicated without a compelling reason.
- [ ] No test is removed merely because Phase 2 changes an internal representation.

## Concurrency / distributed correctness questions to answer

- [ ] Which component owns the mutable state introduced here?
- [ ] Can two threads/processes modify it concurrently?
- [ ] What lock/transaction/unique constraint protects the invariant?
- [ ] What happens if an event/message is delivered twice?
- [ ] What happens if the process crashes after the first durable write but before the second?
- [ ] What happens when cancellation races completion?
- [ ] What happens when shutdown starts while another thread is waiting?
- [ ] What happens when a worker is slow rather than dead?
- [ ] Does this change browser-session affinity or ownership?
- [ ] Can a late result overwrite a newer logical state?

## Negative / adversarial scenarios

- [ ] Exercise or explicitly mark N/A: invalid/empty identifiers.
- [ ] Exercise or explicitly mark N/A: duplicate request/event.
- [ ] Exercise or explicitly mark N/A: unknown enum or node type.
- [ ] Exercise or explicitly mark N/A: timeout at external boundary.
- [ ] Exercise or explicitly mark N/A: cancellation at an inconvenient moment.
- [ ] Exercise or explicitly mark N/A: process shutdown while work is pending.
- [ ] Exercise or explicitly mark N/A: stale data or stale attempt result.
- [ ] Exercise or explicitly mark N/A: tenant mismatch / unauthorized request when relevant.
- [ ] Exercise or explicitly mark N/A: resource exhaustion / queue full when relevant.
- [ ] Exercise or explicitly mark N/A: malformed serialized payload when relevant.

## Explicit no-go items for this milestone

- Never claim exactly-once.
- Application idempotency ledger alone does not eliminate every external side-effect ambiguity.
- Do not invent performance numbers.
- Do not mark a future component as implemented.
- Do not silently change Phase-1 default behavior.
- Do not commit secrets or local credential values.

## Required validation

- [ ] `duplicate delivery tests`
- [ ] `duplicate result tests`
- [ ] `fake side-effect count assertions`
- [ ] `DB unique constraint tests`
- [ ] Run current Phase-1 `npm test` when production app code is touched.
- [ ] Run `npm run typecheck` when TypeScript is touched.
- [ ] Run `npm run lint` when TypeScript/React is touched.
- [ ] Run CMake/CTest when C++ is touched.
- [ ] Run production build at integration/high-risk milestones.

## Performance evidence rules for this milestone

- If this milestone is not explicitly a benchmark milestone, do not add a resume number just because a timer was printed.
- If timing is collected for a test, label it diagnostic unless benchmark methodology requirements are satisfied.
- If this milestone changes a previously benchmarked hot path, note that old results are no longer final for the new SHA.
- Store benchmark raw data only in the defined benchmark result structure, not ad-hoc README prose.
- A result must identify workload, build mode, hardware, sample count, and commit before it becomes evidence-grade.

## Human intervention triggers

- If A required secret/account is missing. -> report `STATUS: BLOCKED` with exact manual action; do not guess.
- If A system-level prerequisite such as Docker/CMake/compiler requires user installation. -> report `STATUS: BLOCKED` with exact manual action; do not guess.
- If A shared/remote database migration would be applied. -> report `STATUS: BLOCKED` with exact manual action; do not guess.
- If Git author configuration is missing. -> report `STATUS: BLOCKED` with exact manual action; do not guess.
- If A destructive operation would touch user data or history. -> report `STATUS: BLOCKED` with exact manual action; do not guess.
- If A paid external test would incur nontrivial cost and the user has not already authorized it. -> report `STATUS: BLOCKED` with exact manual action; do not guess.

## Diff-review checklist before commit

- [ ] Review every changed file with `git diff`.
- [ ] Run `git diff --check`.
- [ ] Remove accidental formatting churn outside the milestone scope.
- [ ] Confirm no secret or `.env.local` value appears in the diff.
- [ ] Confirm no generated build tree, node_modules, vcpkg cache, Docker volume, or large transient trace has been staged.
- [ ] Confirm comments/README wording describe implemented behavior, not planned behavior.

## Commit requirement

Create one local commit with subject:

`phase2(m33): add idempotency and duplicate suppression`

After commit:

- Record the new SHA in `docs/phase2/PROGRESS.md`.
- Record commands/results for the milestone.
- Do not push unless push permission/policy has already been explicitly authorized.
- If all exit criteria are green, proceed automatically to the next milestone.

## Exit criteria

- [ ] All milestone-specific implementation items are complete.
- [ ] All relevant new tests pass.
- [ ] Required Phase-1 regression gates pass.
- [ ] No unresolved race/data-corruption/security issue is knowingly introduced.
- [ ] No secret or accidental generated artifact is staged.
- [ ] Documentation describes current behavior accurately.
- [ ] The milestone has exactly one intended local commit or a clearly documented reason for a split required by repository tooling.

## Mandatory milestone report format

```text
STATUS: DONE or BLOCKED
MILESTONE: exact milestone number and title
BASE_SHA: SHA before milestone
END_SHA: SHA after commit when DONE
WHAT_I_INSPECTED: files/docs/commands read before changes
WHAT_CHANGED: concise implementation summary
FILES_CHANGED: exact paths
TESTS: command -> result
PHASE1_REGRESSION: command -> result or N/A with reason
CPP_VALIDATION: command -> result or N/A
DISTRIBUTED_VALIDATION: command -> result or N/A
METRICS_CAPTURED: actual measured metrics only; otherwise `None`
HUMAN_ACTION: None or exact required action
KNOWN_LIMITATIONS: explicit remaining limitations
COMMIT: commit SHA + subject
NEXT: next milestone number; continue automatically if not blocked
```

---

# MILESTONE 34 — Implement worker crash recovery and failure injection

## Objective

Demonstrate task reassignment rather than merely describing it.

## Why this milestone exists

- This milestone isolates **implement worker crash recovery and failure injection** so failures are attributable and reviewable.
- The milestone is intentionally narrower than the final Phase-2 architecture.
- Do not implement later milestone responsibilities early unless a tiny interface stub is required for compilation.
- Any stub must be clearly marked as non-production and must not be described as a completed feature.

## Mandatory pre-read and repository inspection

- Read `AGENTS.md` and obey repository-local instructions relevant to files you will touch.
- Run `git status --short` and confirm no unrelated user work will be overwritten.
- Read every existing source file that the milestone will modify before editing it.
- Read the nearest tests covering the behavior before changing implementation.
- Inspect current package/library versions rather than assuming APIs from memory.
- Consult current official documentation before using a third-party API that may have changed.

## Entry conditions

- Milestone 33 is DONE, unless this is Milestone 01.
- Working tree contains no unexpected unrelated modifications.
- The current Phase-1 baseline gate is known and available.
- Any dependency introduced in this milestone has a verified version/source and license acceptable for the repository.

## Detailed implementation procedure

1. Create a fault-injection harness that can terminate a selected worker process/container during a task.
2. For synthetic/idempotent tasks, let lease expiry trigger a replacement attempt on another worker.
3. Record failure injection timestamp, lease expiry, replacement dispatch, replacement start, and logical completion.
4. Verify no logical task is lost.
5. Verify duplicate late completion does not corrupt state.
6. For browser-affinity worker death, implement the documented resource-loss policy: either restart the browser resource chain under an explicit policy or fail the run clearly; do not claim transparent session continuation unless proven.
7. Create raw recovery benchmark artifacts across multiple trials.
8. Document recovery behavior separately for synthetic/non-browser and browser-affinity work.
9. Before editing, write a short local plan listing the exact files expected to change.
10. Prefer the smallest interface that satisfies this milestone and leaves room for later milestones.
11. Keep names consistent with existing repository conventions; do not introduce a parallel naming universe.
12. If an existing helper already implements part of the behavior, reuse it rather than creating a near-duplicate.
13. Keep product-facing behavior backwards compatible unless the milestone explicitly introduces a new opt-in Evo path.
14. When behavior crosses a trust boundary, validate input before mutating durable state.
15. When behavior crosses a thread/process boundary, define ownership, lifetime, timeout, and failure semantics explicitly.
16. When behavior emits a timestamp, state whether it is wall-clock or steady-clock and use it consistently.
17. Add tests in the same milestone as the behavior; do not defer all testing to Milestone 39.
18. Update `docs/phase2/PROGRESS.md` with the observed result, not a future-tense promise.

## Phase-1 preservation checklist

- [ ] AI planner still requires explicit user Run before execution.
- [ ] Generated graph remains editable React Flow / Liveblocks state.
- [ ] Legacy Trigger.dev engine remains available unless a later explicit final decision says otherwise.
- [ ] Clerk auth/org enforcement remains server-side.
- [ ] Agent Pro-plan gate remains fail-closed.
- [ ] Live browser credentials remain server/worker only.
- [ ] Legacy Run/Stop/results/replay/rerun behavior must not regress.
- [ ] Existing node registry/executors are not duplicated without a compelling reason.
- [ ] No test is removed merely because Phase 2 changes an internal representation.

## Concurrency / distributed correctness questions to answer

- [ ] Which component owns the mutable state introduced here?
- [ ] Can two threads/processes modify it concurrently?
- [ ] What lock/transaction/unique constraint protects the invariant?
- [ ] What happens if an event/message is delivered twice?
- [ ] What happens if the process crashes after the first durable write but before the second?
- [ ] What happens when cancellation races completion?
- [ ] What happens when shutdown starts while another thread is waiting?
- [ ] What happens when a worker is slow rather than dead?
- [ ] Does this change browser-session affinity or ownership?
- [ ] Can a late result overwrite a newer logical state?

## Negative / adversarial scenarios

- [ ] Exercise or explicitly mark N/A: invalid/empty identifiers.
- [ ] Exercise or explicitly mark N/A: duplicate request/event.
- [ ] Exercise or explicitly mark N/A: unknown enum or node type.
- [ ] Exercise or explicitly mark N/A: timeout at external boundary.
- [ ] Exercise or explicitly mark N/A: cancellation at an inconvenient moment.
- [ ] Exercise or explicitly mark N/A: process shutdown while work is pending.
- [ ] Exercise or explicitly mark N/A: stale data or stale attempt result.
- [ ] Exercise or explicitly mark N/A: tenant mismatch / unauthorized request when relevant.
- [ ] Exercise or explicitly mark N/A: resource exhaustion / queue full when relevant.
- [ ] Exercise or explicitly mark N/A: malformed serialized payload when relevant.

## Explicit no-go items for this milestone

- A test that only kills an idle worker is insufficient.
- Recovery claims must specify workload/resource class.
- Do not invent performance numbers.
- Do not mark a future component as implemented.
- Do not silently change Phase-1 default behavior.
- Do not commit secrets or local credential values.

## Required validation

- [ ] `kill active worker test`
- [ ] `reassignment test`
- [ ] `lost-task count`
- [ ] `recovery-time raw samples`
- [ ] Run current Phase-1 `npm test` when production app code is touched.
- [ ] Run `npm run typecheck` when TypeScript is touched.
- [ ] Run `npm run lint` when TypeScript/React is touched.
- [ ] Run CMake/CTest when C++ is touched.
- [ ] Run production build at integration/high-risk milestones.

## Performance evidence rules for this milestone

- If this milestone is not explicitly a benchmark milestone, do not add a resume number just because a timer was printed.
- If timing is collected for a test, label it diagnostic unless benchmark methodology requirements are satisfied.
- If this milestone changes a previously benchmarked hot path, note that old results are no longer final for the new SHA.
- Store benchmark raw data only in the defined benchmark result structure, not ad-hoc README prose.
- A result must identify workload, build mode, hardware, sample count, and commit before it becomes evidence-grade.

## Human intervention triggers

- If A required secret/account is missing. -> report `STATUS: BLOCKED` with exact manual action; do not guess.
- If A system-level prerequisite such as Docker/CMake/compiler requires user installation. -> report `STATUS: BLOCKED` with exact manual action; do not guess.
- If A shared/remote database migration would be applied. -> report `STATUS: BLOCKED` with exact manual action; do not guess.
- If Git author configuration is missing. -> report `STATUS: BLOCKED` with exact manual action; do not guess.
- If A destructive operation would touch user data or history. -> report `STATUS: BLOCKED` with exact manual action; do not guess.
- If A paid external test would incur nontrivial cost and the user has not already authorized it. -> report `STATUS: BLOCKED` with exact manual action; do not guess.

## Diff-review checklist before commit

- [ ] Review every changed file with `git diff`.
- [ ] Run `git diff --check`.
- [ ] Remove accidental formatting churn outside the milestone scope.
- [ ] Confirm no secret or `.env.local` value appears in the diff.
- [ ] Confirm no generated build tree, node_modules, vcpkg cache, Docker volume, or large transient trace has been staged.
- [ ] Confirm comments/README wording describe implemented behavior, not planned behavior.

## Commit requirement

Create one local commit with subject:

`phase2(m34): implement and measure worker crash recovery`

After commit:

- Record the new SHA in `docs/phase2/PROGRESS.md`.
- Record commands/results for the milestone.
- Do not push unless push permission/policy has already been explicitly authorized.
- If all exit criteria are green, proceed automatically to the next milestone.

## Exit criteria

- [ ] All milestone-specific implementation items are complete.
- [ ] All relevant new tests pass.
- [ ] Required Phase-1 regression gates pass.
- [ ] No unresolved race/data-corruption/security issue is knowingly introduced.
- [ ] No secret or accidental generated artifact is staged.
- [ ] Documentation describes current behavior accurately.
- [ ] The milestone has exactly one intended local commit or a clearly documented reason for a split required by repository tooling.

## Mandatory milestone report format

```text
STATUS: DONE or BLOCKED
MILESTONE: exact milestone number and title
BASE_SHA: SHA before milestone
END_SHA: SHA after commit when DONE
WHAT_I_INSPECTED: files/docs/commands read before changes
WHAT_CHANGED: concise implementation summary
FILES_CHANGED: exact paths
TESTS: command -> result
PHASE1_REGRESSION: command -> result or N/A with reason
CPP_VALIDATION: command -> result or N/A
DISTRIBUTED_VALIDATION: command -> result or N/A
METRICS_CAPTURED: actual measured metrics only; otherwise `None`
HUMAN_ACTION: None or exact required action
KNOWN_LIMITATIONS: explicit remaining limitations
COMMIT: commit SHA + subject
NEXT: next milestone number; continue automatically if not blocked
```

---

# MILESTONE 35 — Implement scheduler restart recovery and durable reconciliation

## Objective

Make the orchestrator restartable without forgetting active runs.

## Why this milestone exists

- This milestone isolates **implement scheduler restart recovery and durable reconciliation** so failures are attributable and reviewable.
- The milestone is intentionally narrower than the final Phase-2 architecture.
- Do not implement later milestone responsibilities early unless a tiny interface stub is required for compilation.
- Any stub must be clearly marked as non-production and must not be described as a completed feature.

## Mandatory pre-read and repository inspection

- Read `AGENTS.md` and obey repository-local instructions relevant to files you will touch.
- Run `git status --short` and confirm no unrelated user work will be overwritten.
- Read every existing source file that the milestone will modify before editing it.
- Read the nearest tests covering the behavior before changing implementation.
- Inspect current package/library versions rather than assuming APIs from memory.
- Consult current official documentation before using a third-party API that may have changed.

## Entry conditions

- Milestone 34 is DONE, unless this is Milestone 01.
- Working tree contains no unexpected unrelated modifications.
- The current Phase-1 baseline gate is known and available.
- Any dependency introduced in this milestone has a verified version/source and license acceptable for the repository.

## Detailed implementation procedure

1. Persist enough run topology/logical state/attempt state to reconstruct active runs.
2. On scheduler startup, identify nonterminal runs owned by Evo engine.
3. Reconcile Postgres logical state with Redis pending/in-flight task state.
4. Do not dispatch a duplicate replacement merely because the scheduler restarted if a valid lease still exists.
5. Resume dependency scheduling for READY work.
6. Handle orphaned expired attempts using the same retry policy as ordinary lease expiry.
7. Add an integration test that kills the scheduler while workers/Redis/Postgres remain alive, restarts it, and verifies eventual consistent terminal outcome.
8. Add a test for restart while no work is active.
9. Before editing, write a short local plan listing the exact files expected to change.
10. Prefer the smallest interface that satisfies this milestone and leaves room for later milestones.
11. Keep names consistent with existing repository conventions; do not introduce a parallel naming universe.
12. If an existing helper already implements part of the behavior, reuse it rather than creating a near-duplicate.
13. Keep product-facing behavior backwards compatible unless the milestone explicitly introduces a new opt-in Evo path.
14. When behavior crosses a trust boundary, validate input before mutating durable state.
15. When behavior crosses a thread/process boundary, define ownership, lifetime, timeout, and failure semantics explicitly.
16. When behavior emits a timestamp, state whether it is wall-clock or steady-clock and use it consistently.
17. Add tests in the same milestone as the behavior; do not defer all testing to Milestone 39.
18. Update `docs/phase2/PROGRESS.md` with the observed result, not a future-tense promise.

## Phase-1 preservation checklist

- [ ] AI planner still requires explicit user Run before execution.
- [ ] Generated graph remains editable React Flow / Liveblocks state.
- [ ] Legacy Trigger.dev engine remains available unless a later explicit final decision says otherwise.
- [ ] Clerk auth/org enforcement remains server-side.
- [ ] Agent Pro-plan gate remains fail-closed.
- [ ] Live browser credentials remain server/worker only.
- [ ] Legacy Run/Stop/results/replay/rerun behavior must not regress.
- [ ] Existing node registry/executors are not duplicated without a compelling reason.
- [ ] No test is removed merely because Phase 2 changes an internal representation.

## Concurrency / distributed correctness questions to answer

- [ ] Which component owns the mutable state introduced here?
- [ ] Can two threads/processes modify it concurrently?
- [ ] What lock/transaction/unique constraint protects the invariant?
- [ ] What happens if an event/message is delivered twice?
- [ ] What happens if the process crashes after the first durable write but before the second?
- [ ] What happens when cancellation races completion?
- [ ] What happens when shutdown starts while another thread is waiting?
- [ ] What happens when a worker is slow rather than dead?
- [ ] Does this change browser-session affinity or ownership?
- [ ] Can a late result overwrite a newer logical state?

## Negative / adversarial scenarios

- [ ] Exercise or explicitly mark N/A: invalid/empty identifiers.
- [ ] Exercise or explicitly mark N/A: duplicate request/event.
- [ ] Exercise or explicitly mark N/A: unknown enum or node type.
- [ ] Exercise or explicitly mark N/A: timeout at external boundary.
- [ ] Exercise or explicitly mark N/A: cancellation at an inconvenient moment.
- [ ] Exercise or explicitly mark N/A: process shutdown while work is pending.
- [ ] Exercise or explicitly mark N/A: stale data or stale attempt result.
- [ ] Exercise or explicitly mark N/A: tenant mismatch / unauthorized request when relevant.
- [ ] Exercise or explicitly mark N/A: resource exhaustion / queue full when relevant.
- [ ] Exercise or explicitly mark N/A: malformed serialized payload when relevant.

## Explicit no-go items for this milestone

- Redis alone is not the audit database; Postgres remains the durable source for run history.
- Document any small restart window that is not recoverable.
- Do not invent performance numbers.
- Do not mark a future component as implemented.
- Do not silently change Phase-1 default behavior.
- Do not commit secrets or local credential values.

## Required validation

- [ ] `scheduler kill/restart test`
- [ ] `no duplicate logical completion`
- [ ] `Postgres state reconciliation assertions`
- [ ] Run current Phase-1 `npm test` when production app code is touched.
- [ ] Run `npm run typecheck` when TypeScript is touched.
- [ ] Run `npm run lint` when TypeScript/React is touched.
- [ ] Run CMake/CTest when C++ is touched.
- [ ] Run production build at integration/high-risk milestones.

## Performance evidence rules for this milestone

- If this milestone is not explicitly a benchmark milestone, do not add a resume number just because a timer was printed.
- If timing is collected for a test, label it diagnostic unless benchmark methodology requirements are satisfied.
- If this milestone changes a previously benchmarked hot path, note that old results are no longer final for the new SHA.
- Store benchmark raw data only in the defined benchmark result structure, not ad-hoc README prose.
- A result must identify workload, build mode, hardware, sample count, and commit before it becomes evidence-grade.

## Human intervention triggers

- If A required secret/account is missing. -> report `STATUS: BLOCKED` with exact manual action; do not guess.
- If A system-level prerequisite such as Docker/CMake/compiler requires user installation. -> report `STATUS: BLOCKED` with exact manual action; do not guess.
- If A shared/remote database migration would be applied. -> report `STATUS: BLOCKED` with exact manual action; do not guess.
- If Git author configuration is missing. -> report `STATUS: BLOCKED` with exact manual action; do not guess.
- If A destructive operation would touch user data or history. -> report `STATUS: BLOCKED` with exact manual action; do not guess.
- If A paid external test would incur nontrivial cost and the user has not already authorized it. -> report `STATUS: BLOCKED` with exact manual action; do not guess.

## Diff-review checklist before commit

- [ ] Review every changed file with `git diff`.
- [ ] Run `git diff --check`.
- [ ] Remove accidental formatting churn outside the milestone scope.
- [ ] Confirm no secret or `.env.local` value appears in the diff.
- [ ] Confirm no generated build tree, node_modules, vcpkg cache, Docker volume, or large transient trace has been staged.
- [ ] Confirm comments/README wording describe implemented behavior, not planned behavior.

## Commit requirement

Create one local commit with subject:

`phase2(m35): add scheduler restart recovery`

After commit:

- Record the new SHA in `docs/phase2/PROGRESS.md`.
- Record commands/results for the milestone.
- Do not push unless push permission/policy has already been explicitly authorized.
- If all exit criteria are green, proceed automatically to the next milestone.

## Exit criteria

- [ ] All milestone-specific implementation items are complete.
- [ ] All relevant new tests pass.
- [ ] Required Phase-1 regression gates pass.
- [ ] No unresolved race/data-corruption/security issue is knowingly introduced.
- [ ] No secret or accidental generated artifact is staged.
- [ ] Documentation describes current behavior accurately.
- [ ] The milestone has exactly one intended local commit or a clearly documented reason for a split required by repository tooling.

## Mandatory milestone report format

```text
STATUS: DONE or BLOCKED
MILESTONE: exact milestone number and title
BASE_SHA: SHA before milestone
END_SHA: SHA after commit when DONE
WHAT_I_INSPECTED: files/docs/commands read before changes
WHAT_CHANGED: concise implementation summary
FILES_CHANGED: exact paths
TESTS: command -> result
PHASE1_REGRESSION: command -> result or N/A with reason
CPP_VALIDATION: command -> result or N/A
DISTRIBUTED_VALIDATION: command -> result or N/A
METRICS_CAPTURED: actual measured metrics only; otherwise `None`
HUMAN_ACTION: None or exact required action
KNOWN_LIMITATIONS: explicit remaining limitations
COMMIT: commit SHA + subject
NEXT: next milestone number; continue automatically if not blocked
```

---

# MILESTONE 36 — Add multi-tenant quotas and backpressure

## Objective

Prevent one organization from exhausting global resources.

## Why this milestone exists

- This milestone isolates **add multi-tenant quotas and backpressure** so failures are attributable and reviewable.
- The milestone is intentionally narrower than the final Phase-2 architecture.
- Do not implement later milestone responsibilities early unless a tiny interface stub is required for compilation.
- Any stub must be clearly marked as non-production and must not be described as a completed feature.

## Mandatory pre-read and repository inspection

- Read `AGENTS.md` and obey repository-local instructions relevant to files you will touch.
- Run `git status --short` and confirm no unrelated user work will be overwritten.
- Read every existing source file that the milestone will modify before editing it.
- Read the nearest tests covering the behavior before changing implementation.
- Inspect current package/library versions rather than assuming APIs from memory.
- Consult current official documentation before using a third-party API that may have changed.

## Entry conditions

- Milestone 35 is DONE, unless this is Milestone 01.
- Working tree contains no unexpected unrelated modifications.
- The current Phase-1 baseline gate is known and available.
- Any dependency introduced in this milestone has a verified version/source and license acceptable for the repository.

## Detailed implementation procedure

1. Use org ID already present in authenticated run submission as the scheduling tenant key.
2. Add configurable per-org in-flight logical task/run limits.
3. Add global resource-class capacities, especially browser-session capacity.
4. Define admission behavior when limits are exceeded: queued vs rejected with resource-exhausted status.
5. Bound internal queue growth.
6. Expose queue depth and rejected/deferred counters.
7. Test one noisy tenant plus one small tenant.
8. Test global browser capacity and side-effect capacity separately.
9. Do not trust org ID supplied by a browser client; it must originate from authenticated server-side submission.
10. Before editing, write a short local plan listing the exact files expected to change.
11. Prefer the smallest interface that satisfies this milestone and leaves room for later milestones.
12. Keep names consistent with existing repository conventions; do not introduce a parallel naming universe.
13. If an existing helper already implements part of the behavior, reuse it rather than creating a near-duplicate.
14. Keep product-facing behavior backwards compatible unless the milestone explicitly introduces a new opt-in Evo path.
15. When behavior crosses a trust boundary, validate input before mutating durable state.
16. When behavior crosses a thread/process boundary, define ownership, lifetime, timeout, and failure semantics explicitly.
17. When behavior emits a timestamp, state whether it is wall-clock or steady-clock and use it consistently.
18. Add tests in the same milestone as the behavior; do not defer all testing to Milestone 39.
19. Update `docs/phase2/PROGRESS.md` with the observed result, not a future-tense promise.

## Phase-1 preservation checklist

- [ ] AI planner still requires explicit user Run before execution.
- [ ] Generated graph remains editable React Flow / Liveblocks state.
- [ ] Legacy Trigger.dev engine remains available unless a later explicit final decision says otherwise.
- [ ] Clerk auth/org enforcement remains server-side.
- [ ] Agent Pro-plan gate remains fail-closed.
- [ ] Live browser credentials remain server/worker only.
- [ ] Legacy Run/Stop/results/replay/rerun behavior must not regress.
- [ ] Existing node registry/executors are not duplicated without a compelling reason.
- [ ] No test is removed merely because Phase 2 changes an internal representation.

## Concurrency / distributed correctness questions to answer

- [ ] Which component owns the mutable state introduced here?
- [ ] Can two threads/processes modify it concurrently?
- [ ] What lock/transaction/unique constraint protects the invariant?
- [ ] What happens if an event/message is delivered twice?
- [ ] What happens if the process crashes after the first durable write but before the second?
- [ ] What happens when cancellation races completion?
- [ ] What happens when shutdown starts while another thread is waiting?
- [ ] What happens when a worker is slow rather than dead?
- [ ] Does this change browser-session affinity or ownership?
- [ ] Can a late result overwrite a newer logical state?

## Negative / adversarial scenarios

- [ ] Exercise or explicitly mark N/A: invalid/empty identifiers.
- [ ] Exercise or explicitly mark N/A: duplicate request/event.
- [ ] Exercise or explicitly mark N/A: unknown enum or node type.
- [ ] Exercise or explicitly mark N/A: timeout at external boundary.
- [ ] Exercise or explicitly mark N/A: cancellation at an inconvenient moment.
- [ ] Exercise or explicitly mark N/A: process shutdown while work is pending.
- [ ] Exercise or explicitly mark N/A: stale data or stale attempt result.
- [ ] Exercise or explicitly mark N/A: tenant mismatch / unauthorized request when relevant.
- [ ] Exercise or explicitly mark N/A: resource exhaustion / queue full when relevant.
- [ ] Exercise or explicitly mark N/A: malformed serialized payload when relevant.

## Explicit no-go items for this milestone

- Backpressure is not fairness by itself.
- Do not create unbounded per-tenant queues.
- Do not invent performance numbers.
- Do not mark a future component as implemented.
- Do not silently change Phase-1 default behavior.
- Do not commit secrets or local credential values.

## Required validation

- [ ] `quota tests`
- [ ] `queue bound tests`
- [ ] `resource exhaustion tests`
- [ ] `tenant isolation tests`
- [ ] Run current Phase-1 `npm test` when production app code is touched.
- [ ] Run `npm run typecheck` when TypeScript is touched.
- [ ] Run `npm run lint` when TypeScript/React is touched.
- [ ] Run CMake/CTest when C++ is touched.
- [ ] Run production build at integration/high-risk milestones.

## Performance evidence rules for this milestone

- If this milestone is not explicitly a benchmark milestone, do not add a resume number just because a timer was printed.
- If timing is collected for a test, label it diagnostic unless benchmark methodology requirements are satisfied.
- If this milestone changes a previously benchmarked hot path, note that old results are no longer final for the new SHA.
- Store benchmark raw data only in the defined benchmark result structure, not ad-hoc README prose.
- A result must identify workload, build mode, hardware, sample count, and commit before it becomes evidence-grade.

## Human intervention triggers

- If A required secret/account is missing. -> report `STATUS: BLOCKED` with exact manual action; do not guess.
- If A system-level prerequisite such as Docker/CMake/compiler requires user installation. -> report `STATUS: BLOCKED` with exact manual action; do not guess.
- If A shared/remote database migration would be applied. -> report `STATUS: BLOCKED` with exact manual action; do not guess.
- If Git author configuration is missing. -> report `STATUS: BLOCKED` with exact manual action; do not guess.
- If A destructive operation would touch user data or history. -> report `STATUS: BLOCKED` with exact manual action; do not guess.
- If A paid external test would incur nontrivial cost and the user has not already authorized it. -> report `STATUS: BLOCKED` with exact manual action; do not guess.

## Diff-review checklist before commit

- [ ] Review every changed file with `git diff`.
- [ ] Run `git diff --check`.
- [ ] Remove accidental formatting churn outside the milestone scope.
- [ ] Confirm no secret or `.env.local` value appears in the diff.
- [ ] Confirm no generated build tree, node_modules, vcpkg cache, Docker volume, or large transient trace has been staged.
- [ ] Confirm comments/README wording describe implemented behavior, not planned behavior.

## Commit requirement

Create one local commit with subject:

`phase2(m36): add tenant quotas and backpressure`

After commit:

- Record the new SHA in `docs/phase2/PROGRESS.md`.
- Record commands/results for the milestone.
- Do not push unless push permission/policy has already been explicitly authorized.
- If all exit criteria are green, proceed automatically to the next milestone.

## Exit criteria

- [ ] All milestone-specific implementation items are complete.
- [ ] All relevant new tests pass.
- [ ] Required Phase-1 regression gates pass.
- [ ] No unresolved race/data-corruption/security issue is knowingly introduced.
- [ ] No secret or accidental generated artifact is staged.
- [ ] Documentation describes current behavior accurately.
- [ ] The milestone has exactly one intended local commit or a clearly documented reason for a split required by repository tooling.

## Mandatory milestone report format

```text
STATUS: DONE or BLOCKED
MILESTONE: exact milestone number and title
BASE_SHA: SHA before milestone
END_SHA: SHA after commit when DONE
WHAT_I_INSPECTED: files/docs/commands read before changes
WHAT_CHANGED: concise implementation summary
FILES_CHANGED: exact paths
TESTS: command -> result
PHASE1_REGRESSION: command -> result or N/A with reason
CPP_VALIDATION: command -> result or N/A
DISTRIBUTED_VALIDATION: command -> result or N/A
METRICS_CAPTURED: actual measured metrics only; otherwise `None`
HUMAN_ACTION: None or exact required action
KNOWN_LIMITATIONS: explicit remaining limitations
COMMIT: commit SHA + subject
NEXT: next milestone number; continue automatically if not blocked
```

---

# MILESTONE 37 — Implement fair scheduling and starvation resistance

## Objective

Provide a defensible multi-tenant scheduler rather than FIFO monopoly.

## Why this milestone exists

- This milestone isolates **implement fair scheduling and starvation resistance** so failures are attributable and reviewable.
- The milestone is intentionally narrower than the final Phase-2 architecture.
- Do not implement later milestone responsibilities early unless a tiny interface stub is required for compilation.
- Any stub must be clearly marked as non-production and must not be described as a completed feature.

## Mandatory pre-read and repository inspection

- Read `AGENTS.md` and obey repository-local instructions relevant to files you will touch.
- Run `git status --short` and confirm no unrelated user work will be overwritten.
- Read every existing source file that the milestone will modify before editing it.
- Read the nearest tests covering the behavior before changing implementation.
- Inspect current package/library versions rather than assuming APIs from memory.
- Consult current official documentation before using a third-party API that may have changed.

## Entry conditions

- Milestone 36 is DONE, unless this is Milestone 01.
- Working tree contains no unexpected unrelated modifications.
- The current Phase-1 baseline gate is known and available.
- Any dependency introduced in this milestone has a verified version/source and license acceptable for the repository.

## Detailed implementation procedure

1. Choose a simple explainable fairness algorithm such as weighted round robin or deficit round robin after documenting tradeoffs.
2. Keep dependency readiness/resource affinity separate from tenant selection.
3. Add per-org ready queues or equivalent structure.
4. Support equal weights first; optional weights must be explicit configuration.
5. Test that a large tenant backlog does not indefinitely starve a small tenant.
6. Measure per-tenant wait distributions and Jain fairness index on controlled workloads.
7. Test fairness under unequal task durations.
8. Document where browser affinity can legitimately reduce ideal fairness.
9. Before editing, write a short local plan listing the exact files expected to change.
10. Prefer the smallest interface that satisfies this milestone and leaves room for later milestones.
11. Keep names consistent with existing repository conventions; do not introduce a parallel naming universe.
12. If an existing helper already implements part of the behavior, reuse it rather than creating a near-duplicate.
13. Keep product-facing behavior backwards compatible unless the milestone explicitly introduces a new opt-in Evo path.
14. When behavior crosses a trust boundary, validate input before mutating durable state.
15. When behavior crosses a thread/process boundary, define ownership, lifetime, timeout, and failure semantics explicitly.
16. When behavior emits a timestamp, state whether it is wall-clock or steady-clock and use it consistently.
17. Add tests in the same milestone as the behavior; do not defer all testing to Milestone 39.
18. Update `docs/phase2/PROGRESS.md` with the observed result, not a future-tense promise.

## Phase-1 preservation checklist

- [ ] AI planner still requires explicit user Run before execution.
- [ ] Generated graph remains editable React Flow / Liveblocks state.
- [ ] Legacy Trigger.dev engine remains available unless a later explicit final decision says otherwise.
- [ ] Clerk auth/org enforcement remains server-side.
- [ ] Agent Pro-plan gate remains fail-closed.
- [ ] Live browser credentials remain server/worker only.
- [ ] Legacy Run/Stop/results/replay/rerun behavior must not regress.
- [ ] Existing node registry/executors are not duplicated without a compelling reason.
- [ ] No test is removed merely because Phase 2 changes an internal representation.

## Concurrency / distributed correctness questions to answer

- [ ] Which component owns the mutable state introduced here?
- [ ] Can two threads/processes modify it concurrently?
- [ ] What lock/transaction/unique constraint protects the invariant?
- [ ] What happens if an event/message is delivered twice?
- [ ] What happens if the process crashes after the first durable write but before the second?
- [ ] What happens when cancellation races completion?
- [ ] What happens when shutdown starts while another thread is waiting?
- [ ] What happens when a worker is slow rather than dead?
- [ ] Does this change browser-session affinity or ownership?
- [ ] Can a late result overwrite a newer logical state?

## Negative / adversarial scenarios

- [ ] Exercise or explicitly mark N/A: invalid/empty identifiers.
- [ ] Exercise or explicitly mark N/A: duplicate request/event.
- [ ] Exercise or explicitly mark N/A: unknown enum or node type.
- [ ] Exercise or explicitly mark N/A: timeout at external boundary.
- [ ] Exercise or explicitly mark N/A: cancellation at an inconvenient moment.
- [ ] Exercise or explicitly mark N/A: process shutdown while work is pending.
- [ ] Exercise or explicitly mark N/A: stale data or stale attempt result.
- [ ] Exercise or explicitly mark N/A: tenant mismatch / unauthorized request when relevant.
- [ ] Exercise or explicitly mark N/A: resource exhaustion / queue full when relevant.
- [ ] Exercise or explicitly mark N/A: malformed serialized payload when relevant.

## Explicit no-go items for this milestone

- Do not implement a complicated scheduler algorithm solely for resume wording.
- Fairness tests must have deterministic seeds/workloads.
- Do not invent performance numbers.
- Do not mark a future component as implemented.
- Do not silently change Phase-1 default behavior.
- Do not commit secrets or local credential values.

## Required validation

- [ ] `starvation test`
- [ ] `equal-weight fairness benchmark`
- [ ] `unequal-duration benchmark`
- [ ] `raw tenant wait samples`
- [ ] Run current Phase-1 `npm test` when production app code is touched.
- [ ] Run `npm run typecheck` when TypeScript is touched.
- [ ] Run `npm run lint` when TypeScript/React is touched.
- [ ] Run CMake/CTest when C++ is touched.
- [ ] Run production build at integration/high-risk milestones.

## Performance evidence rules for this milestone

- If this milestone is not explicitly a benchmark milestone, do not add a resume number just because a timer was printed.
- If timing is collected for a test, label it diagnostic unless benchmark methodology requirements are satisfied.
- If this milestone changes a previously benchmarked hot path, note that old results are no longer final for the new SHA.
- Store benchmark raw data only in the defined benchmark result structure, not ad-hoc README prose.
- A result must identify workload, build mode, hardware, sample count, and commit before it becomes evidence-grade.

## Human intervention triggers

- If A required secret/account is missing. -> report `STATUS: BLOCKED` with exact manual action; do not guess.
- If A system-level prerequisite such as Docker/CMake/compiler requires user installation. -> report `STATUS: BLOCKED` with exact manual action; do not guess.
- If A shared/remote database migration would be applied. -> report `STATUS: BLOCKED` with exact manual action; do not guess.
- If Git author configuration is missing. -> report `STATUS: BLOCKED` with exact manual action; do not guess.
- If A destructive operation would touch user data or history. -> report `STATUS: BLOCKED` with exact manual action; do not guess.
- If A paid external test would incur nontrivial cost and the user has not already authorized it. -> report `STATUS: BLOCKED` with exact manual action; do not guess.

## Diff-review checklist before commit

- [ ] Review every changed file with `git diff`.
- [ ] Run `git diff --check`.
- [ ] Remove accidental formatting churn outside the milestone scope.
- [ ] Confirm no secret or `.env.local` value appears in the diff.
- [ ] Confirm no generated build tree, node_modules, vcpkg cache, Docker volume, or large transient trace has been staged.
- [ ] Confirm comments/README wording describe implemented behavior, not planned behavior.

## Commit requirement

Create one local commit with subject:

`phase2(m37): add fair multi-tenant scheduling`

After commit:

- Record the new SHA in `docs/phase2/PROGRESS.md`.
- Record commands/results for the milestone.
- Do not push unless push permission/policy has already been explicitly authorized.
- If all exit criteria are green, proceed automatically to the next milestone.

## Exit criteria

- [ ] All milestone-specific implementation items are complete.
- [ ] All relevant new tests pass.
- [ ] Required Phase-1 regression gates pass.
- [ ] No unresolved race/data-corruption/security issue is knowingly introduced.
- [ ] No secret or accidental generated artifact is staged.
- [ ] Documentation describes current behavior accurately.
- [ ] The milestone has exactly one intended local commit or a clearly documented reason for a split required by repository tooling.

## Mandatory milestone report format

```text
STATUS: DONE or BLOCKED
MILESTONE: exact milestone number and title
BASE_SHA: SHA before milestone
END_SHA: SHA after commit when DONE
WHAT_I_INSPECTED: files/docs/commands read before changes
WHAT_CHANGED: concise implementation summary
FILES_CHANGED: exact paths
TESTS: command -> result
PHASE1_REGRESSION: command -> result or N/A with reason
CPP_VALIDATION: command -> result or N/A
DISTRIBUTED_VALIDATION: command -> result or N/A
METRICS_CAPTURED: actual measured metrics only; otherwise `None`
HUMAN_ACTION: None or exact required action
KNOWN_LIMITATIONS: explicit remaining limitations
COMMIT: commit SHA + subject
NEXT: next milestone number; continue automatically if not blocked
```

---

# MILESTONE 38 — Add observability, service security, and CI quality gates

## Objective

Make the distributed engine diagnosable and continuously verifiable without live secrets.

## Why this milestone exists

- This milestone isolates **add observability, service security, and ci quality gates** so failures are attributable and reviewable.
- The milestone is intentionally narrower than the final Phase-2 architecture.
- Do not implement later milestone responsibilities early unless a tiny interface stub is required for compilation.
- Any stub must be clearly marked as non-production and must not be described as a completed feature.

## Mandatory pre-read and repository inspection

- Read `AGENTS.md` and obey repository-local instructions relevant to files you will touch.
- Run `git status --short` and confirm no unrelated user work will be overwritten.
- Read every existing source file that the milestone will modify before editing it.
- Read the nearest tests covering the behavior before changing implementation.
- Inspect current package/library versions rather than assuming APIs from memory.
- Consult current official documentation before using a third-party API that may have changed.

## Entry conditions

- Milestone 37 is DONE, unless this is Milestone 01.
- Working tree contains no unexpected unrelated modifications.
- The current Phase-1 baseline gate is known and available.
- Any dependency introduced in this milestone has a verified version/source and license acceptable for the repository.

## Detailed implementation procedure

1. Propagate run ID, node ID, attempt ID, org ID, worker ID, and trace/correlation ID through scheduler/Redis/worker logs.
2. Emit structured JSON logs from C++ and TypeScript services with secret redaction.
3. Expose scheduler metrics in a Prometheus-compatible form directly or through a small trusted adapter.
4. If OpenTelemetry is added, prove an actual exporter/trace path before claiming it; otherwise document correlation IDs without using the OpenTelemetry name.
5. Add service-to-service authentication for gRPC and sensitive Evo endpoints using a server-only engine token or stronger verified mechanism.
6. Apply input size limits and validate enums/IDs at trust boundaries.
7. Create GitHub Actions/CI jobs for Phase-1 Node checks, C++ GCC/Clang builds, C++ tests, sanitizer jobs where supported, and Redis/Postgres distributed synthetic integration.
8. Do not require Browserbase/Resend/paid external keys in ordinary PR CI.
9. Add a benchmark smoke test that verifies the harness works but does not assert timing on shared CI runners.
10. Create `docs/phase2/SECURITY.md` and threat-model notes.
11. Before editing, write a short local plan listing the exact files expected to change.
12. Prefer the smallest interface that satisfies this milestone and leaves room for later milestones.
13. Keep names consistent with existing repository conventions; do not introduce a parallel naming universe.
14. If an existing helper already implements part of the behavior, reuse it rather than creating a near-duplicate.
15. Keep product-facing behavior backwards compatible unless the milestone explicitly introduces a new opt-in Evo path.
16. When behavior crosses a trust boundary, validate input before mutating durable state.
17. When behavior crosses a thread/process boundary, define ownership, lifetime, timeout, and failure semantics explicitly.
18. When behavior emits a timestamp, state whether it is wall-clock or steady-clock and use it consistently.
19. Add tests in the same milestone as the behavior; do not defer all testing to Milestone 39.
20. Update `docs/phase2/PROGRESS.md` with the observed result, not a future-tense promise.

## Phase-1 preservation checklist

- [ ] AI planner still requires explicit user Run before execution.
- [ ] Generated graph remains editable React Flow / Liveblocks state.
- [ ] Legacy Trigger.dev engine remains available unless a later explicit final decision says otherwise.
- [ ] Clerk auth/org enforcement remains server-side.
- [ ] Agent Pro-plan gate remains fail-closed.
- [ ] Live browser credentials remain server/worker only.
- [ ] Legacy Run/Stop/results/replay/rerun behavior must not regress.
- [ ] Existing node registry/executors are not duplicated without a compelling reason.
- [ ] No test is removed merely because Phase 2 changes an internal representation.

## Concurrency / distributed correctness questions to answer

- [ ] Which component owns the mutable state introduced here?
- [ ] Can two threads/processes modify it concurrently?
- [ ] What lock/transaction/unique constraint protects the invariant?
- [ ] What happens if an event/message is delivered twice?
- [ ] What happens if the process crashes after the first durable write but before the second?
- [ ] What happens when cancellation races completion?
- [ ] What happens when shutdown starts while another thread is waiting?
- [ ] What happens when a worker is slow rather than dead?
- [ ] Does this change browser-session affinity or ownership?
- [ ] Can a late result overwrite a newer logical state?

## Negative / adversarial scenarios

- [ ] Exercise or explicitly mark N/A: invalid/empty identifiers.
- [ ] Exercise or explicitly mark N/A: duplicate request/event.
- [ ] Exercise or explicitly mark N/A: unknown enum or node type.
- [ ] Exercise or explicitly mark N/A: timeout at external boundary.
- [ ] Exercise or explicitly mark N/A: cancellation at an inconvenient moment.
- [ ] Exercise or explicitly mark N/A: process shutdown while work is pending.
- [ ] Exercise or explicitly mark N/A: stale data or stale attempt result.
- [ ] Exercise or explicitly mark N/A: tenant mismatch / unauthorized request when relevant.
- [ ] Exercise or explicitly mark N/A: resource exhaustion / queue full when relevant.
- [ ] Exercise or explicitly mark N/A: malformed serialized payload when relevant.

## Explicit no-go items for this milestone

- Do not expose engine token to the browser.
- Do not publish private Redis/Postgres ports in production defaults.
- Do not invent performance numbers.
- Do not mark a future component as implemented.
- Do not silently change Phase-1 default behavior.
- Do not commit secrets or local credential values.

## Required validation

- [ ] `CI workflow validation`
- [ ] `secret scan`
- [ ] `auth-negative tests`
- [ ] `sanitizer CI`
- [ ] `distributed integration CI`
- [ ] Run current Phase-1 `npm test` when production app code is touched.
- [ ] Run `npm run typecheck` when TypeScript is touched.
- [ ] Run `npm run lint` when TypeScript/React is touched.
- [ ] Run CMake/CTest when C++ is touched.
- [ ] Run production build at integration/high-risk milestones.

## Performance evidence rules for this milestone

- If this milestone is not explicitly a benchmark milestone, do not add a resume number just because a timer was printed.
- If timing is collected for a test, label it diagnostic unless benchmark methodology requirements are satisfied.
- If this milestone changes a previously benchmarked hot path, note that old results are no longer final for the new SHA.
- Store benchmark raw data only in the defined benchmark result structure, not ad-hoc README prose.
- A result must identify workload, build mode, hardware, sample count, and commit before it becomes evidence-grade.

## Human intervention triggers

- If A required secret/account is missing. -> report `STATUS: BLOCKED` with exact manual action; do not guess.
- If A system-level prerequisite such as Docker/CMake/compiler requires user installation. -> report `STATUS: BLOCKED` with exact manual action; do not guess.
- If A shared/remote database migration would be applied. -> report `STATUS: BLOCKED` with exact manual action; do not guess.
- If Git author configuration is missing. -> report `STATUS: BLOCKED` with exact manual action; do not guess.
- If A destructive operation would touch user data or history. -> report `STATUS: BLOCKED` with exact manual action; do not guess.
- If A paid external test would incur nontrivial cost and the user has not already authorized it. -> report `STATUS: BLOCKED` with exact manual action; do not guess.

## Diff-review checklist before commit

- [ ] Review every changed file with `git diff`.
- [ ] Run `git diff --check`.
- [ ] Remove accidental formatting churn outside the milestone scope.
- [ ] Confirm no secret or `.env.local` value appears in the diff.
- [ ] Confirm no generated build tree, node_modules, vcpkg cache, Docker volume, or large transient trace has been staged.
- [ ] Confirm comments/README wording describe implemented behavior, not planned behavior.

## Commit requirement

Create one local commit with subject:

`phase2(m38): add observability security and ci gates`

After commit:

- Record the new SHA in `docs/phase2/PROGRESS.md`.
- Record commands/results for the milestone.
- Do not push unless push permission/policy has already been explicitly authorized.
- If all exit criteria are green, proceed automatically to the next milestone.

## Exit criteria

- [ ] All milestone-specific implementation items are complete.
- [ ] All relevant new tests pass.
- [ ] Required Phase-1 regression gates pass.
- [ ] No unresolved race/data-corruption/security issue is knowingly introduced.
- [ ] No secret or accidental generated artifact is staged.
- [ ] Documentation describes current behavior accurately.
- [ ] The milestone has exactly one intended local commit or a clearly documented reason for a split required by repository tooling.

## Mandatory milestone report format

```text
STATUS: DONE or BLOCKED
MILESTONE: exact milestone number and title
BASE_SHA: SHA before milestone
END_SHA: SHA after commit when DONE
WHAT_I_INSPECTED: files/docs/commands read before changes
WHAT_CHANGED: concise implementation summary
FILES_CHANGED: exact paths
TESTS: command -> result
PHASE1_REGRESSION: command -> result or N/A with reason
CPP_VALIDATION: command -> result or N/A
DISTRIBUTED_VALIDATION: command -> result or N/A
METRICS_CAPTURED: actual measured metrics only; otherwise `None`
HUMAN_ACTION: None or exact required action
KNOWN_LIMITATIONS: explicit remaining limitations
COMMIT: commit SHA + subject
NEXT: next milestone number; continue automatically if not blocked
```

---

# MILESTONE 39 — Run the final reproducible performance, scaling, and chaos campaign

## Objective

Produce the evidence from which resume metrics may finally be written.

## Why this milestone exists

- This milestone isolates **run the final reproducible performance, scaling, and chaos campaign** so failures are attributable and reviewable.
- The milestone is intentionally narrower than the final Phase-2 architecture.
- Do not implement later milestone responsibilities early unless a tiny interface stub is required for compilation.
- Any stub must be clearly marked as non-production and must not be described as a completed feature.

## Mandatory pre-read and repository inspection

- Read `AGENTS.md` and obey repository-local instructions relevant to files you will touch.
- Run `git status --short` and confirm no unrelated user work will be overwritten.
- Read every existing source file that the milestone will modify before editing it.
- Read the nearest tests covering the behavior before changing implementation.
- Inspect current package/library versions rather than assuming APIs from memory.
- Consult current official documentation before using a third-party API that may have changed.

## Entry conditions

- Milestone 38 is DONE, unless this is Milestone 01.
- Working tree contains no unexpected unrelated modifications.
- The current Phase-1 baseline gate is known and available.
- Any dependency introduced in this milestone has a verified version/source and license acceptable for the repository.

## Detailed implementation procedure

1. Freeze a benchmark commit SHA before the campaign.
2. Run local scheduler benchmarks on Release builds for sequential reference vs concurrent scheduler.
3. Run thread counts 1/2/4/8 and sensible higher counts subject to machine limits.
4. Run distributed worker counts 1/2/4/8 where local resources permit.
5. Use DAG sizes such as 10/50/100/500/1000 where runtime remains practical.
6. Use linear, diamond, wide, layered, and seeded random DAG shapes.
7. Run synthetic I/O-like and CPU-like task profiles separately.
8. Measure p50/p95/p99 ready-to-dispatch, queue latency, makespan, throughput, queue depth, utilization, CPU/memory when collectors are reliable, cancellation latency, recovery time, retries, and duplicates.
9. Run worker-kill, scheduler-kill, Redis temporary outage, Postgres temporary outage, duplicate delivery, lost acknowledgement, slow worker, cancellation, and overload scenarios.
10. For Browserbase end-to-end tests, use a small cost-bounded scenario set and clearly label external network/model latency.
11. Run multiple trials and retain raw samples.
12. Generate machine-readable summaries and human-readable reports.
13. Create `benchmarks/results/<timestamp>-<sha>/manifest.json`, raw data, summary, and checksums.
14. Do not overwrite previous result directories.
15. If a result is worse than expected, preserve it and explain it.
16. Before editing, write a short local plan listing the exact files expected to change.
17. Prefer the smallest interface that satisfies this milestone and leaves room for later milestones.
18. Keep names consistent with existing repository conventions; do not introduce a parallel naming universe.
19. If an existing helper already implements part of the behavior, reuse it rather than creating a near-duplicate.
20. Keep product-facing behavior backwards compatible unless the milestone explicitly introduces a new opt-in Evo path.
21. When behavior crosses a trust boundary, validate input before mutating durable state.
22. When behavior crosses a thread/process boundary, define ownership, lifetime, timeout, and failure semantics explicitly.
23. When behavior emits a timestamp, state whether it is wall-clock or steady-clock and use it consistently.
24. Add tests in the same milestone as the behavior; do not defer all testing to Milestone 39.
25. Update `docs/phase2/PROGRESS.md` with the observed result, not a future-tense promise.

## Phase-1 preservation checklist

- [ ] AI planner still requires explicit user Run before execution.
- [ ] Generated graph remains editable React Flow / Liveblocks state.
- [ ] Legacy Trigger.dev engine remains available unless a later explicit final decision says otherwise.
- [ ] Clerk auth/org enforcement remains server-side.
- [ ] Agent Pro-plan gate remains fail-closed.
- [ ] Live browser credentials remain server/worker only.
- [ ] Legacy Run/Stop/results/replay/rerun behavior must not regress.
- [ ] Existing node registry/executors are not duplicated without a compelling reason.
- [ ] No test is removed merely because Phase 2 changes an internal representation.

## Concurrency / distributed correctness questions to answer

- [ ] Which component owns the mutable state introduced here?
- [ ] Can two threads/processes modify it concurrently?
- [ ] What lock/transaction/unique constraint protects the invariant?
- [ ] What happens if an event/message is delivered twice?
- [ ] What happens if the process crashes after the first durable write but before the second?
- [ ] What happens when cancellation races completion?
- [ ] What happens when shutdown starts while another thread is waiting?
- [ ] What happens when a worker is slow rather than dead?
- [ ] Does this change browser-session affinity or ownership?
- [ ] Can a late result overwrite a newer logical state?

## Negative / adversarial scenarios

- [ ] Exercise or explicitly mark N/A: invalid/empty identifiers.
- [ ] Exercise or explicitly mark N/A: duplicate request/event.
- [ ] Exercise or explicitly mark N/A: unknown enum or node type.
- [ ] Exercise or explicitly mark N/A: timeout at external boundary.
- [ ] Exercise or explicitly mark N/A: cancellation at an inconvenient moment.
- [ ] Exercise or explicitly mark N/A: process shutdown while work is pending.
- [ ] Exercise or explicitly mark N/A: stale data or stale attempt result.
- [ ] Exercise or explicitly mark N/A: tenant mismatch / unauthorized request when relevant.
- [ ] Exercise or explicitly mark N/A: resource exhaustion / queue full when relevant.
- [ ] Exercise or explicitly mark N/A: malformed serialized payload when relevant.

## Explicit no-go items for this milestone

- Do not cherry-pick only the fastest run.
- Do not use a synthetic scheduler speedup as an end-to-end browser speedup.
- Do not use CI runner timing as final resume evidence.
- Do not invent performance numbers.
- Do not mark a future component as implemented.
- Do not silently change Phase-1 default behavior.
- Do not commit secrets or local credential values.

## Required validation

- [ ] `re-run selected benchmark to prove reproducibility`
- [ ] `validate raw/summary calculations`
- [ ] `fault outcomes audit`
- [ ] `manual external E2E when keys configured`
- [ ] Run current Phase-1 `npm test` when production app code is touched.
- [ ] Run `npm run typecheck` when TypeScript is touched.
- [ ] Run `npm run lint` when TypeScript/React is touched.
- [ ] Run CMake/CTest when C++ is touched.
- [ ] Run production build at integration/high-risk milestones.

## Performance evidence rules for this milestone

- If this milestone is not explicitly a benchmark milestone, do not add a resume number just because a timer was printed.
- If timing is collected for a test, label it diagnostic unless benchmark methodology requirements are satisfied.
- If this milestone changes a previously benchmarked hot path, note that old results are no longer final for the new SHA.
- Store benchmark raw data only in the defined benchmark result structure, not ad-hoc README prose.
- A result must identify workload, build mode, hardware, sample count, and commit before it becomes evidence-grade.

## Human intervention triggers

- If A required secret/account is missing. -> report `STATUS: BLOCKED` with exact manual action; do not guess.
- If A system-level prerequisite such as Docker/CMake/compiler requires user installation. -> report `STATUS: BLOCKED` with exact manual action; do not guess.
- If A shared/remote database migration would be applied. -> report `STATUS: BLOCKED` with exact manual action; do not guess.
- If Git author configuration is missing. -> report `STATUS: BLOCKED` with exact manual action; do not guess.
- If A destructive operation would touch user data or history. -> report `STATUS: BLOCKED` with exact manual action; do not guess.
- If A paid external test would incur nontrivial cost and the user has not already authorized it. -> report `STATUS: BLOCKED` with exact manual action; do not guess.

## Diff-review checklist before commit

- [ ] Review every changed file with `git diff`.
- [ ] Run `git diff --check`.
- [ ] Remove accidental formatting churn outside the milestone scope.
- [ ] Confirm no secret or `.env.local` value appears in the diff.
- [ ] Confirm no generated build tree, node_modules, vcpkg cache, Docker volume, or large transient trace has been staged.
- [ ] Confirm comments/README wording describe implemented behavior, not planned behavior.

## Commit requirement

Create one local commit with subject:

`phase2(m39): capture final benchmark and chaos evidence`

After commit:

- Record the new SHA in `docs/phase2/PROGRESS.md`.
- Record commands/results for the milestone.
- Do not push unless push permission/policy has already been explicitly authorized.
- If all exit criteria are green, proceed automatically to the next milestone.

## Exit criteria

- [ ] All milestone-specific implementation items are complete.
- [ ] All relevant new tests pass.
- [ ] Required Phase-1 regression gates pass.
- [ ] No unresolved race/data-corruption/security issue is knowingly introduced.
- [ ] No secret or accidental generated artifact is staged.
- [ ] Documentation describes current behavior accurately.
- [ ] The milestone has exactly one intended local commit or a clearly documented reason for a split required by repository tooling.

## Mandatory milestone report format

```text
STATUS: DONE or BLOCKED
MILESTONE: exact milestone number and title
BASE_SHA: SHA before milestone
END_SHA: SHA after commit when DONE
WHAT_I_INSPECTED: files/docs/commands read before changes
WHAT_CHANGED: concise implementation summary
FILES_CHANGED: exact paths
TESTS: command -> result
PHASE1_REGRESSION: command -> result or N/A with reason
CPP_VALIDATION: command -> result or N/A
DISTRIBUTED_VALIDATION: command -> result or N/A
METRICS_CAPTURED: actual measured metrics only; otherwise `None`
HUMAN_ACTION: None or exact required action
KNOWN_LIMITATIONS: explicit remaining limitations
COMMIT: commit SHA + subject
NEXT: next milestone number; continue automatically if not blocked
```

---

# MILESTONE 40 — Final Phase-2 audit, documentation, release, and resume evidence registry

## Objective

Finish with truthful, reproducible project claims and a clean compatibility story.

## Why this milestone exists

- This milestone isolates **final phase-2 audit, documentation, release, and resume evidence registry** so failures are attributable and reviewable.
- The milestone is intentionally narrower than the final Phase-2 architecture.
- Do not implement later milestone responsibilities early unless a tiny interface stub is required for compilation.
- Any stub must be clearly marked as non-production and must not be described as a completed feature.

## Mandatory pre-read and repository inspection

- Read `AGENTS.md` and obey repository-local instructions relevant to files you will touch.
- Run `git status --short` and confirm no unrelated user work will be overwritten.
- Read every existing source file that the milestone will modify before editing it.
- Read the nearest tests covering the behavior before changing implementation.
- Inspect current package/library versions rather than assuming APIs from memory.
- Consult current official documentation before using a third-party API that may have changed.

## Entry conditions

- Milestone 39 is DONE, unless this is Milestone 01.
- Working tree contains no unexpected unrelated modifications.
- The current Phase-1 baseline gate is known and available.
- Any dependency introduced in this milestone has a verified version/source and license acceptable for the repository.

## Detailed implementation procedure

1. Run every Phase-1 regression test and every Phase-2 unit/integration test.
2. Run typecheck, lint, production build, C++ Release build, CTest, sanitizers where supported, and local distributed integration.
3. Re-run a representative benchmark subset on the final SHA or document why the benchmark SHA differs only by docs.
4. Update README architecture to show legacy and Evo engines accurately.
5. Update getting-started instructions with C++/Docker/Redis requirements and human intervention points.
6. Document exactly which execution engine is default and how to switch safely.
7. Create `docs/phase2/PHASE-2-IMPLEMENTATION-REPORT.md`.
8. Create `docs/phase2/RESUME_EVIDENCE.md` with one row per candidate claim: claim, evidence command, raw artifact, commit SHA, status GREEN/YELLOW/RED, exact caveat.
9. Generate candidate resume bullets only from GREEN claims.
10. Do not round metrics deceptively; preserve denominators and workload context in evidence docs.
11. Create system-design interview notes covering scheduler, thread pool, leases, idempotency, browser affinity, fairness, retries, crash recovery, and measured tradeoffs.
12. Ensure README distinguishes what is implemented from future work.
13. Ensure no secret, local path, personal token, or benchmark placeholder is committed.
14. Create final milestone commit and a release checklist; push only if user has authorized it.
15. Before editing, write a short local plan listing the exact files expected to change.
16. Prefer the smallest interface that satisfies this milestone and leaves room for later milestones.
17. Keep names consistent with existing repository conventions; do not introduce a parallel naming universe.
18. If an existing helper already implements part of the behavior, reuse it rather than creating a near-duplicate.
19. Keep product-facing behavior backwards compatible unless the milestone explicitly introduces a new opt-in Evo path.
20. When behavior crosses a trust boundary, validate input before mutating durable state.
21. When behavior crosses a thread/process boundary, define ownership, lifetime, timeout, and failure semantics explicitly.
22. When behavior emits a timestamp, state whether it is wall-clock or steady-clock and use it consistently.
23. Add tests in the same milestone as the behavior; do not defer all testing to Milestone 39.
24. Update `docs/phase2/PROGRESS.md` with the observed result, not a future-tense promise.

## Phase-1 preservation checklist

- [ ] AI planner still requires explicit user Run before execution.
- [ ] Generated graph remains editable React Flow / Liveblocks state.
- [ ] Legacy Trigger.dev engine remains available unless a later explicit final decision says otherwise.
- [ ] Clerk auth/org enforcement remains server-side.
- [ ] Agent Pro-plan gate remains fail-closed.
- [ ] Live browser credentials remain server/worker only.
- [ ] Legacy Run/Stop/results/replay/rerun behavior must not regress.
- [ ] Existing node registry/executors are not duplicated without a compelling reason.
- [ ] No test is removed merely because Phase 2 changes an internal representation.

## Concurrency / distributed correctness questions to answer

- [ ] Which component owns the mutable state introduced here?
- [ ] Can two threads/processes modify it concurrently?
- [ ] What lock/transaction/unique constraint protects the invariant?
- [ ] What happens if an event/message is delivered twice?
- [ ] What happens if the process crashes after the first durable write but before the second?
- [ ] What happens when cancellation races completion?
- [ ] What happens when shutdown starts while another thread is waiting?
- [ ] What happens when a worker is slow rather than dead?
- [ ] Does this change browser-session affinity or ownership?
- [ ] Can a late result overwrite a newer logical state?

## Negative / adversarial scenarios

- [ ] Exercise or explicitly mark N/A: invalid/empty identifiers.
- [ ] Exercise or explicitly mark N/A: duplicate request/event.
- [ ] Exercise or explicitly mark N/A: unknown enum or node type.
- [ ] Exercise or explicitly mark N/A: timeout at external boundary.
- [ ] Exercise or explicitly mark N/A: cancellation at an inconvenient moment.
- [ ] Exercise or explicitly mark N/A: process shutdown while work is pending.
- [ ] Exercise or explicitly mark N/A: stale data or stale attempt result.
- [ ] Exercise or explicitly mark N/A: tenant mismatch / unauthorized request when relevant.
- [ ] Exercise or explicitly mark N/A: resource exhaustion / queue full when relevant.
- [ ] Exercise or explicitly mark N/A: malformed serialized payload when relevant.

## Explicit no-go items for this milestone

- No fabricated metric survives this milestone.
- No unsupported `exactly once`, `zero downtime`, or `linear scaling` claim.
- Phase-1 behavior must still pass.
- Do not invent performance numbers.
- Do not mark a future component as implemented.
- Do not silently change Phase-1 default behavior.
- Do not commit secrets or local credential values.

## Required validation

- [ ] `full Node suite`
- [ ] `full C++ suite`
- [ ] `distributed integration`
- [ ] `build`
- [ ] `security check`
- [ ] `evidence-link audit`
- [ ] Run current Phase-1 `npm test` when production app code is touched.
- [ ] Run `npm run typecheck` when TypeScript is touched.
- [ ] Run `npm run lint` when TypeScript/React is touched.
- [ ] Run CMake/CTest when C++ is touched.
- [ ] Run production build at integration/high-risk milestones.

## Performance evidence rules for this milestone

- If this milestone is not explicitly a benchmark milestone, do not add a resume number just because a timer was printed.
- If timing is collected for a test, label it diagnostic unless benchmark methodology requirements are satisfied.
- If this milestone changes a previously benchmarked hot path, note that old results are no longer final for the new SHA.
- Store benchmark raw data only in the defined benchmark result structure, not ad-hoc README prose.
- A result must identify workload, build mode, hardware, sample count, and commit before it becomes evidence-grade.

## Human intervention triggers

- If A required secret/account is missing. -> report `STATUS: BLOCKED` with exact manual action; do not guess.
- If A system-level prerequisite such as Docker/CMake/compiler requires user installation. -> report `STATUS: BLOCKED` with exact manual action; do not guess.
- If A shared/remote database migration would be applied. -> report `STATUS: BLOCKED` with exact manual action; do not guess.
- If Git author configuration is missing. -> report `STATUS: BLOCKED` with exact manual action; do not guess.
- If A destructive operation would touch user data or history. -> report `STATUS: BLOCKED` with exact manual action; do not guess.
- If A paid external test would incur nontrivial cost and the user has not already authorized it. -> report `STATUS: BLOCKED` with exact manual action; do not guess.

## Diff-review checklist before commit

- [ ] Review every changed file with `git diff`.
- [ ] Run `git diff --check`.
- [ ] Remove accidental formatting churn outside the milestone scope.
- [ ] Confirm no secret or `.env.local` value appears in the diff.
- [ ] Confirm no generated build tree, node_modules, vcpkg cache, Docker volume, or large transient trace has been staged.
- [ ] Confirm comments/README wording describe implemented behavior, not planned behavior.

## Commit requirement

Create one local commit with subject:

`phase2(m40): finalize evidence-backed distributed engine`

After commit:

- Record the new SHA in `docs/phase2/PROGRESS.md`.
- Record commands/results for the milestone.
- Do not push unless push permission/policy has already been explicitly authorized.
- If all exit criteria are green, proceed automatically to the next milestone.

## Exit criteria

- [ ] All milestone-specific implementation items are complete.
- [ ] All relevant new tests pass.
- [ ] Required Phase-1 regression gates pass.
- [ ] No unresolved race/data-corruption/security issue is knowingly introduced.
- [ ] No secret or accidental generated artifact is staged.
- [ ] Documentation describes current behavior accurately.
- [ ] The milestone has exactly one intended local commit or a clearly documented reason for a split required by repository tooling.

## Mandatory milestone report format

```text
STATUS: DONE or BLOCKED
MILESTONE: exact milestone number and title
BASE_SHA: SHA before milestone
END_SHA: SHA after commit when DONE
WHAT_I_INSPECTED: files/docs/commands read before changes
WHAT_CHANGED: concise implementation summary
FILES_CHANGED: exact paths
TESTS: command -> result
PHASE1_REGRESSION: command -> result or N/A with reason
CPP_VALIDATION: command -> result or N/A
DISTRIBUTED_VALIDATION: command -> result or N/A
METRICS_CAPTURED: actual measured metrics only; otherwise `None`
HUMAN_ACTION: None or exact required action
KNOWN_LIMITATIONS: explicit remaining limitations
COMMIT: commit SHA + subject
NEXT: next milestone number; continue automatically if not blocked
```

---

# APPENDIX A — Phase-1 Preservation Matrix

## Planner

- [ ] Prompt screen still works
- [ ] Provider secret stays server-side
- [ ] Registry-derived node catalog remains authoritative
- [ ] Unsupported goals fail safely
- [ ] Generation never auto-runs

## Canvas

- [ ] Generated/manual graphs remain editable
- [ ] Liveblocks collaboration still works
- [ ] Node inspector values remain writable
- [ ] Connections/interpolation tokens remain valid
- [ ] Preview banner lifecycle remains correct

## Execution

- [ ] Legacy Trigger task remains callable
- [ ] Server-side graph validation remains enforced
- [ ] Agent plan gate remains enforced
- [ ] Stop remains available
- [ ] Rerun creates a new run

## Browser

- [ ] One default browser session per run
- [ ] Live-view gate behavior preserved
- [ ] On-page highlights preserved
- [ ] Final screenshot preserved
- [ ] Replay points at selected run session

## Results

- [ ] Readable output rendering preserved
- [ ] Terminal-only result popup
- [ ] Final URL/timing preserved
- [ ] Failed/canceled status accurate
- [ ] Post-run editing preserved

---

# APPENDIX B — C++ Concurrency Review Checklist

## Thread lifecycle

- [ ] Every owned thread has clear owner
- [ ] No detached threads
- [ ] Stop path wakes blocking waits
- [ ] Destructor cannot deadlock
- [ ] Exceptions are captured

## Locks

- [ ] Lock order documented
- [ ] No callback/external I/O while holding broad scheduler mutex unless justified
- [ ] Condition predicates protected by same mutex
- [ ] No recursive lock dependency
- [ ] Critical sections minimized after correctness

## Atomics

- [ ] Memory ordering defaults documented
- [ ] Atomics used only for independent invariants
- [ ] Composite state not split across unrelated atomics
- [ ] Tests cover races
- [ ] TSan used where supported

## Queues

- [ ] No lost wakeup
- [ ] Bound defined
- [ ] Close semantics defined
- [ ] Producer after close rejected
- [ ] Consumer wake on close

---

# APPENDIX C — DAG Correctness Matrix

## Linear

- [ ] A→B→C ordering
- [ ] no parallel dependency violation
- [ ] failure blocks downstream

## Diamond

- [ ] A unlocks B/C
- [ ] B/C may overlap
- [ ] D waits for both
- [ ] duplicate B completion does not unlock D early

## Wide

- [ ] many independent children ready together
- [ ] capacity bound respected
- [ ] all eventually complete

## Disconnected

- [ ] policy explicit
- [ ] no accidental orphan execution
- [ ] validation message clear

## Cycle

- [ ] rejected before run
- [ ] no partial task dispatch

## Random

- [ ] seed recorded
- [ ] reference scheduler equivalence
- [ ] stress repetition

---

# APPENDIX D — Redis Streams Semantics Checklist

## Task stream

- [ ] consumer group created idempotently
- [ ] message schema versioned
- [ ] payload size bounded
- [ ] enqueue failure retryable
- [ ] stream key namespaced

## Pending

- [ ] claimed attempt recorded
- [ ] lease separate from Redis pending time
- [ ] stale pending reconciliation
- [ ] no blind XAUTOCLAIM assumptions without verified semantics

## Ack

- [ ] ack timing defined
- [ ] result durability before ack rule defined
- [ ] lost ack duplicate handled
- [ ] late ack harmless

## Results

- [ ] result stream durable
- [ ] duplicate results deduped
- [ ] scheduler consumer group recoverable
- [ ] malformed result quarantined/logged

---

# APPENDIX E — Postgres Phase-2 Data Model Checklist

## workflow_versions

- [ ] immutable graph snapshot
- [ ] workflow_id/org linkage
- [ ] version/hash
- [ ] created_at
- [ ] no update path

## workflow_runs

- [ ] engine-neutral run id
- [ ] workflow version reference
- [ ] org/workflow refs
- [ ] engine discriminator
- [ ] status/timestamps
- [ ] browser session/final URL optional
- [ ] error/metrics structured

## node_runs

- [ ] run+node unique
- [ ] logical status
- [ ] output jsonb
- [ ] ready/start/finish timestamps
- [ ] retry summary

## task_attempts

- [ ] attempt id
- [ ] run/node
- [ ] attempt number
- [ ] worker
- [ ] lease
- [ ] status
- [ ] timings
- [ ] error class

## idempotency

- [ ] logical key unique
- [ ] state
- [ ] result reference
- [ ] created/completed timestamps
- [ ] side-effect caveat documented

---

# APPENDIX F — Browser-Affinity Failure Semantics

## Normal run

- [ ] worker owns session
- [ ] same affinity serialized
- [ ] session id emitted early
- [ ] live gate before first action
- [ ] close in cleanup

## Graceful cancel

- [ ] scheduler stops dispatch
- [ ] worker receives cancel
- [ ] Stagehand closes
- [ ] terminal cancel persists

## Worker crash

- [ ] lease expires
- [ ] browser resource considered lost
- [ ] policy explicit: fail or restart chain
- [ ] never claim transparent continuation without proof

## Rerun

- [ ] new run id
- [ ] new affinity key
- [ ] new Browserbase session
- [ ] old replay remains historical

---

# APPENDIX G — Retry and Error Taxonomy

## Transient

- [ ] network timeout
- [ ] 429/temporary provider failure when policy allows
- [ ] Redis transient
- [ ] worker resource transient

## Permanent

- [ ] invalid node input
- [ ] unsupported operation
- [ ] schema validation
- [ ] non-retryable provider response

## Authorization

- [ ] never blind retry
- [ ] fail closed
- [ ] do not leak details

## Canceled

- [ ] no retry
- [ ] terminal run policy

## Resource lost

- [ ] retry only if operation/resource semantics permit
- [ ] browser caveat

---

# APPENDIX H — Idempotency Threat Matrix

## Duplicate queue delivery before execution

- [ ] one logical operation
- [ ] extra attempt allowed but side effect guarded

## Worker executes then result duplicated

- [ ] logical commit once
- [ ] dependency unlock once

## Worker finishes external effect then crashes before local commit

- [ ] explicit ambiguity unless external provider idempotency resolves it

## Expired attempt finishes late

- [ ] must not overwrite replacement success/failure incorrectly

## Scheduler restarts

- [ ] idempotency state recovered
- [ ] no duplicate dependency decrement

---

# APPENDIX I — Cancellation Race Matrix

## cancel before dispatch

- [ ] no worker task
- [ ] logical canceled

## cancel after enqueue before worker start

- [ ] worker observes cancel and avoids side effect where possible

## cancel during synthetic task

- [ ] stop token test
- [ ] latency measured

## cancel during browser call

- [ ] cooperative/forced cleanup behavior measured
- [ ] no false instant-cancel claim

## completion races cancel

- [ ] one terminal state wins according to transaction/state rule
- [ ] late event harmless

---

# APPENDIX J — Multi-Tenant Fairness Benchmark Matrix

## Equal tenants

- [ ] same task duration
- [ ] large queues
- [ ] service rates
- [ ] Jain index

## Noisy neighbor

- [ ] Tenant A huge backlog
- [ ] Tenant B small backlog
- [ ] B wait bounded/no starvation

## Unequal durations

- [ ] short vs long tasks
- [ ] fairness algorithm observed

## Browser constrained

- [ ] shared browser capacity
- [ ] tenant quotas
- [ ] affinity effect documented

## Weighted

- [ ] only if implemented
- [ ] measured share approximates configured weight
- [ ] no starvation

---

# APPENDIX K — Observability Field Dictionary

## Run

- [ ] run_id
- [ ] workflow_id
- [ ] workflow_version_id
- [ ] org_id
- [ ] engine
- [ ] trace_id

## Node

- [ ] node_id
- [ ] node_type
- [ ] logical_status
- [ ] ready_at
- [ ] terminal_at

## Attempt

- [ ] attempt_id
- [ ] attempt_number
- [ ] worker_id
- [ ] lease_expires_at
- [ ] error_class

## Transport

- [ ] redis_message_id
- [ ] enqueue_at
- [ ] dequeue/start_at
- [ ] ack_at

## Browser

- [ ] browser_session_id only where authorized
- [ ] affinity key
- [ ] resource owner worker

---

# APPENDIX L — Benchmark Result Directory Contract

## manifest.json

- [ ] commit SHA
- [ ] dirty flag must be false for final evidence
- [ ] hardware
- [ ] OS
- [ ] compiler
- [ ] build type
- [ ] command
- [ ] seed
- [ ] sample count

## raw.csv or raw.jsonl

- [ ] one observation per row
- [ ] timestamps/durations
- [ ] scenario labels
- [ ] no manually edited summary values

## summary.json

- [ ] mean
- [ ] stdev
- [ ] p50
- [ ] p95
- [ ] p99 when valid
- [ ] min/max
- [ ] n

## README.md

- [ ] method
- [ ] limitations
- [ ] exact commands
- [ ] interpretation

## checksums

- [ ] SHA256 for evidence artifacts where practical

---

# APPENDIX M — Final Chaos Campaign

## Worker faults

- [ ] kill active worker
- [ ] slow heartbeat
- [ ] duplicate worker ID rejection/handling
- [ ] graceful shutdown

## Scheduler faults

- [ ] kill/restart during active run
- [ ] restart during no work
- [ ] reconcile pending

## Redis faults

- [ ] temporary unavailable
- [ ] connection reset
- [ ] duplicate/redelivery

## Postgres faults

- [ ] temporary unavailable
- [ ] transaction failure
- [ ] recovery without double unlock

## Application faults

- [ ] cancel race
- [ ] invalid payload
- [ ] unauthorized event access
- [ ] overload/backpressure

---

# APPENDIX N — CI Matrix

## Node Phase 1

- [ ] npm test
- [ ] typecheck
- [ ] lint
- [ ] build where env permits

## C++ GCC

- [ ] Release build
- [ ] CTest

## C++ Clang

- [ ] Release build
- [ ] CTest

## Sanitizers

- [ ] ASan/UBSan
- [ ] TSan separate if supported

## Distributed

- [ ] Redis service
- [ ] Postgres service
- [ ] scheduler
- [ ] synthetic workers
- [ ] E2E test

## Security

- [ ] secret pattern scan
- [ ] auth-negative unit/integration tests

## Benchmark smoke

- [ ] short deterministic run only
- [ ] no timing threshold

---

# APPENDIX O — Resume Evidence Registry Rules

## GREEN

- [ ] implemented
- [ ] tested
- [ ] reproducible evidence
- [ ] claim wording matches exact scope

## YELLOW

- [ ] implemented but metric not reproduced/final
- [ ] or external/manual-only proof
- [ ] do not use quantified claim yet

## RED

- [ ] not implemented
- [ ] failed test
- [ ] unsupported inference
- [ ] never place on resume

## Each claim

- [ ] must link commit
- [ ] must link command
- [ ] must link raw result
- [ ] must state workload
- [ ] must state caveat

---

# APPENDIX P — Candidate Resume Claim Templates — PLACEHOLDERS ONLY

## Scheduler

- [ ] Engineered a C++20 multithreaded DAG scheduler using bounded worker threads and dependency-aware dispatch, achieving [MEASURED X] on [DEFINED WORKLOAD].

## Distributed

- [ ] Built a Redis Streams-based distributed execution runtime with task leases, heartbeats, retries, and crash recovery across [MEASURED N] workers, sustaining [MEASURED THROUGHPUT] at [MEASURED P95].

## Reliability

- [ ] Implemented at-least-once task delivery with idempotent logical commits and duplicate suppression, recovering [MEASURED RESULT] under [DEFINED FAILURE INJECTION].

## Multi-tenant

- [ ] Designed tenant quotas/backpressure and fair scheduling, reducing/limiting [MEASURED WAIT/FAIRNESS RESULT] under noisy-neighbor load.

---

# APPENDIX Q — Questions the implementation agent must be able to answer before Phase 2 is considered interview-ready

The final code is not enough. The implementation report must support concise, technically correct answers to each question below.
1. Why was a bounded thread pool chosen instead of one thread per node?
2. Why `std::jthread` instead of detached `std::thread`?
3. How does the ready queue avoid lost wakeups?
4. What is the lock ordering in the scheduler?
5. Where are atomics used and why are they safe?
6. What prevents a fan-in node from starting after only one predecessor completes?
7. How is a duplicated completion event handled?
8. What happens when cancellation races a successful completion?
9. How does browser session affinity limit parallelism?
10. Why is it unsafe to run two browser-mutating nodes on the same page concurrently?
11. What work can still run concurrently inside one workflow?
12. How does the system scale across independent workflows?
13. Why Redis Streams instead of plain Pub/Sub?
14. What does a Redis consumer group guarantee and what does it not guarantee?
15. Why are application leases still necessary?
16. How is a slow worker distinguished from a dead worker?
17. What exactly does a heartbeat prove?
18. What happens to a task when its lease expires?
19. What happens if the expired worker later reports success?
20. Why does the system describe delivery as at-least-once?
21. Why is exactly-once execution a dangerous claim?
22. What is the external side-effect crash window for email?
23. How does the idempotency ledger reduce duplicates?
24. What duplicate cases are still fundamentally ambiguous without provider support?
25. How does a scheduler restart recover active run state?
26. What is stored in Redis vs Postgres?
27. Why is Postgres the audit/history source while Redis is transport/coordination?
28. What makes a workflow version immutable?
29. How does optimistic concurrency avoid silent overwrite?
30. How are tenant quotas different from fairness?
31. Why choose weighted round robin/deficit round robin over one global FIFO?
32. What workload shows noisy-neighbor starvation in the baseline?
33. How is fairness measured?
34. What is backpressure and where is it enforced?
35. What happens when the global queue limit is reached?
36. How is cancellation propagated from the UI to a browser worker?
37. Why might browser cancellation latency be longer than scheduler cancellation latency?
38. What is ready-to-dispatch scheduling latency?
39. What is queue latency?
40. What is workflow makespan?
41. How is speedup calculated?
42. Why can parallel efficiency fall as threads increase?
43. Why are Browserbase end-to-end timings separated from scheduler microbenchmarks?
44. Why are Debug builds invalid for final performance claims?
45. Why are raw samples retained?
46. Why should CI runner timings not be used as final resume metrics?
47. How were failure tests injected?
48. How was worker recovery time measured?
49. What exact limitations remain after Phase 2?
50. Which resume claims are GREEN and where is the evidence?

# APPENDIX R — Detailed benchmark scenario catalog

Each scenario must have a stable ID so raw results can be compared without ambiguity.
## Scenario S001 — linear / n=10 / io-sleep-10ms
- Shape: `linear`
- Logical node count target: `10`
- Work profile: `io-sleep-10ms`
- Required comparison: sequential reference vs concurrent scheduler when the shape has exploitable parallelism.
- Required samples: choose enough repeated trials to support the requested percentile reporting; record actual n in the manifest.
- Required output: raw per-run makespan plus per-node ready/dispatch/start/finish timings.
- Required interpretation: explicitly say whether the topology limits speedup.
- Do not omit the scenario because it performs worse than expected.

## Scenario S002 — linear / n=10 / io-sleep-50ms
- Shape: `linear`
- Logical node count target: `10`
- Work profile: `io-sleep-50ms`
- Required comparison: sequential reference vs concurrent scheduler when the shape has exploitable parallelism.
- Required samples: choose enough repeated trials to support the requested percentile reporting; record actual n in the manifest.
- Required output: raw per-run makespan plus per-node ready/dispatch/start/finish timings.
- Required interpretation: explicitly say whether the topology limits speedup.
- Do not omit the scenario because it performs worse than expected.

## Scenario S003 — linear / n=10 / cpu-fixed-small
- Shape: `linear`
- Logical node count target: `10`
- Work profile: `cpu-fixed-small`
- Required comparison: sequential reference vs concurrent scheduler when the shape has exploitable parallelism.
- Required samples: choose enough repeated trials to support the requested percentile reporting; record actual n in the manifest.
- Required output: raw per-run makespan plus per-node ready/dispatch/start/finish timings.
- Required interpretation: explicitly say whether the topology limits speedup.
- Do not omit the scenario because it performs worse than expected.

## Scenario S004 — linear / n=50 / io-sleep-10ms
- Shape: `linear`
- Logical node count target: `50`
- Work profile: `io-sleep-10ms`
- Required comparison: sequential reference vs concurrent scheduler when the shape has exploitable parallelism.
- Required samples: choose enough repeated trials to support the requested percentile reporting; record actual n in the manifest.
- Required output: raw per-run makespan plus per-node ready/dispatch/start/finish timings.
- Required interpretation: explicitly say whether the topology limits speedup.
- Do not omit the scenario because it performs worse than expected.

## Scenario S005 — linear / n=50 / io-sleep-50ms
- Shape: `linear`
- Logical node count target: `50`
- Work profile: `io-sleep-50ms`
- Required comparison: sequential reference vs concurrent scheduler when the shape has exploitable parallelism.
- Required samples: choose enough repeated trials to support the requested percentile reporting; record actual n in the manifest.
- Required output: raw per-run makespan plus per-node ready/dispatch/start/finish timings.
- Required interpretation: explicitly say whether the topology limits speedup.
- Do not omit the scenario because it performs worse than expected.

## Scenario S006 — linear / n=50 / cpu-fixed-small
- Shape: `linear`
- Logical node count target: `50`
- Work profile: `cpu-fixed-small`
- Required comparison: sequential reference vs concurrent scheduler when the shape has exploitable parallelism.
- Required samples: choose enough repeated trials to support the requested percentile reporting; record actual n in the manifest.
- Required output: raw per-run makespan plus per-node ready/dispatch/start/finish timings.
- Required interpretation: explicitly say whether the topology limits speedup.
- Do not omit the scenario because it performs worse than expected.

## Scenario S007 — linear / n=100 / io-sleep-10ms
- Shape: `linear`
- Logical node count target: `100`
- Work profile: `io-sleep-10ms`
- Required comparison: sequential reference vs concurrent scheduler when the shape has exploitable parallelism.
- Required samples: choose enough repeated trials to support the requested percentile reporting; record actual n in the manifest.
- Required output: raw per-run makespan plus per-node ready/dispatch/start/finish timings.
- Required interpretation: explicitly say whether the topology limits speedup.
- Do not omit the scenario because it performs worse than expected.

## Scenario S008 — linear / n=100 / io-sleep-50ms
- Shape: `linear`
- Logical node count target: `100`
- Work profile: `io-sleep-50ms`
- Required comparison: sequential reference vs concurrent scheduler when the shape has exploitable parallelism.
- Required samples: choose enough repeated trials to support the requested percentile reporting; record actual n in the manifest.
- Required output: raw per-run makespan plus per-node ready/dispatch/start/finish timings.
- Required interpretation: explicitly say whether the topology limits speedup.
- Do not omit the scenario because it performs worse than expected.

## Scenario S009 — linear / n=100 / cpu-fixed-small
- Shape: `linear`
- Logical node count target: `100`
- Work profile: `cpu-fixed-small`
- Required comparison: sequential reference vs concurrent scheduler when the shape has exploitable parallelism.
- Required samples: choose enough repeated trials to support the requested percentile reporting; record actual n in the manifest.
- Required output: raw per-run makespan plus per-node ready/dispatch/start/finish timings.
- Required interpretation: explicitly say whether the topology limits speedup.
- Do not omit the scenario because it performs worse than expected.

## Scenario S010 — linear / n=500 / io-sleep-10ms
- Shape: `linear`
- Logical node count target: `500`
- Work profile: `io-sleep-10ms`
- Required comparison: sequential reference vs concurrent scheduler when the shape has exploitable parallelism.
- Required samples: choose enough repeated trials to support the requested percentile reporting; record actual n in the manifest.
- Required output: raw per-run makespan plus per-node ready/dispatch/start/finish timings.
- Required interpretation: explicitly say whether the topology limits speedup.
- Do not omit the scenario because it performs worse than expected.

## Scenario S011 — linear / n=500 / io-sleep-50ms
- Shape: `linear`
- Logical node count target: `500`
- Work profile: `io-sleep-50ms`
- Required comparison: sequential reference vs concurrent scheduler when the shape has exploitable parallelism.
- Required samples: choose enough repeated trials to support the requested percentile reporting; record actual n in the manifest.
- Required output: raw per-run makespan plus per-node ready/dispatch/start/finish timings.
- Required interpretation: explicitly say whether the topology limits speedup.
- Do not omit the scenario because it performs worse than expected.

## Scenario S012 — linear / n=500 / cpu-fixed-small
- Shape: `linear`
- Logical node count target: `500`
- Work profile: `cpu-fixed-small`
- Required comparison: sequential reference vs concurrent scheduler when the shape has exploitable parallelism.
- Required samples: choose enough repeated trials to support the requested percentile reporting; record actual n in the manifest.
- Required output: raw per-run makespan plus per-node ready/dispatch/start/finish timings.
- Required interpretation: explicitly say whether the topology limits speedup.
- Do not omit the scenario because it performs worse than expected.

## Scenario S013 — diamond / n=10 / io-sleep-10ms
- Shape: `diamond`
- Logical node count target: `10`
- Work profile: `io-sleep-10ms`
- Required comparison: sequential reference vs concurrent scheduler when the shape has exploitable parallelism.
- Required samples: choose enough repeated trials to support the requested percentile reporting; record actual n in the manifest.
- Required output: raw per-run makespan plus per-node ready/dispatch/start/finish timings.
- Required interpretation: explicitly say whether the topology limits speedup.
- Do not omit the scenario because it performs worse than expected.

## Scenario S014 — diamond / n=10 / io-sleep-50ms
- Shape: `diamond`
- Logical node count target: `10`
- Work profile: `io-sleep-50ms`
- Required comparison: sequential reference vs concurrent scheduler when the shape has exploitable parallelism.
- Required samples: choose enough repeated trials to support the requested percentile reporting; record actual n in the manifest.
- Required output: raw per-run makespan plus per-node ready/dispatch/start/finish timings.
- Required interpretation: explicitly say whether the topology limits speedup.
- Do not omit the scenario because it performs worse than expected.

## Scenario S015 — diamond / n=10 / cpu-fixed-small
- Shape: `diamond`
- Logical node count target: `10`
- Work profile: `cpu-fixed-small`
- Required comparison: sequential reference vs concurrent scheduler when the shape has exploitable parallelism.
- Required samples: choose enough repeated trials to support the requested percentile reporting; record actual n in the manifest.
- Required output: raw per-run makespan plus per-node ready/dispatch/start/finish timings.
- Required interpretation: explicitly say whether the topology limits speedup.
- Do not omit the scenario because it performs worse than expected.

## Scenario S016 — diamond / n=50 / io-sleep-10ms
- Shape: `diamond`
- Logical node count target: `50`
- Work profile: `io-sleep-10ms`
- Required comparison: sequential reference vs concurrent scheduler when the shape has exploitable parallelism.
- Required samples: choose enough repeated trials to support the requested percentile reporting; record actual n in the manifest.
- Required output: raw per-run makespan plus per-node ready/dispatch/start/finish timings.
- Required interpretation: explicitly say whether the topology limits speedup.
- Do not omit the scenario because it performs worse than expected.

## Scenario S017 — diamond / n=50 / io-sleep-50ms
- Shape: `diamond`
- Logical node count target: `50`
- Work profile: `io-sleep-50ms`
- Required comparison: sequential reference vs concurrent scheduler when the shape has exploitable parallelism.
- Required samples: choose enough repeated trials to support the requested percentile reporting; record actual n in the manifest.
- Required output: raw per-run makespan plus per-node ready/dispatch/start/finish timings.
- Required interpretation: explicitly say whether the topology limits speedup.
- Do not omit the scenario because it performs worse than expected.

## Scenario S018 — diamond / n=50 / cpu-fixed-small
- Shape: `diamond`
- Logical node count target: `50`
- Work profile: `cpu-fixed-small`
- Required comparison: sequential reference vs concurrent scheduler when the shape has exploitable parallelism.
- Required samples: choose enough repeated trials to support the requested percentile reporting; record actual n in the manifest.
- Required output: raw per-run makespan plus per-node ready/dispatch/start/finish timings.
- Required interpretation: explicitly say whether the topology limits speedup.
- Do not omit the scenario because it performs worse than expected.

## Scenario S019 — diamond / n=100 / io-sleep-10ms
- Shape: `diamond`
- Logical node count target: `100`
- Work profile: `io-sleep-10ms`
- Required comparison: sequential reference vs concurrent scheduler when the shape has exploitable parallelism.
- Required samples: choose enough repeated trials to support the requested percentile reporting; record actual n in the manifest.
- Required output: raw per-run makespan plus per-node ready/dispatch/start/finish timings.
- Required interpretation: explicitly say whether the topology limits speedup.
- Do not omit the scenario because it performs worse than expected.

## Scenario S020 — diamond / n=100 / io-sleep-50ms
- Shape: `diamond`
- Logical node count target: `100`
- Work profile: `io-sleep-50ms`
- Required comparison: sequential reference vs concurrent scheduler when the shape has exploitable parallelism.
- Required samples: choose enough repeated trials to support the requested percentile reporting; record actual n in the manifest.
- Required output: raw per-run makespan plus per-node ready/dispatch/start/finish timings.
- Required interpretation: explicitly say whether the topology limits speedup.
- Do not omit the scenario because it performs worse than expected.

## Scenario S021 — diamond / n=100 / cpu-fixed-small
- Shape: `diamond`
- Logical node count target: `100`
- Work profile: `cpu-fixed-small`
- Required comparison: sequential reference vs concurrent scheduler when the shape has exploitable parallelism.
- Required samples: choose enough repeated trials to support the requested percentile reporting; record actual n in the manifest.
- Required output: raw per-run makespan plus per-node ready/dispatch/start/finish timings.
- Required interpretation: explicitly say whether the topology limits speedup.
- Do not omit the scenario because it performs worse than expected.

## Scenario S022 — diamond / n=500 / io-sleep-10ms
- Shape: `diamond`
- Logical node count target: `500`
- Work profile: `io-sleep-10ms`
- Required comparison: sequential reference vs concurrent scheduler when the shape has exploitable parallelism.
- Required samples: choose enough repeated trials to support the requested percentile reporting; record actual n in the manifest.
- Required output: raw per-run makespan plus per-node ready/dispatch/start/finish timings.
- Required interpretation: explicitly say whether the topology limits speedup.
- Do not omit the scenario because it performs worse than expected.

## Scenario S023 — diamond / n=500 / io-sleep-50ms
- Shape: `diamond`
- Logical node count target: `500`
- Work profile: `io-sleep-50ms`
- Required comparison: sequential reference vs concurrent scheduler when the shape has exploitable parallelism.
- Required samples: choose enough repeated trials to support the requested percentile reporting; record actual n in the manifest.
- Required output: raw per-run makespan plus per-node ready/dispatch/start/finish timings.
- Required interpretation: explicitly say whether the topology limits speedup.
- Do not omit the scenario because it performs worse than expected.

## Scenario S024 — diamond / n=500 / cpu-fixed-small
- Shape: `diamond`
- Logical node count target: `500`
- Work profile: `cpu-fixed-small`
- Required comparison: sequential reference vs concurrent scheduler when the shape has exploitable parallelism.
- Required samples: choose enough repeated trials to support the requested percentile reporting; record actual n in the manifest.
- Required output: raw per-run makespan plus per-node ready/dispatch/start/finish timings.
- Required interpretation: explicitly say whether the topology limits speedup.
- Do not omit the scenario because it performs worse than expected.

## Scenario S025 — wide-fanout-fanin / n=10 / io-sleep-10ms
- Shape: `wide-fanout-fanin`
- Logical node count target: `10`
- Work profile: `io-sleep-10ms`
- Required comparison: sequential reference vs concurrent scheduler when the shape has exploitable parallelism.
- Required samples: choose enough repeated trials to support the requested percentile reporting; record actual n in the manifest.
- Required output: raw per-run makespan plus per-node ready/dispatch/start/finish timings.
- Required interpretation: explicitly say whether the topology limits speedup.
- Do not omit the scenario because it performs worse than expected.

## Scenario S026 — wide-fanout-fanin / n=10 / io-sleep-50ms
- Shape: `wide-fanout-fanin`
- Logical node count target: `10`
- Work profile: `io-sleep-50ms`
- Required comparison: sequential reference vs concurrent scheduler when the shape has exploitable parallelism.
- Required samples: choose enough repeated trials to support the requested percentile reporting; record actual n in the manifest.
- Required output: raw per-run makespan plus per-node ready/dispatch/start/finish timings.
- Required interpretation: explicitly say whether the topology limits speedup.
- Do not omit the scenario because it performs worse than expected.

## Scenario S027 — wide-fanout-fanin / n=10 / cpu-fixed-small
- Shape: `wide-fanout-fanin`
- Logical node count target: `10`
- Work profile: `cpu-fixed-small`
- Required comparison: sequential reference vs concurrent scheduler when the shape has exploitable parallelism.
- Required samples: choose enough repeated trials to support the requested percentile reporting; record actual n in the manifest.
- Required output: raw per-run makespan plus per-node ready/dispatch/start/finish timings.
- Required interpretation: explicitly say whether the topology limits speedup.
- Do not omit the scenario because it performs worse than expected.

## Scenario S028 — wide-fanout-fanin / n=50 / io-sleep-10ms
- Shape: `wide-fanout-fanin`
- Logical node count target: `50`
- Work profile: `io-sleep-10ms`
- Required comparison: sequential reference vs concurrent scheduler when the shape has exploitable parallelism.
- Required samples: choose enough repeated trials to support the requested percentile reporting; record actual n in the manifest.
- Required output: raw per-run makespan plus per-node ready/dispatch/start/finish timings.
- Required interpretation: explicitly say whether the topology limits speedup.
- Do not omit the scenario because it performs worse than expected.

## Scenario S029 — wide-fanout-fanin / n=50 / io-sleep-50ms
- Shape: `wide-fanout-fanin`
- Logical node count target: `50`
- Work profile: `io-sleep-50ms`
- Required comparison: sequential reference vs concurrent scheduler when the shape has exploitable parallelism.
- Required samples: choose enough repeated trials to support the requested percentile reporting; record actual n in the manifest.
- Required output: raw per-run makespan plus per-node ready/dispatch/start/finish timings.
- Required interpretation: explicitly say whether the topology limits speedup.
- Do not omit the scenario because it performs worse than expected.

## Scenario S030 — wide-fanout-fanin / n=50 / cpu-fixed-small
- Shape: `wide-fanout-fanin`
- Logical node count target: `50`
- Work profile: `cpu-fixed-small`
- Required comparison: sequential reference vs concurrent scheduler when the shape has exploitable parallelism.
- Required samples: choose enough repeated trials to support the requested percentile reporting; record actual n in the manifest.
- Required output: raw per-run makespan plus per-node ready/dispatch/start/finish timings.
- Required interpretation: explicitly say whether the topology limits speedup.
- Do not omit the scenario because it performs worse than expected.

## Scenario S031 — wide-fanout-fanin / n=100 / io-sleep-10ms
- Shape: `wide-fanout-fanin`
- Logical node count target: `100`
- Work profile: `io-sleep-10ms`
- Required comparison: sequential reference vs concurrent scheduler when the shape has exploitable parallelism.
- Required samples: choose enough repeated trials to support the requested percentile reporting; record actual n in the manifest.
- Required output: raw per-run makespan plus per-node ready/dispatch/start/finish timings.
- Required interpretation: explicitly say whether the topology limits speedup.
- Do not omit the scenario because it performs worse than expected.

## Scenario S032 — wide-fanout-fanin / n=100 / io-sleep-50ms
- Shape: `wide-fanout-fanin`
- Logical node count target: `100`
- Work profile: `io-sleep-50ms`
- Required comparison: sequential reference vs concurrent scheduler when the shape has exploitable parallelism.
- Required samples: choose enough repeated trials to support the requested percentile reporting; record actual n in the manifest.
- Required output: raw per-run makespan plus per-node ready/dispatch/start/finish timings.
- Required interpretation: explicitly say whether the topology limits speedup.
- Do not omit the scenario because it performs worse than expected.

## Scenario S033 — wide-fanout-fanin / n=100 / cpu-fixed-small
- Shape: `wide-fanout-fanin`
- Logical node count target: `100`
- Work profile: `cpu-fixed-small`
- Required comparison: sequential reference vs concurrent scheduler when the shape has exploitable parallelism.
- Required samples: choose enough repeated trials to support the requested percentile reporting; record actual n in the manifest.
- Required output: raw per-run makespan plus per-node ready/dispatch/start/finish timings.
- Required interpretation: explicitly say whether the topology limits speedup.
- Do not omit the scenario because it performs worse than expected.

## Scenario S034 — wide-fanout-fanin / n=500 / io-sleep-10ms
- Shape: `wide-fanout-fanin`
- Logical node count target: `500`
- Work profile: `io-sleep-10ms`
- Required comparison: sequential reference vs concurrent scheduler when the shape has exploitable parallelism.
- Required samples: choose enough repeated trials to support the requested percentile reporting; record actual n in the manifest.
- Required output: raw per-run makespan plus per-node ready/dispatch/start/finish timings.
- Required interpretation: explicitly say whether the topology limits speedup.
- Do not omit the scenario because it performs worse than expected.

## Scenario S035 — wide-fanout-fanin / n=500 / io-sleep-50ms
- Shape: `wide-fanout-fanin`
- Logical node count target: `500`
- Work profile: `io-sleep-50ms`
- Required comparison: sequential reference vs concurrent scheduler when the shape has exploitable parallelism.
- Required samples: choose enough repeated trials to support the requested percentile reporting; record actual n in the manifest.
- Required output: raw per-run makespan plus per-node ready/dispatch/start/finish timings.
- Required interpretation: explicitly say whether the topology limits speedup.
- Do not omit the scenario because it performs worse than expected.

## Scenario S036 — wide-fanout-fanin / n=500 / cpu-fixed-small
- Shape: `wide-fanout-fanin`
- Logical node count target: `500`
- Work profile: `cpu-fixed-small`
- Required comparison: sequential reference vs concurrent scheduler when the shape has exploitable parallelism.
- Required samples: choose enough repeated trials to support the requested percentile reporting; record actual n in the manifest.
- Required output: raw per-run makespan plus per-node ready/dispatch/start/finish timings.
- Required interpretation: explicitly say whether the topology limits speedup.
- Do not omit the scenario because it performs worse than expected.

## Scenario S037 — layered / n=10 / io-sleep-10ms
- Shape: `layered`
- Logical node count target: `10`
- Work profile: `io-sleep-10ms`
- Required comparison: sequential reference vs concurrent scheduler when the shape has exploitable parallelism.
- Required samples: choose enough repeated trials to support the requested percentile reporting; record actual n in the manifest.
- Required output: raw per-run makespan plus per-node ready/dispatch/start/finish timings.
- Required interpretation: explicitly say whether the topology limits speedup.
- Do not omit the scenario because it performs worse than expected.

## Scenario S038 — layered / n=10 / io-sleep-50ms
- Shape: `layered`
- Logical node count target: `10`
- Work profile: `io-sleep-50ms`
- Required comparison: sequential reference vs concurrent scheduler when the shape has exploitable parallelism.
- Required samples: choose enough repeated trials to support the requested percentile reporting; record actual n in the manifest.
- Required output: raw per-run makespan plus per-node ready/dispatch/start/finish timings.
- Required interpretation: explicitly say whether the topology limits speedup.
- Do not omit the scenario because it performs worse than expected.

## Scenario S039 — layered / n=10 / cpu-fixed-small
- Shape: `layered`
- Logical node count target: `10`
- Work profile: `cpu-fixed-small`
- Required comparison: sequential reference vs concurrent scheduler when the shape has exploitable parallelism.
- Required samples: choose enough repeated trials to support the requested percentile reporting; record actual n in the manifest.
- Required output: raw per-run makespan plus per-node ready/dispatch/start/finish timings.
- Required interpretation: explicitly say whether the topology limits speedup.
- Do not omit the scenario because it performs worse than expected.

## Scenario S040 — layered / n=50 / io-sleep-10ms
- Shape: `layered`
- Logical node count target: `50`
- Work profile: `io-sleep-10ms`
- Required comparison: sequential reference vs concurrent scheduler when the shape has exploitable parallelism.
- Required samples: choose enough repeated trials to support the requested percentile reporting; record actual n in the manifest.
- Required output: raw per-run makespan plus per-node ready/dispatch/start/finish timings.
- Required interpretation: explicitly say whether the topology limits speedup.
- Do not omit the scenario because it performs worse than expected.

## Scenario S041 — layered / n=50 / io-sleep-50ms
- Shape: `layered`
- Logical node count target: `50`
- Work profile: `io-sleep-50ms`
- Required comparison: sequential reference vs concurrent scheduler when the shape has exploitable parallelism.
- Required samples: choose enough repeated trials to support the requested percentile reporting; record actual n in the manifest.
- Required output: raw per-run makespan plus per-node ready/dispatch/start/finish timings.
- Required interpretation: explicitly say whether the topology limits speedup.
- Do not omit the scenario because it performs worse than expected.

## Scenario S042 — layered / n=50 / cpu-fixed-small
- Shape: `layered`
- Logical node count target: `50`
- Work profile: `cpu-fixed-small`
- Required comparison: sequential reference vs concurrent scheduler when the shape has exploitable parallelism.
- Required samples: choose enough repeated trials to support the requested percentile reporting; record actual n in the manifest.
- Required output: raw per-run makespan plus per-node ready/dispatch/start/finish timings.
- Required interpretation: explicitly say whether the topology limits speedup.
- Do not omit the scenario because it performs worse than expected.

## Scenario S043 — layered / n=100 / io-sleep-10ms
- Shape: `layered`
- Logical node count target: `100`
- Work profile: `io-sleep-10ms`
- Required comparison: sequential reference vs concurrent scheduler when the shape has exploitable parallelism.
- Required samples: choose enough repeated trials to support the requested percentile reporting; record actual n in the manifest.
- Required output: raw per-run makespan plus per-node ready/dispatch/start/finish timings.
- Required interpretation: explicitly say whether the topology limits speedup.
- Do not omit the scenario because it performs worse than expected.

## Scenario S044 — layered / n=100 / io-sleep-50ms
- Shape: `layered`
- Logical node count target: `100`
- Work profile: `io-sleep-50ms`
- Required comparison: sequential reference vs concurrent scheduler when the shape has exploitable parallelism.
- Required samples: choose enough repeated trials to support the requested percentile reporting; record actual n in the manifest.
- Required output: raw per-run makespan plus per-node ready/dispatch/start/finish timings.
- Required interpretation: explicitly say whether the topology limits speedup.
- Do not omit the scenario because it performs worse than expected.

## Scenario S045 — layered / n=100 / cpu-fixed-small
- Shape: `layered`
- Logical node count target: `100`
- Work profile: `cpu-fixed-small`
- Required comparison: sequential reference vs concurrent scheduler when the shape has exploitable parallelism.
- Required samples: choose enough repeated trials to support the requested percentile reporting; record actual n in the manifest.
- Required output: raw per-run makespan plus per-node ready/dispatch/start/finish timings.
- Required interpretation: explicitly say whether the topology limits speedup.
- Do not omit the scenario because it performs worse than expected.

## Scenario S046 — layered / n=500 / io-sleep-10ms
- Shape: `layered`
- Logical node count target: `500`
- Work profile: `io-sleep-10ms`
- Required comparison: sequential reference vs concurrent scheduler when the shape has exploitable parallelism.
- Required samples: choose enough repeated trials to support the requested percentile reporting; record actual n in the manifest.
- Required output: raw per-run makespan plus per-node ready/dispatch/start/finish timings.
- Required interpretation: explicitly say whether the topology limits speedup.
- Do not omit the scenario because it performs worse than expected.

## Scenario S047 — layered / n=500 / io-sleep-50ms
- Shape: `layered`
- Logical node count target: `500`
- Work profile: `io-sleep-50ms`
- Required comparison: sequential reference vs concurrent scheduler when the shape has exploitable parallelism.
- Required samples: choose enough repeated trials to support the requested percentile reporting; record actual n in the manifest.
- Required output: raw per-run makespan plus per-node ready/dispatch/start/finish timings.
- Required interpretation: explicitly say whether the topology limits speedup.
- Do not omit the scenario because it performs worse than expected.

## Scenario S048 — layered / n=500 / cpu-fixed-small
- Shape: `layered`
- Logical node count target: `500`
- Work profile: `cpu-fixed-small`
- Required comparison: sequential reference vs concurrent scheduler when the shape has exploitable parallelism.
- Required samples: choose enough repeated trials to support the requested percentile reporting; record actual n in the manifest.
- Required output: raw per-run makespan plus per-node ready/dispatch/start/finish timings.
- Required interpretation: explicitly say whether the topology limits speedup.
- Do not omit the scenario because it performs worse than expected.

## Scenario S049 — seeded-random / n=10 / io-sleep-10ms
- Shape: `seeded-random`
- Logical node count target: `10`
- Work profile: `io-sleep-10ms`
- Required comparison: sequential reference vs concurrent scheduler when the shape has exploitable parallelism.
- Required samples: choose enough repeated trials to support the requested percentile reporting; record actual n in the manifest.
- Required output: raw per-run makespan plus per-node ready/dispatch/start/finish timings.
- Required interpretation: explicitly say whether the topology limits speedup.
- Do not omit the scenario because it performs worse than expected.

## Scenario S050 — seeded-random / n=10 / io-sleep-50ms
- Shape: `seeded-random`
- Logical node count target: `10`
- Work profile: `io-sleep-50ms`
- Required comparison: sequential reference vs concurrent scheduler when the shape has exploitable parallelism.
- Required samples: choose enough repeated trials to support the requested percentile reporting; record actual n in the manifest.
- Required output: raw per-run makespan plus per-node ready/dispatch/start/finish timings.
- Required interpretation: explicitly say whether the topology limits speedup.
- Do not omit the scenario because it performs worse than expected.

## Scenario S051 — seeded-random / n=10 / cpu-fixed-small
- Shape: `seeded-random`
- Logical node count target: `10`
- Work profile: `cpu-fixed-small`
- Required comparison: sequential reference vs concurrent scheduler when the shape has exploitable parallelism.
- Required samples: choose enough repeated trials to support the requested percentile reporting; record actual n in the manifest.
- Required output: raw per-run makespan plus per-node ready/dispatch/start/finish timings.
- Required interpretation: explicitly say whether the topology limits speedup.
- Do not omit the scenario because it performs worse than expected.

## Scenario S052 — seeded-random / n=50 / io-sleep-10ms
- Shape: `seeded-random`
- Logical node count target: `50`
- Work profile: `io-sleep-10ms`
- Required comparison: sequential reference vs concurrent scheduler when the shape has exploitable parallelism.
- Required samples: choose enough repeated trials to support the requested percentile reporting; record actual n in the manifest.
- Required output: raw per-run makespan plus per-node ready/dispatch/start/finish timings.
- Required interpretation: explicitly say whether the topology limits speedup.
- Do not omit the scenario because it performs worse than expected.

## Scenario S053 — seeded-random / n=50 / io-sleep-50ms
- Shape: `seeded-random`
- Logical node count target: `50`
- Work profile: `io-sleep-50ms`
- Required comparison: sequential reference vs concurrent scheduler when the shape has exploitable parallelism.
- Required samples: choose enough repeated trials to support the requested percentile reporting; record actual n in the manifest.
- Required output: raw per-run makespan plus per-node ready/dispatch/start/finish timings.
- Required interpretation: explicitly say whether the topology limits speedup.
- Do not omit the scenario because it performs worse than expected.

## Scenario S054 — seeded-random / n=50 / cpu-fixed-small
- Shape: `seeded-random`
- Logical node count target: `50`
- Work profile: `cpu-fixed-small`
- Required comparison: sequential reference vs concurrent scheduler when the shape has exploitable parallelism.
- Required samples: choose enough repeated trials to support the requested percentile reporting; record actual n in the manifest.
- Required output: raw per-run makespan plus per-node ready/dispatch/start/finish timings.
- Required interpretation: explicitly say whether the topology limits speedup.
- Do not omit the scenario because it performs worse than expected.

## Scenario S055 — seeded-random / n=100 / io-sleep-10ms
- Shape: `seeded-random`
- Logical node count target: `100`
- Work profile: `io-sleep-10ms`
- Required comparison: sequential reference vs concurrent scheduler when the shape has exploitable parallelism.
- Required samples: choose enough repeated trials to support the requested percentile reporting; record actual n in the manifest.
- Required output: raw per-run makespan plus per-node ready/dispatch/start/finish timings.
- Required interpretation: explicitly say whether the topology limits speedup.
- Do not omit the scenario because it performs worse than expected.

## Scenario S056 — seeded-random / n=100 / io-sleep-50ms
- Shape: `seeded-random`
- Logical node count target: `100`
- Work profile: `io-sleep-50ms`
- Required comparison: sequential reference vs concurrent scheduler when the shape has exploitable parallelism.
- Required samples: choose enough repeated trials to support the requested percentile reporting; record actual n in the manifest.
- Required output: raw per-run makespan plus per-node ready/dispatch/start/finish timings.
- Required interpretation: explicitly say whether the topology limits speedup.
- Do not omit the scenario because it performs worse than expected.

## Scenario S057 — seeded-random / n=100 / cpu-fixed-small
- Shape: `seeded-random`
- Logical node count target: `100`
- Work profile: `cpu-fixed-small`
- Required comparison: sequential reference vs concurrent scheduler when the shape has exploitable parallelism.
- Required samples: choose enough repeated trials to support the requested percentile reporting; record actual n in the manifest.
- Required output: raw per-run makespan plus per-node ready/dispatch/start/finish timings.
- Required interpretation: explicitly say whether the topology limits speedup.
- Do not omit the scenario because it performs worse than expected.

## Scenario S058 — seeded-random / n=500 / io-sleep-10ms
- Shape: `seeded-random`
- Logical node count target: `500`
- Work profile: `io-sleep-10ms`
- Required comparison: sequential reference vs concurrent scheduler when the shape has exploitable parallelism.
- Required samples: choose enough repeated trials to support the requested percentile reporting; record actual n in the manifest.
- Required output: raw per-run makespan plus per-node ready/dispatch/start/finish timings.
- Required interpretation: explicitly say whether the topology limits speedup.
- Do not omit the scenario because it performs worse than expected.

## Scenario S059 — seeded-random / n=500 / io-sleep-50ms
- Shape: `seeded-random`
- Logical node count target: `500`
- Work profile: `io-sleep-50ms`
- Required comparison: sequential reference vs concurrent scheduler when the shape has exploitable parallelism.
- Required samples: choose enough repeated trials to support the requested percentile reporting; record actual n in the manifest.
- Required output: raw per-run makespan plus per-node ready/dispatch/start/finish timings.
- Required interpretation: explicitly say whether the topology limits speedup.
- Do not omit the scenario because it performs worse than expected.

## Scenario S060 — seeded-random / n=500 / cpu-fixed-small
- Shape: `seeded-random`
- Logical node count target: `500`
- Work profile: `cpu-fixed-small`
- Required comparison: sequential reference vs concurrent scheduler when the shape has exploitable parallelism.
- Required samples: choose enough repeated trials to support the requested percentile reporting; record actual n in the manifest.
- Required output: raw per-run makespan plus per-node ready/dispatch/start/finish timings.
- Required interpretation: explicitly say whether the topology limits speedup.
- Do not omit the scenario because it performs worse than expected.

# APPENDIX S — Distributed scaling scenario catalog
## Worker scaling — 1 worker(s), 100 logical tasks
- Worker count: 1
- Logical task target: 100
- Use the same task profile and DAG/workflow mix as the comparison runs.
- Collect throughput, queue latency, scheduler latency, queue depth high-water mark, worker utilization, retries, and errors.
- Repeat after warmup and preserve every raw sample.
- Calculate scaling relative to the 1-worker case only when all other conditions are identical.
- If the host cannot run this worker count without swapping or external throttling, record that limitation rather than hiding it.

## Worker scaling — 1 worker(s), 500 logical tasks
- Worker count: 1
- Logical task target: 500
- Use the same task profile and DAG/workflow mix as the comparison runs.
- Collect throughput, queue latency, scheduler latency, queue depth high-water mark, worker utilization, retries, and errors.
- Repeat after warmup and preserve every raw sample.
- Calculate scaling relative to the 1-worker case only when all other conditions are identical.
- If the host cannot run this worker count without swapping or external throttling, record that limitation rather than hiding it.

## Worker scaling — 1 worker(s), 1000 logical tasks
- Worker count: 1
- Logical task target: 1000
- Use the same task profile and DAG/workflow mix as the comparison runs.
- Collect throughput, queue latency, scheduler latency, queue depth high-water mark, worker utilization, retries, and errors.
- Repeat after warmup and preserve every raw sample.
- Calculate scaling relative to the 1-worker case only when all other conditions are identical.
- If the host cannot run this worker count without swapping or external throttling, record that limitation rather than hiding it.

## Worker scaling — 2 worker(s), 100 logical tasks
- Worker count: 2
- Logical task target: 100
- Use the same task profile and DAG/workflow mix as the comparison runs.
- Collect throughput, queue latency, scheduler latency, queue depth high-water mark, worker utilization, retries, and errors.
- Repeat after warmup and preserve every raw sample.
- Calculate scaling relative to the 1-worker case only when all other conditions are identical.
- If the host cannot run this worker count without swapping or external throttling, record that limitation rather than hiding it.

## Worker scaling — 2 worker(s), 500 logical tasks
- Worker count: 2
- Logical task target: 500
- Use the same task profile and DAG/workflow mix as the comparison runs.
- Collect throughput, queue latency, scheduler latency, queue depth high-water mark, worker utilization, retries, and errors.
- Repeat after warmup and preserve every raw sample.
- Calculate scaling relative to the 1-worker case only when all other conditions are identical.
- If the host cannot run this worker count without swapping or external throttling, record that limitation rather than hiding it.

## Worker scaling — 2 worker(s), 1000 logical tasks
- Worker count: 2
- Logical task target: 1000
- Use the same task profile and DAG/workflow mix as the comparison runs.
- Collect throughput, queue latency, scheduler latency, queue depth high-water mark, worker utilization, retries, and errors.
- Repeat after warmup and preserve every raw sample.
- Calculate scaling relative to the 1-worker case only when all other conditions are identical.
- If the host cannot run this worker count without swapping or external throttling, record that limitation rather than hiding it.

## Worker scaling — 4 worker(s), 100 logical tasks
- Worker count: 4
- Logical task target: 100
- Use the same task profile and DAG/workflow mix as the comparison runs.
- Collect throughput, queue latency, scheduler latency, queue depth high-water mark, worker utilization, retries, and errors.
- Repeat after warmup and preserve every raw sample.
- Calculate scaling relative to the 1-worker case only when all other conditions are identical.
- If the host cannot run this worker count without swapping or external throttling, record that limitation rather than hiding it.

## Worker scaling — 4 worker(s), 500 logical tasks
- Worker count: 4
- Logical task target: 500
- Use the same task profile and DAG/workflow mix as the comparison runs.
- Collect throughput, queue latency, scheduler latency, queue depth high-water mark, worker utilization, retries, and errors.
- Repeat after warmup and preserve every raw sample.
- Calculate scaling relative to the 1-worker case only when all other conditions are identical.
- If the host cannot run this worker count without swapping or external throttling, record that limitation rather than hiding it.

## Worker scaling — 4 worker(s), 1000 logical tasks
- Worker count: 4
- Logical task target: 1000
- Use the same task profile and DAG/workflow mix as the comparison runs.
- Collect throughput, queue latency, scheduler latency, queue depth high-water mark, worker utilization, retries, and errors.
- Repeat after warmup and preserve every raw sample.
- Calculate scaling relative to the 1-worker case only when all other conditions are identical.
- If the host cannot run this worker count without swapping or external throttling, record that limitation rather than hiding it.

## Worker scaling — 8 worker(s), 100 logical tasks
- Worker count: 8
- Logical task target: 100
- Use the same task profile and DAG/workflow mix as the comparison runs.
- Collect throughput, queue latency, scheduler latency, queue depth high-water mark, worker utilization, retries, and errors.
- Repeat after warmup and preserve every raw sample.
- Calculate scaling relative to the 1-worker case only when all other conditions are identical.
- If the host cannot run this worker count without swapping or external throttling, record that limitation rather than hiding it.

## Worker scaling — 8 worker(s), 500 logical tasks
- Worker count: 8
- Logical task target: 500
- Use the same task profile and DAG/workflow mix as the comparison runs.
- Collect throughput, queue latency, scheduler latency, queue depth high-water mark, worker utilization, retries, and errors.
- Repeat after warmup and preserve every raw sample.
- Calculate scaling relative to the 1-worker case only when all other conditions are identical.
- If the host cannot run this worker count without swapping or external throttling, record that limitation rather than hiding it.

## Worker scaling — 8 worker(s), 1000 logical tasks
- Worker count: 8
- Logical task target: 1000
- Use the same task profile and DAG/workflow mix as the comparison runs.
- Collect throughput, queue latency, scheduler latency, queue depth high-water mark, worker utilization, retries, and errors.
- Repeat after warmup and preserve every raw sample.
- Calculate scaling relative to the 1-worker case only when all other conditions are identical.
- If the host cannot run this worker count without swapping or external throttling, record that limitation rather than hiding it.

# APPENDIX T — Failure-injection experiment protocol

For every failure experiment, follow the same evidence sequence.

1. Start from a clean documented local stack.
2. Record commit SHA and configuration.
3. Start metrics collection.
4. Submit a deterministic workload.
5. Wait until the target failure precondition is observed.
6. Record the failure-injection timestamp.
7. Inject exactly one primary fault unless the experiment explicitly studies compound faults.
8. Record scheduler/worker/Redis/Postgres events.
9. Wait for a terminal outcome or bounded experiment timeout.
10. Export raw evidence.
11. Verify logical output count against expected work.
12. Verify duplicate logical commits.
13. Verify side-effect sink count when relevant.
14. Compute recovery/cancellation metrics from raw timestamps.
15. Repeat the experiment enough times to characterize variability.
16. Restore the environment before the next scenario.

Required failure cases:
- F01: kill one active synthetic worker
- F02: kill two active workers sequentially
- F03: gracefully stop a worker during idle
- F04: gracefully stop a worker during task execution
- F05: pause/slow a worker while heartbeats still renew
- F06: stop heartbeats while task continues
- F07: kill scheduler during active distributed run
- F08: restart scheduler after tasks are pending
- F09: temporarily stop Redis
- F10: restart Redis
- F11: temporarily stop local Postgres
- F12: restart Postgres
- F13: inject duplicate task message
- F14: inject duplicate result message
- F15: simulate lost acknowledgement
- F16: deliver a stale result from an expired attempt
- F17: cancel while task is READY
- F18: cancel while task is LEASED
- F19: cancel while synthetic task is RUNNING
- F20: cancel while browser task is RUNNING when live credentials are available
- F21: submit over tenant quota
- F22: submit over global queue bound
- F23: run noisy-neighbor tenant workload
- F24: run malformed protobuf/Redis payload
- F25: attempt cross-org event/run access

# APPENDIX U — Manual intervention playbook

The implementation agent must use these patterns rather than improvising secret handling.

## U.1 Missing Browserbase key

Say:

```text
STATUS: BLOCKED (external live-browser verification only)
Set BROWSERBASE_API_KEY in the server/worker environment or `.env.local` according to the existing project setup.
Do not paste the key into chat.
Confirm once configured.
```

Continue synthetic/non-browser milestones if Browserbase is not required for them.

## U.2 Missing Redis/Postgres local infrastructure

Prefer Docker Compose from Milestone 18.

If Docker is not installed, state the installation requirement and pause the infrastructure-dependent milestone.

Do not substitute the user's remote Neon database for destructive integration tests.

## U.3 Database migration to Neon

Generate and validate locally first.

Before applying to a shared Neon branch/database, say exactly:

```text
A new additive Phase-2 migration is ready and has passed against local Postgres.
Applying it to the configured Neon database changes persistent schema.
Please confirm that I may run the migration against that database.
```

## U.4 Missing compiler / CMake

Detect OS first.

Do not run privileged installs silently.

Tell the user the missing tool and official/platform-standard installation action.

## U.5 Missing Git identity

Do not fabricate author identity.

Ask the user to configure `user.name` and `user.email`, then retry the milestone commit.

## U.6 Paid benchmark usage

Do not launch a large Browserbase/model campaign merely to create numbers.

State estimated scenario count and ask for approval if the run can incur meaningful external cost.

Synthetic/local benchmarks should remain the primary scheduler evidence.

# APPENDIX V — Final evidence wording rules

The final report must distinguish these categories.

## Implementation claim

Example:

> Implemented a C++20 bounded-thread DAG scheduler with dependency counters and browser-resource affinity.

This requires code + tests.

## Reliability claim

Example:

> Reassigned expired synthetic tasks after worker failure using leases and heartbeats.

This requires failure-injection tests.

## Performance claim

Example placeholder only:

> Improved synthetic wide-DAG makespan by X× versus the C++ sequential reference on an N-node, Y-ms/task workload using Z worker threads.

This requires benchmark evidence.

## Distributed scaling claim

Example placeholder only:

> Sustained X logical tasks/s at p95 Y-ms queue latency using N local worker processes.

This requires distributed benchmark evidence.

## Claims that are forbidden without exceptional proof

- exactly-once execution
- zero task loss under every possible failure
- linear scaling
- lock-free scheduler
- browser tasks all run in parallel
- zero downtime
- production-grade at internet scale
- fault tolerant with no qualification
- X% faster browser automation when only synthetic scheduling was measured

# APPENDIX W — Phase-1 source areas to re-audit before integration changes

The following paths were present in the supplied archive. The implementation agent must re-check actual current paths because the repository may evolve.
## `README.md`
- Confirm the file still exists at this path or locate its successor.
- Read it before modifying any adjacent Phase-2 integration.
- Identify its Phase-1 behavioral contract.
- Identify tests that protect it.
- Do not duplicate its behavior in a new Phase-2 file without a documented reason.

## `AGENTS.md`
- Confirm the file still exists at this path or locate its successor.
- Read it before modifying any adjacent Phase-2 integration.
- Identify its Phase-1 behavioral contract.
- Identify tests that protect it.
- Do not duplicate its behavior in a new Phase-2 file without a documented reason.

## `docs/PHASE-1-IMPLEMENTATION-REPORT.md`
- Confirm the file still exists at this path or locate its successor.
- Read it before modifying any adjacent Phase-2 integration.
- Identify its Phase-1 behavioral contract.
- Identify tests that protect it.
- Do not duplicate its behavior in a new Phase-2 file without a documented reason.

## `package.json`
- Confirm the file still exists at this path or locate its successor.
- Read it before modifying any adjacent Phase-2 integration.
- Identify its Phase-1 behavioral contract.
- Identify tests that protect it.
- Do not duplicate its behavior in a new Phase-2 file without a documented reason.

## `.env.example`
- Confirm the file still exists at this path or locate its successor.
- Read it before modifying any adjacent Phase-2 integration.
- Identify its Phase-1 behavioral contract.
- Identify tests that protect it.
- Do not duplicate its behavior in a new Phase-2 file without a documented reason.

## `trigger.config.ts`
- Confirm the file still exists at this path or locate its successor.
- Read it before modifying any adjacent Phase-2 integration.
- Identify its Phase-1 behavioral contract.
- Identify tests that protect it.
- Do not duplicate its behavior in a new Phase-2 file without a documented reason.

## `features/workflows/actions.ts`
- Confirm the file still exists at this path or locate its successor.
- Read it before modifying any adjacent Phase-2 integration.
- Identify its Phase-1 behavioral contract.
- Identify tests that protect it.
- Do not duplicate its behavior in a new Phase-2 file without a documented reason.

## `features/workflows/data.ts`
- Confirm the file still exists at this path or locate its successor.
- Read it before modifying any adjacent Phase-2 integration.
- Identify its Phase-1 behavioral contract.
- Identify tests that protect it.
- Do not duplicate its behavior in a new Phase-2 file without a documented reason.

## `features/workflows/tasks/run-workflow.ts`
- Confirm the file still exists at this path or locate its successor.
- Read it before modifying any adjacent Phase-2 integration.
- Identify its Phase-1 behavioral contract.
- Identify tests that protect it.
- Do not duplicate its behavior in a new Phase-2 file without a documented reason.

## `features/workflows/components/workflow-shell.tsx`
- Confirm the file still exists at this path or locate its successor.
- Read it before modifying any adjacent Phase-2 integration.
- Identify its Phase-1 behavioral contract.
- Identify tests that protect it.
- Do not duplicate its behavior in a new Phase-2 file without a documented reason.

## `features/workflows/components/workflow-runs-provider.tsx`
- Confirm the file still exists at this path or locate its successor.
- Read it before modifying any adjacent Phase-2 integration.
- Identify its Phase-1 behavioral contract.
- Identify tests that protect it.
- Do not duplicate its behavior in a new Phase-2 file without a documented reason.

## `features/workflows/components/right-sidebar.tsx`
- Confirm the file still exists at this path or locate its successor.
- Read it before modifying any adjacent Phase-2 integration.
- Identify its Phase-1 behavioral contract.
- Identify tests that protect it.
- Do not duplicate its behavior in a new Phase-2 file without a documented reason.

## `features/workflows/components/canvas.tsx`
- Confirm the file still exists at this path or locate its successor.
- Read it before modifying any adjacent Phase-2 integration.
- Identify its Phase-1 behavioral contract.
- Identify tests that protect it.
- Do not duplicate its behavior in a new Phase-2 file without a documented reason.

## `features/workflows/components/live-browser.tsx`
- Confirm the file still exists at this path or locate its successor.
- Read it before modifying any adjacent Phase-2 integration.
- Identify its Phase-1 behavioral contract.
- Identify tests that protect it.
- Do not duplicate its behavior in a new Phase-2 file without a documented reason.

## `features/workflows/components/console-panel.tsx`
- Confirm the file still exists at this path or locate its successor.
- Read it before modifying any adjacent Phase-2 integration.
- Identify its Phase-1 behavioral contract.
- Identify tests that protect it.
- Do not duplicate its behavior in a new Phase-2 file without a documented reason.

## `features/workflows/components/run-results-dialog.tsx`
- Confirm the file still exists at this path or locate its successor.
- Read it before modifying any adjacent Phase-2 integration.
- Identify its Phase-1 behavioral contract.
- Identify tests that protect it.
- Do not duplicate its behavior in a new Phase-2 file without a documented reason.

## `features/workflows/components/step-output-view.tsx`
- Confirm the file still exists at this path or locate its successor.
- Read it before modifying any adjacent Phase-2 integration.
- Identify its Phase-1 behavioral contract.
- Identify tests that protect it.
- Do not duplicate its behavior in a new Phase-2 file without a documented reason.

## `features/workflows/components/session-replay.tsx`
- Confirm the file still exists at this path or locate its successor.
- Read it before modifying any adjacent Phase-2 integration.
- Identify its Phase-1 behavioral contract.
- Identify tests that protect it.
- Do not duplicate its behavior in a new Phase-2 file without a documented reason.

## `features/workflows/lib/validate-graph.ts`
- Confirm the file still exists at this path or locate its successor.
- Read it before modifying any adjacent Phase-2 integration.
- Identify its Phase-1 behavioral contract.
- Identify tests that protect it.
- Do not duplicate its behavior in a new Phase-2 file without a documented reason.

## `features/workflows/lib/interpolate.ts`
- Confirm the file still exists at this path or locate its successor.
- Read it before modifying any adjacent Phase-2 integration.
- Identify its Phase-1 behavioral contract.
- Identify tests that protect it.
- Do not duplicate its behavior in a new Phase-2 file without a documented reason.

## `features/workflows/lib/planner-types.ts`
- Confirm the file still exists at this path or locate its successor.
- Read it before modifying any adjacent Phase-2 integration.
- Identify its Phase-1 behavioral contract.
- Identify tests that protect it.
- Do not duplicate its behavior in a new Phase-2 file without a documented reason.

## `features/workflows/lib/planner-catalog.ts`
- Confirm the file still exists at this path or locate its successor.
- Read it before modifying any adjacent Phase-2 integration.
- Identify its Phase-1 behavioral contract.
- Identify tests that protect it.
- Do not duplicate its behavior in a new Phase-2 file without a documented reason.

## `features/workflows/lib/planner-service.ts`
- Confirm the file still exists at this path or locate its successor.
- Read it before modifying any adjacent Phase-2 integration.
- Identify its Phase-1 behavioral contract.
- Identify tests that protect it.
- Do not duplicate its behavior in a new Phase-2 file without a documented reason.

## `features/workflows/lib/convert-plan.ts`
- Confirm the file still exists at this path or locate its successor.
- Read it before modifying any adjacent Phase-2 integration.
- Identify its Phase-1 behavioral contract.
- Identify tests that protect it.
- Do not duplicate its behavior in a new Phase-2 file without a documented reason.

## `features/workflows/lib/highlight-element.ts`
- Confirm the file still exists at this path or locate its successor.
- Read it before modifying any adjacent Phase-2 integration.
- Identify its Phase-1 behavioral contract.
- Identify tests that protect it.
- Do not duplicate its behavior in a new Phase-2 file without a documented reason.

## `features/workflows/nodes/node-registry.ts`
- Confirm the file still exists at this path or locate its successor.
- Read it before modifying any adjacent Phase-2 integration.
- Identify its Phase-1 behavioral contract.
- Identify tests that protect it.
- Do not duplicate its behavior in a new Phase-2 file without a documented reason.

## `features/workflows/nodes/node-executors.ts`
- Confirm the file still exists at this path or locate its successor.
- Read it before modifying any adjacent Phase-2 integration.
- Identify its Phase-1 behavioral contract.
- Identify tests that protect it.
- Do not duplicate its behavior in a new Phase-2 file without a documented reason.

## `features/workflows/nodes/open-url.ts`
- Confirm the file still exists at this path or locate its successor.
- Read it before modifying any adjacent Phase-2 integration.
- Identify its Phase-1 behavioral contract.
- Identify tests that protect it.
- Do not duplicate its behavior in a new Phase-2 file without a documented reason.

## `features/workflows/nodes/act.ts`
- Confirm the file still exists at this path or locate its successor.
- Read it before modifying any adjacent Phase-2 integration.
- Identify its Phase-1 behavioral contract.
- Identify tests that protect it.
- Do not duplicate its behavior in a new Phase-2 file without a documented reason.

## `features/workflows/nodes/extract.ts`
- Confirm the file still exists at this path or locate its successor.
- Read it before modifying any adjacent Phase-2 integration.
- Identify its Phase-1 behavioral contract.
- Identify tests that protect it.
- Do not duplicate its behavior in a new Phase-2 file without a documented reason.

## `features/workflows/nodes/observe.ts`
- Confirm the file still exists at this path or locate its successor.
- Read it before modifying any adjacent Phase-2 integration.
- Identify its Phase-1 behavioral contract.
- Identify tests that protect it.
- Do not duplicate its behavior in a new Phase-2 file without a documented reason.

## `features/workflows/nodes/agent.ts`
- Confirm the file still exists at this path or locate its successor.
- Read it before modifying any adjacent Phase-2 integration.
- Identify its Phase-1 behavioral contract.
- Identify tests that protect it.
- Do not duplicate its behavior in a new Phase-2 file without a documented reason.

## `features/workflows/nodes/send-email.ts`
- Confirm the file still exists at this path or locate its successor.
- Read it before modifying any adjacent Phase-2 integration.
- Identify its Phase-1 behavioral contract.
- Identify tests that protect it.
- Do not duplicate its behavior in a new Phase-2 file without a documented reason.

## `features/workflows/lib/convert-plan.test.ts`
- Confirm the file still exists at this path or locate its successor.
- Read it before modifying any adjacent Phase-2 integration.
- Identify its Phase-1 behavioral contract.
- Identify tests that protect it.
- Do not duplicate its behavior in a new Phase-2 file without a documented reason.

## `features/workflows/lib/integration.test.ts`
- Confirm the file still exists at this path or locate its successor.
- Read it before modifying any adjacent Phase-2 integration.
- Identify its Phase-1 behavioral contract.
- Identify tests that protect it.
- Do not duplicate its behavior in a new Phase-2 file without a documented reason.

## `features/workflows/lib/lifecycle.test.ts`
- Confirm the file still exists at this path or locate its successor.
- Read it before modifying any adjacent Phase-2 integration.
- Identify its Phase-1 behavioral contract.
- Identify tests that protect it.
- Do not duplicate its behavior in a new Phase-2 file without a documented reason.

## `lib/db/schema.ts`
- Confirm the file still exists at this path or locate its successor.
- Read it before modifying any adjacent Phase-2 integration.
- Identify its Phase-1 behavioral contract.
- Identify tests that protect it.
- Do not duplicate its behavior in a new Phase-2 file without a documented reason.

## `lib/db/index.ts`
- Confirm the file still exists at this path or locate its successor.
- Read it before modifying any adjacent Phase-2 integration.
- Identify its Phase-1 behavioral contract.
- Identify tests that protect it.
- Do not duplicate its behavior in a new Phase-2 file without a documented reason.

## `lib/auth.ts`
- Confirm the file still exists at this path or locate its successor.
- Read it before modifying any adjacent Phase-2 integration.
- Identify its Phase-1 behavioral contract.
- Identify tests that protect it.
- Do not duplicate its behavior in a new Phase-2 file without a documented reason.

## `lib/browserbase.ts`
- Confirm the file still exists at this path or locate its successor.
- Read it before modifying any adjacent Phase-2 integration.
- Identify its Phase-1 behavioral contract.
- Identify tests that protect it.
- Do not duplicate its behavior in a new Phase-2 file without a documented reason.

## `lib/liveblocks.ts`
- Confirm the file still exists at this path or locate its successor.
- Read it before modifying any adjacent Phase-2 integration.
- Identify its Phase-1 behavioral contract.
- Identify tests that protect it.
- Do not duplicate its behavior in a new Phase-2 file without a documented reason.

## `lib/resend.ts`
- Confirm the file still exists at this path or locate its successor.
- Read it before modifying any adjacent Phase-2 integration.
- Identify its Phase-1 behavioral contract.
- Identify tests that protect it.
- Do not duplicate its behavior in a new Phase-2 file without a documented reason.

## `app/api/live-view/[sessionId]/route.ts`
- Confirm the file still exists at this path or locate its successor.
- Read it before modifying any adjacent Phase-2 integration.
- Identify its Phase-1 behavioral contract.
- Identify tests that protect it.
- Do not duplicate its behavior in a new Phase-2 file without a documented reason.

## `app/api/live-view/[sessionId]/connected/route.ts`
- Confirm the file still exists at this path or locate its successor.
- Read it before modifying any adjacent Phase-2 integration.
- Identify its Phase-1 behavioral contract.
- Identify tests that protect it.
- Do not duplicate its behavior in a new Phase-2 file without a documented reason.

## `app/api/replays/[sessionId]/route.ts`
- Confirm the file still exists at this path or locate its successor.
- Read it before modifying any adjacent Phase-2 integration.
- Identify its Phase-1 behavioral contract.
- Identify tests that protect it.
- Do not duplicate its behavior in a new Phase-2 file without a documented reason.

## `app/api/runs/[runId]/screenshot/route.ts`
- Confirm the file still exists at this path or locate its successor.
- Read it before modifying any adjacent Phase-2 integration.
- Identify its Phase-1 behavioral contract.
- Identify tests that protect it.
- Do not duplicate its behavior in a new Phase-2 file without a documented reason.

# APPENDIX X — Final release checklist
- [ ] R001 — Working tree is clean after the final milestone commit.
- [ ] R002 — Phase-1 baseline SHA is documented.
- [ ] R003 — Every milestone has a progress entry and commit SHA.
- [ ] R004 — No milestone is marked DONE with failing required tests.
- [ ] R005 — AI planner still works.
- [ ] R006 — Build manually still works.
- [ ] R007 — Generated workflow preview still works.
- [ ] R008 — Manual edit before Run still works.
- [ ] R009 — Legacy engine Run still works.
- [ ] R010 — Legacy Stop still works.
- [ ] R011 — Legacy live view still works with live credentials.
- [ ] R012 — Legacy result dialog still works.
- [ ] R013 — Legacy screenshot still works.
- [ ] R014 — Legacy replay still works.
- [ ] R015 — Legacy rerun gets fresh session.
- [ ] R016 — Evo engine can submit synthetic run.
- [ ] R017 — Evo engine can run distributed synthetic DAG.
- [ ] R018 — Evo engine can execute existing non-browser node path safely.
- [ ] R019 — Evo browser path preserves one-session affinity.
- [ ] R020 — Evo live view is authorized.
- [ ] R021 — Evo Stop propagates end-to-end.
- [ ] R022 — Evo run result persists.
- [ ] R023 — Evo screenshot/replay access is scoped correctly.
- [ ] R024 — Immutable workflow version is referenced by each Evo run.
- [ ] R025 — Rerun after edit references a new/current version as intended.
- [ ] R026 — C++ scheduler passes unit tests.
- [ ] R027 — Ready queue passes stress tests.
- [ ] R028 — Thread pool passes shutdown tests.
- [ ] R029 — Concurrent scheduler matches sequential reference logical output.
- [ ] R030 — ASan/UBSan are clean where supported.
- [ ] R031 — TSan is clean where supported or limitation is documented.
- [ ] R032 — Redis task transport passes duplicate/redelivery tests.
- [ ] R033 — Worker leases expire and recover in controlled tests.
- [ ] R034 — Slow renewing worker is not misclassified as dead.
- [ ] R035 — Retry backoff is bounded and jittered.
- [ ] R036 — Permanent errors do not retry blindly.
- [ ] R037 — Dead-letter/exhaustion state is visible.
- [ ] R038 — Idempotency duplicate logical commit is prevented.
- [ ] R039 — External side-effect ambiguity is documented accurately.
- [ ] R040 — Worker crash recovery evidence exists.
- [ ] R041 — Scheduler restart recovery evidence exists.
- [ ] R042 — Tenant quota tests exist.
- [ ] R043 — Backpressure tests exist.
- [ ] R044 — Fairness/starvation tests exist.
- [ ] R045 — No cross-org run/event access is possible in tested routes.
- [ ] R046 — Service-to-service secret remains server-side.
- [ ] R047 — No secret appears in Git diff/history for Phase-2 commits.
- [ ] R048 — CI has Phase-1 and C++ gates.
- [ ] R049 — CI distributed test uses local/mocked services, not production secrets.
- [ ] R050 — Benchmark runner records commit SHA and dirty state.
- [ ] R051 — Benchmark runner records hardware/compiler/build metadata.
- [ ] R052 — Final scheduler benchmarks use Release build.
- [ ] R053 — Raw benchmark samples are preserved.
- [ ] R054 — Benchmark summaries are generated from raw data, not hand-edited.
- [ ] R055 — Fault injection timestamps are preserved.
- [ ] R056 — Cancellation latency is split into scheduler vs actual worker/resource stop where measured.
- [ ] R057 — Fairness report includes per-tenant wait distribution.
- [ ] R058 — Browser end-to-end results are clearly separated from synthetic scheduler results.
- [ ] R059 — README contains no placeholder X/Y/Z metrics.
- [ ] R060 — README contains no unimplemented future feature in current-feature list.
- [ ] R061 — RESUME_EVIDENCE.md classifies claims GREEN/YELLOW/RED.
- [ ] R062 — Every GREEN quantified claim links raw evidence.
- [ ] R063 — No YELLOW/RED quantified claim is copied into candidate resume bullets.
- [ ] R064 — Final build instructions work from a clean checkout with documented prerequisites.
- [ ] R065 — Human intervention/setup steps are explicit.
- [ ] R066 — No Docker volume/build cache is committed.
- [ ] R067 — No generated vcpkg dependency tree is committed.
- [ ] R068 — No benchmark result directory contains secrets or personal local paths that should be redacted.
- [ ] R069 — Final architecture diagram matches actual implementation.
- [ ] R070 — Failure model matches actual retry/recovery behavior.
- [ ] R071 — Known limitations are present and specific.
- [ ] R072 — Final release can be explained end-to-end in an interview without relying on vague AI-generated wording.

# APPENDIX Y — Per-milestone verification ledger template

For each milestone, populate a copy of the following ledger in `docs/phase2/progress/MNN.md` or equivalent.

The ledger is intentionally exhaustive so the agent cannot mark a milestone complete based only on compilation.
## Ledger for Milestone 01
- [ ] Milestone number/title:
- [ ] Start SHA:
- [ ] End SHA:
- [ ] Date/time:
- [ ] Host OS/architecture:
- [ ] Working-tree cleanliness before start:
- [ ] Repository instructions read:
- [ ] Source files inspected:
- [ ] Tests inspected:
- [ ] Third-party docs consulted:
- [ ] Files created:
- [ ] Files modified:
- [ ] Files deleted (normally none):
- [ ] Database migration generated:
- [ ] Database migration applied locally:
- [ ] Remote database changed (must normally be no unless approved):
- [ ] New environment variables:
- [ ] Human setup required:
- [ ] C++ configure command:
- [ ] C++ build command:
- [ ] CTest command/result:
- [ ] Sanitizer command/result:
- [ ] Node test command/result:
- [ ] Typecheck result:
- [ ] Lint result:
- [ ] Build result:
- [ ] Distributed integration result:
- [ ] Security-negative test result:
- [ ] Fault-injection result:
- [ ] Benchmark command:
- [ ] Benchmark manifest path:
- [ ] Raw benchmark path:
- [ ] Summary benchmark path:
- [ ] Metric claims created (normally none until final):
- [ ] Phase-1 behaviors touched:
- [ ] Phase-1 regressions observed:
- [ ] Known limitations:
- [ ] Diff reviewed:
- [ ] git diff --check result:
- [ ] Secrets checked:
- [ ] Commit subject:
- [ ] Commit SHA:
- [ ] Next milestone readiness:

## Ledger for Milestone 02
- [ ] Milestone number/title:
- [ ] Start SHA:
- [ ] End SHA:
- [ ] Date/time:
- [ ] Host OS/architecture:
- [ ] Working-tree cleanliness before start:
- [ ] Repository instructions read:
- [ ] Source files inspected:
- [ ] Tests inspected:
- [ ] Third-party docs consulted:
- [ ] Files created:
- [ ] Files modified:
- [ ] Files deleted (normally none):
- [ ] Database migration generated:
- [ ] Database migration applied locally:
- [ ] Remote database changed (must normally be no unless approved):
- [ ] New environment variables:
- [ ] Human setup required:
- [ ] C++ configure command:
- [ ] C++ build command:
- [ ] CTest command/result:
- [ ] Sanitizer command/result:
- [ ] Node test command/result:
- [ ] Typecheck result:
- [ ] Lint result:
- [ ] Build result:
- [ ] Distributed integration result:
- [ ] Security-negative test result:
- [ ] Fault-injection result:
- [ ] Benchmark command:
- [ ] Benchmark manifest path:
- [ ] Raw benchmark path:
- [ ] Summary benchmark path:
- [ ] Metric claims created (normally none until final):
- [ ] Phase-1 behaviors touched:
- [ ] Phase-1 regressions observed:
- [ ] Known limitations:
- [ ] Diff reviewed:
- [ ] git diff --check result:
- [ ] Secrets checked:
- [ ] Commit subject:
- [ ] Commit SHA:
- [ ] Next milestone readiness:

## Ledger for Milestone 03
- [ ] Milestone number/title:
- [ ] Start SHA:
- [ ] End SHA:
- [ ] Date/time:
- [ ] Host OS/architecture:
- [ ] Working-tree cleanliness before start:
- [ ] Repository instructions read:
- [ ] Source files inspected:
- [ ] Tests inspected:
- [ ] Third-party docs consulted:
- [ ] Files created:
- [ ] Files modified:
- [ ] Files deleted (normally none):
- [ ] Database migration generated:
- [ ] Database migration applied locally:
- [ ] Remote database changed (must normally be no unless approved):
- [ ] New environment variables:
- [ ] Human setup required:
- [ ] C++ configure command:
- [ ] C++ build command:
- [ ] CTest command/result:
- [ ] Sanitizer command/result:
- [ ] Node test command/result:
- [ ] Typecheck result:
- [ ] Lint result:
- [ ] Build result:
- [ ] Distributed integration result:
- [ ] Security-negative test result:
- [ ] Fault-injection result:
- [ ] Benchmark command:
- [ ] Benchmark manifest path:
- [ ] Raw benchmark path:
- [ ] Summary benchmark path:
- [ ] Metric claims created (normally none until final):
- [ ] Phase-1 behaviors touched:
- [ ] Phase-1 regressions observed:
- [ ] Known limitations:
- [ ] Diff reviewed:
- [ ] git diff --check result:
- [ ] Secrets checked:
- [ ] Commit subject:
- [ ] Commit SHA:
- [ ] Next milestone readiness:

## Ledger for Milestone 04
- [ ] Milestone number/title:
- [ ] Start SHA:
- [ ] End SHA:
- [ ] Date/time:
- [ ] Host OS/architecture:
- [ ] Working-tree cleanliness before start:
- [ ] Repository instructions read:
- [ ] Source files inspected:
- [ ] Tests inspected:
- [ ] Third-party docs consulted:
- [ ] Files created:
- [ ] Files modified:
- [ ] Files deleted (normally none):
- [ ] Database migration generated:
- [ ] Database migration applied locally:
- [ ] Remote database changed (must normally be no unless approved):
- [ ] New environment variables:
- [ ] Human setup required:
- [ ] C++ configure command:
- [ ] C++ build command:
- [ ] CTest command/result:
- [ ] Sanitizer command/result:
- [ ] Node test command/result:
- [ ] Typecheck result:
- [ ] Lint result:
- [ ] Build result:
- [ ] Distributed integration result:
- [ ] Security-negative test result:
- [ ] Fault-injection result:
- [ ] Benchmark command:
- [ ] Benchmark manifest path:
- [ ] Raw benchmark path:
- [ ] Summary benchmark path:
- [ ] Metric claims created (normally none until final):
- [ ] Phase-1 behaviors touched:
- [ ] Phase-1 regressions observed:
- [ ] Known limitations:
- [ ] Diff reviewed:
- [ ] git diff --check result:
- [ ] Secrets checked:
- [ ] Commit subject:
- [ ] Commit SHA:
- [ ] Next milestone readiness:

## Ledger for Milestone 05
- [ ] Milestone number/title:
- [ ] Start SHA:
- [ ] End SHA:
- [ ] Date/time:
- [ ] Host OS/architecture:
- [ ] Working-tree cleanliness before start:
- [ ] Repository instructions read:
- [ ] Source files inspected:
- [ ] Tests inspected:
- [ ] Third-party docs consulted:
- [ ] Files created:
- [ ] Files modified:
- [ ] Files deleted (normally none):
- [ ] Database migration generated:
- [ ] Database migration applied locally:
- [ ] Remote database changed (must normally be no unless approved):
- [ ] New environment variables:
- [ ] Human setup required:
- [ ] C++ configure command:
- [ ] C++ build command:
- [ ] CTest command/result:
- [ ] Sanitizer command/result:
- [ ] Node test command/result:
- [ ] Typecheck result:
- [ ] Lint result:
- [ ] Build result:
- [ ] Distributed integration result:
- [ ] Security-negative test result:
- [ ] Fault-injection result:
- [ ] Benchmark command:
- [ ] Benchmark manifest path:
- [ ] Raw benchmark path:
- [ ] Summary benchmark path:
- [ ] Metric claims created (normally none until final):
- [ ] Phase-1 behaviors touched:
- [ ] Phase-1 regressions observed:
- [ ] Known limitations:
- [ ] Diff reviewed:
- [ ] git diff --check result:
- [ ] Secrets checked:
- [ ] Commit subject:
- [ ] Commit SHA:
- [ ] Next milestone readiness:

## Ledger for Milestone 06
- [ ] Milestone number/title:
- [ ] Start SHA:
- [ ] End SHA:
- [ ] Date/time:
- [ ] Host OS/architecture:
- [ ] Working-tree cleanliness before start:
- [ ] Repository instructions read:
- [ ] Source files inspected:
- [ ] Tests inspected:
- [ ] Third-party docs consulted:
- [ ] Files created:
- [ ] Files modified:
- [ ] Files deleted (normally none):
- [ ] Database migration generated:
- [ ] Database migration applied locally:
- [ ] Remote database changed (must normally be no unless approved):
- [ ] New environment variables:
- [ ] Human setup required:
- [ ] C++ configure command:
- [ ] C++ build command:
- [ ] CTest command/result:
- [ ] Sanitizer command/result:
- [ ] Node test command/result:
- [ ] Typecheck result:
- [ ] Lint result:
- [ ] Build result:
- [ ] Distributed integration result:
- [ ] Security-negative test result:
- [ ] Fault-injection result:
- [ ] Benchmark command:
- [ ] Benchmark manifest path:
- [ ] Raw benchmark path:
- [ ] Summary benchmark path:
- [ ] Metric claims created (normally none until final):
- [ ] Phase-1 behaviors touched:
- [ ] Phase-1 regressions observed:
- [ ] Known limitations:
- [ ] Diff reviewed:
- [ ] git diff --check result:
- [ ] Secrets checked:
- [ ] Commit subject:
- [ ] Commit SHA:
- [ ] Next milestone readiness:

## Ledger for Milestone 07
- [ ] Milestone number/title:
- [ ] Start SHA:
- [ ] End SHA:
- [ ] Date/time:
- [ ] Host OS/architecture:
- [ ] Working-tree cleanliness before start:
- [ ] Repository instructions read:
- [ ] Source files inspected:
- [ ] Tests inspected:
- [ ] Third-party docs consulted:
- [ ] Files created:
- [ ] Files modified:
- [ ] Files deleted (normally none):
- [ ] Database migration generated:
- [ ] Database migration applied locally:
- [ ] Remote database changed (must normally be no unless approved):
- [ ] New environment variables:
- [ ] Human setup required:
- [ ] C++ configure command:
- [ ] C++ build command:
- [ ] CTest command/result:
- [ ] Sanitizer command/result:
- [ ] Node test command/result:
- [ ] Typecheck result:
- [ ] Lint result:
- [ ] Build result:
- [ ] Distributed integration result:
- [ ] Security-negative test result:
- [ ] Fault-injection result:
- [ ] Benchmark command:
- [ ] Benchmark manifest path:
- [ ] Raw benchmark path:
- [ ] Summary benchmark path:
- [ ] Metric claims created (normally none until final):
- [ ] Phase-1 behaviors touched:
- [ ] Phase-1 regressions observed:
- [ ] Known limitations:
- [ ] Diff reviewed:
- [ ] git diff --check result:
- [ ] Secrets checked:
- [ ] Commit subject:
- [ ] Commit SHA:
- [ ] Next milestone readiness:

## Ledger for Milestone 08
- [ ] Milestone number/title:
- [ ] Start SHA:
- [ ] End SHA:
- [ ] Date/time:
- [ ] Host OS/architecture:
- [ ] Working-tree cleanliness before start:
- [ ] Repository instructions read:
- [ ] Source files inspected:
- [ ] Tests inspected:
- [ ] Third-party docs consulted:
- [ ] Files created:
- [ ] Files modified:
- [ ] Files deleted (normally none):
- [ ] Database migration generated:
- [ ] Database migration applied locally:
- [ ] Remote database changed (must normally be no unless approved):
- [ ] New environment variables:
- [ ] Human setup required:
- [ ] C++ configure command:
- [ ] C++ build command:
- [ ] CTest command/result:
- [ ] Sanitizer command/result:
- [ ] Node test command/result:
- [ ] Typecheck result:
- [ ] Lint result:
- [ ] Build result:
- [ ] Distributed integration result:
- [ ] Security-negative test result:
- [ ] Fault-injection result:
- [ ] Benchmark command:
- [ ] Benchmark manifest path:
- [ ] Raw benchmark path:
- [ ] Summary benchmark path:
- [ ] Metric claims created (normally none until final):
- [ ] Phase-1 behaviors touched:
- [ ] Phase-1 regressions observed:
- [ ] Known limitations:
- [ ] Diff reviewed:
- [ ] git diff --check result:
- [ ] Secrets checked:
- [ ] Commit subject:
- [ ] Commit SHA:
- [ ] Next milestone readiness:

## Ledger for Milestone 09
- [ ] Milestone number/title:
- [ ] Start SHA:
- [ ] End SHA:
- [ ] Date/time:
- [ ] Host OS/architecture:
- [ ] Working-tree cleanliness before start:
- [ ] Repository instructions read:
- [ ] Source files inspected:
- [ ] Tests inspected:
- [ ] Third-party docs consulted:
- [ ] Files created:
- [ ] Files modified:
- [ ] Files deleted (normally none):
- [ ] Database migration generated:
- [ ] Database migration applied locally:
- [ ] Remote database changed (must normally be no unless approved):
- [ ] New environment variables:
- [ ] Human setup required:
- [ ] C++ configure command:
- [ ] C++ build command:
- [ ] CTest command/result:
- [ ] Sanitizer command/result:
- [ ] Node test command/result:
- [ ] Typecheck result:
- [ ] Lint result:
- [ ] Build result:
- [ ] Distributed integration result:
- [ ] Security-negative test result:
- [ ] Fault-injection result:
- [ ] Benchmark command:
- [ ] Benchmark manifest path:
- [ ] Raw benchmark path:
- [ ] Summary benchmark path:
- [ ] Metric claims created (normally none until final):
- [ ] Phase-1 behaviors touched:
- [ ] Phase-1 regressions observed:
- [ ] Known limitations:
- [ ] Diff reviewed:
- [ ] git diff --check result:
- [ ] Secrets checked:
- [ ] Commit subject:
- [ ] Commit SHA:
- [ ] Next milestone readiness:

## Ledger for Milestone 10
- [ ] Milestone number/title:
- [ ] Start SHA:
- [ ] End SHA:
- [ ] Date/time:
- [ ] Host OS/architecture:
- [ ] Working-tree cleanliness before start:
- [ ] Repository instructions read:
- [ ] Source files inspected:
- [ ] Tests inspected:
- [ ] Third-party docs consulted:
- [ ] Files created:
- [ ] Files modified:
- [ ] Files deleted (normally none):
- [ ] Database migration generated:
- [ ] Database migration applied locally:
- [ ] Remote database changed (must normally be no unless approved):
- [ ] New environment variables:
- [ ] Human setup required:
- [ ] C++ configure command:
- [ ] C++ build command:
- [ ] CTest command/result:
- [ ] Sanitizer command/result:
- [ ] Node test command/result:
- [ ] Typecheck result:
- [ ] Lint result:
- [ ] Build result:
- [ ] Distributed integration result:
- [ ] Security-negative test result:
- [ ] Fault-injection result:
- [ ] Benchmark command:
- [ ] Benchmark manifest path:
- [ ] Raw benchmark path:
- [ ] Summary benchmark path:
- [ ] Metric claims created (normally none until final):
- [ ] Phase-1 behaviors touched:
- [ ] Phase-1 regressions observed:
- [ ] Known limitations:
- [ ] Diff reviewed:
- [ ] git diff --check result:
- [ ] Secrets checked:
- [ ] Commit subject:
- [ ] Commit SHA:
- [ ] Next milestone readiness:

## Ledger for Milestone 11
- [ ] Milestone number/title:
- [ ] Start SHA:
- [ ] End SHA:
- [ ] Date/time:
- [ ] Host OS/architecture:
- [ ] Working-tree cleanliness before start:
- [ ] Repository instructions read:
- [ ] Source files inspected:
- [ ] Tests inspected:
- [ ] Third-party docs consulted:
- [ ] Files created:
- [ ] Files modified:
- [ ] Files deleted (normally none):
- [ ] Database migration generated:
- [ ] Database migration applied locally:
- [ ] Remote database changed (must normally be no unless approved):
- [ ] New environment variables:
- [ ] Human setup required:
- [ ] C++ configure command:
- [ ] C++ build command:
- [ ] CTest command/result:
- [ ] Sanitizer command/result:
- [ ] Node test command/result:
- [ ] Typecheck result:
- [ ] Lint result:
- [ ] Build result:
- [ ] Distributed integration result:
- [ ] Security-negative test result:
- [ ] Fault-injection result:
- [ ] Benchmark command:
- [ ] Benchmark manifest path:
- [ ] Raw benchmark path:
- [ ] Summary benchmark path:
- [ ] Metric claims created (normally none until final):
- [ ] Phase-1 behaviors touched:
- [ ] Phase-1 regressions observed:
- [ ] Known limitations:
- [ ] Diff reviewed:
- [ ] git diff --check result:
- [ ] Secrets checked:
- [ ] Commit subject:
- [ ] Commit SHA:
- [ ] Next milestone readiness:

## Ledger for Milestone 12
- [ ] Milestone number/title:
- [ ] Start SHA:
- [ ] End SHA:
- [ ] Date/time:
- [ ] Host OS/architecture:
- [ ] Working-tree cleanliness before start:
- [ ] Repository instructions read:
- [ ] Source files inspected:
- [ ] Tests inspected:
- [ ] Third-party docs consulted:
- [ ] Files created:
- [ ] Files modified:
- [ ] Files deleted (normally none):
- [ ] Database migration generated:
- [ ] Database migration applied locally:
- [ ] Remote database changed (must normally be no unless approved):
- [ ] New environment variables:
- [ ] Human setup required:
- [ ] C++ configure command:
- [ ] C++ build command:
- [ ] CTest command/result:
- [ ] Sanitizer command/result:
- [ ] Node test command/result:
- [ ] Typecheck result:
- [ ] Lint result:
- [ ] Build result:
- [ ] Distributed integration result:
- [ ] Security-negative test result:
- [ ] Fault-injection result:
- [ ] Benchmark command:
- [ ] Benchmark manifest path:
- [ ] Raw benchmark path:
- [ ] Summary benchmark path:
- [ ] Metric claims created (normally none until final):
- [ ] Phase-1 behaviors touched:
- [ ] Phase-1 regressions observed:
- [ ] Known limitations:
- [ ] Diff reviewed:
- [ ] git diff --check result:
- [ ] Secrets checked:
- [ ] Commit subject:
- [ ] Commit SHA:
- [ ] Next milestone readiness:

## Ledger for Milestone 13
- [ ] Milestone number/title:
- [ ] Start SHA:
- [ ] End SHA:
- [ ] Date/time:
- [ ] Host OS/architecture:
- [ ] Working-tree cleanliness before start:
- [ ] Repository instructions read:
- [ ] Source files inspected:
- [ ] Tests inspected:
- [ ] Third-party docs consulted:
- [ ] Files created:
- [ ] Files modified:
- [ ] Files deleted (normally none):
- [ ] Database migration generated:
- [ ] Database migration applied locally:
- [ ] Remote database changed (must normally be no unless approved):
- [ ] New environment variables:
- [ ] Human setup required:
- [ ] C++ configure command:
- [ ] C++ build command:
- [ ] CTest command/result:
- [ ] Sanitizer command/result:
- [ ] Node test command/result:
- [ ] Typecheck result:
- [ ] Lint result:
- [ ] Build result:
- [ ] Distributed integration result:
- [ ] Security-negative test result:
- [ ] Fault-injection result:
- [ ] Benchmark command:
- [ ] Benchmark manifest path:
- [ ] Raw benchmark path:
- [ ] Summary benchmark path:
- [ ] Metric claims created (normally none until final):
- [ ] Phase-1 behaviors touched:
- [ ] Phase-1 regressions observed:
- [ ] Known limitations:
- [ ] Diff reviewed:
- [ ] git diff --check result:
- [ ] Secrets checked:
- [ ] Commit subject:
- [ ] Commit SHA:
- [ ] Next milestone readiness:

## Ledger for Milestone 14
- [ ] Milestone number/title:
- [ ] Start SHA:
- [ ] End SHA:
- [ ] Date/time:
- [ ] Host OS/architecture:
- [ ] Working-tree cleanliness before start:
- [ ] Repository instructions read:
- [ ] Source files inspected:
- [ ] Tests inspected:
- [ ] Third-party docs consulted:
- [ ] Files created:
- [ ] Files modified:
- [ ] Files deleted (normally none):
- [ ] Database migration generated:
- [ ] Database migration applied locally:
- [ ] Remote database changed (must normally be no unless approved):
- [ ] New environment variables:
- [ ] Human setup required:
- [ ] C++ configure command:
- [ ] C++ build command:
- [ ] CTest command/result:
- [ ] Sanitizer command/result:
- [ ] Node test command/result:
- [ ] Typecheck result:
- [ ] Lint result:
- [ ] Build result:
- [ ] Distributed integration result:
- [ ] Security-negative test result:
- [ ] Fault-injection result:
- [ ] Benchmark command:
- [ ] Benchmark manifest path:
- [ ] Raw benchmark path:
- [ ] Summary benchmark path:
- [ ] Metric claims created (normally none until final):
- [ ] Phase-1 behaviors touched:
- [ ] Phase-1 regressions observed:
- [ ] Known limitations:
- [ ] Diff reviewed:
- [ ] git diff --check result:
- [ ] Secrets checked:
- [ ] Commit subject:
- [ ] Commit SHA:
- [ ] Next milestone readiness:

## Ledger for Milestone 15
- [ ] Milestone number/title:
- [ ] Start SHA:
- [ ] End SHA:
- [ ] Date/time:
- [ ] Host OS/architecture:
- [ ] Working-tree cleanliness before start:
- [ ] Repository instructions read:
- [ ] Source files inspected:
- [ ] Tests inspected:
- [ ] Third-party docs consulted:
- [ ] Files created:
- [ ] Files modified:
- [ ] Files deleted (normally none):
- [ ] Database migration generated:
- [ ] Database migration applied locally:
- [ ] Remote database changed (must normally be no unless approved):
- [ ] New environment variables:
- [ ] Human setup required:
- [ ] C++ configure command:
- [ ] C++ build command:
- [ ] CTest command/result:
- [ ] Sanitizer command/result:
- [ ] Node test command/result:
- [ ] Typecheck result:
- [ ] Lint result:
- [ ] Build result:
- [ ] Distributed integration result:
- [ ] Security-negative test result:
- [ ] Fault-injection result:
- [ ] Benchmark command:
- [ ] Benchmark manifest path:
- [ ] Raw benchmark path:
- [ ] Summary benchmark path:
- [ ] Metric claims created (normally none until final):
- [ ] Phase-1 behaviors touched:
- [ ] Phase-1 regressions observed:
- [ ] Known limitations:
- [ ] Diff reviewed:
- [ ] git diff --check result:
- [ ] Secrets checked:
- [ ] Commit subject:
- [ ] Commit SHA:
- [ ] Next milestone readiness:

## Ledger for Milestone 16
- [ ] Milestone number/title:
- [ ] Start SHA:
- [ ] End SHA:
- [ ] Date/time:
- [ ] Host OS/architecture:
- [ ] Working-tree cleanliness before start:
- [ ] Repository instructions read:
- [ ] Source files inspected:
- [ ] Tests inspected:
- [ ] Third-party docs consulted:
- [ ] Files created:
- [ ] Files modified:
- [ ] Files deleted (normally none):
- [ ] Database migration generated:
- [ ] Database migration applied locally:
- [ ] Remote database changed (must normally be no unless approved):
- [ ] New environment variables:
- [ ] Human setup required:
- [ ] C++ configure command:
- [ ] C++ build command:
- [ ] CTest command/result:
- [ ] Sanitizer command/result:
- [ ] Node test command/result:
- [ ] Typecheck result:
- [ ] Lint result:
- [ ] Build result:
- [ ] Distributed integration result:
- [ ] Security-negative test result:
- [ ] Fault-injection result:
- [ ] Benchmark command:
- [ ] Benchmark manifest path:
- [ ] Raw benchmark path:
- [ ] Summary benchmark path:
- [ ] Metric claims created (normally none until final):
- [ ] Phase-1 behaviors touched:
- [ ] Phase-1 regressions observed:
- [ ] Known limitations:
- [ ] Diff reviewed:
- [ ] git diff --check result:
- [ ] Secrets checked:
- [ ] Commit subject:
- [ ] Commit SHA:
- [ ] Next milestone readiness:

## Ledger for Milestone 17
- [ ] Milestone number/title:
- [ ] Start SHA:
- [ ] End SHA:
- [ ] Date/time:
- [ ] Host OS/architecture:
- [ ] Working-tree cleanliness before start:
- [ ] Repository instructions read:
- [ ] Source files inspected:
- [ ] Tests inspected:
- [ ] Third-party docs consulted:
- [ ] Files created:
- [ ] Files modified:
- [ ] Files deleted (normally none):
- [ ] Database migration generated:
- [ ] Database migration applied locally:
- [ ] Remote database changed (must normally be no unless approved):
- [ ] New environment variables:
- [ ] Human setup required:
- [ ] C++ configure command:
- [ ] C++ build command:
- [ ] CTest command/result:
- [ ] Sanitizer command/result:
- [ ] Node test command/result:
- [ ] Typecheck result:
- [ ] Lint result:
- [ ] Build result:
- [ ] Distributed integration result:
- [ ] Security-negative test result:
- [ ] Fault-injection result:
- [ ] Benchmark command:
- [ ] Benchmark manifest path:
- [ ] Raw benchmark path:
- [ ] Summary benchmark path:
- [ ] Metric claims created (normally none until final):
- [ ] Phase-1 behaviors touched:
- [ ] Phase-1 regressions observed:
- [ ] Known limitations:
- [ ] Diff reviewed:
- [ ] git diff --check result:
- [ ] Secrets checked:
- [ ] Commit subject:
- [ ] Commit SHA:
- [ ] Next milestone readiness:

## Ledger for Milestone 18
- [ ] Milestone number/title:
- [ ] Start SHA:
- [ ] End SHA:
- [ ] Date/time:
- [ ] Host OS/architecture:
- [ ] Working-tree cleanliness before start:
- [ ] Repository instructions read:
- [ ] Source files inspected:
- [ ] Tests inspected:
- [ ] Third-party docs consulted:
- [ ] Files created:
- [ ] Files modified:
- [ ] Files deleted (normally none):
- [ ] Database migration generated:
- [ ] Database migration applied locally:
- [ ] Remote database changed (must normally be no unless approved):
- [ ] New environment variables:
- [ ] Human setup required:
- [ ] C++ configure command:
- [ ] C++ build command:
- [ ] CTest command/result:
- [ ] Sanitizer command/result:
- [ ] Node test command/result:
- [ ] Typecheck result:
- [ ] Lint result:
- [ ] Build result:
- [ ] Distributed integration result:
- [ ] Security-negative test result:
- [ ] Fault-injection result:
- [ ] Benchmark command:
- [ ] Benchmark manifest path:
- [ ] Raw benchmark path:
- [ ] Summary benchmark path:
- [ ] Metric claims created (normally none until final):
- [ ] Phase-1 behaviors touched:
- [ ] Phase-1 regressions observed:
- [ ] Known limitations:
- [ ] Diff reviewed:
- [ ] git diff --check result:
- [ ] Secrets checked:
- [ ] Commit subject:
- [ ] Commit SHA:
- [ ] Next milestone readiness:

## Ledger for Milestone 19
- [ ] Milestone number/title:
- [ ] Start SHA:
- [ ] End SHA:
- [ ] Date/time:
- [ ] Host OS/architecture:
- [ ] Working-tree cleanliness before start:
- [ ] Repository instructions read:
- [ ] Source files inspected:
- [ ] Tests inspected:
- [ ] Third-party docs consulted:
- [ ] Files created:
- [ ] Files modified:
- [ ] Files deleted (normally none):
- [ ] Database migration generated:
- [ ] Database migration applied locally:
- [ ] Remote database changed (must normally be no unless approved):
- [ ] New environment variables:
- [ ] Human setup required:
- [ ] C++ configure command:
- [ ] C++ build command:
- [ ] CTest command/result:
- [ ] Sanitizer command/result:
- [ ] Node test command/result:
- [ ] Typecheck result:
- [ ] Lint result:
- [ ] Build result:
- [ ] Distributed integration result:
- [ ] Security-negative test result:
- [ ] Fault-injection result:
- [ ] Benchmark command:
- [ ] Benchmark manifest path:
- [ ] Raw benchmark path:
- [ ] Summary benchmark path:
- [ ] Metric claims created (normally none until final):
- [ ] Phase-1 behaviors touched:
- [ ] Phase-1 regressions observed:
- [ ] Known limitations:
- [ ] Diff reviewed:
- [ ] git diff --check result:
- [ ] Secrets checked:
- [ ] Commit subject:
- [ ] Commit SHA:
- [ ] Next milestone readiness:

## Ledger for Milestone 20
- [ ] Milestone number/title:
- [ ] Start SHA:
- [ ] End SHA:
- [ ] Date/time:
- [ ] Host OS/architecture:
- [ ] Working-tree cleanliness before start:
- [ ] Repository instructions read:
- [ ] Source files inspected:
- [ ] Tests inspected:
- [ ] Third-party docs consulted:
- [ ] Files created:
- [ ] Files modified:
- [ ] Files deleted (normally none):
- [ ] Database migration generated:
- [ ] Database migration applied locally:
- [ ] Remote database changed (must normally be no unless approved):
- [ ] New environment variables:
- [ ] Human setup required:
- [ ] C++ configure command:
- [ ] C++ build command:
- [ ] CTest command/result:
- [ ] Sanitizer command/result:
- [ ] Node test command/result:
- [ ] Typecheck result:
- [ ] Lint result:
- [ ] Build result:
- [ ] Distributed integration result:
- [ ] Security-negative test result:
- [ ] Fault-injection result:
- [ ] Benchmark command:
- [ ] Benchmark manifest path:
- [ ] Raw benchmark path:
- [ ] Summary benchmark path:
- [ ] Metric claims created (normally none until final):
- [ ] Phase-1 behaviors touched:
- [ ] Phase-1 regressions observed:
- [ ] Known limitations:
- [ ] Diff reviewed:
- [ ] git diff --check result:
- [ ] Secrets checked:
- [ ] Commit subject:
- [ ] Commit SHA:
- [ ] Next milestone readiness:

## Ledger for Milestone 21
- [ ] Milestone number/title:
- [ ] Start SHA:
- [ ] End SHA:
- [ ] Date/time:
- [ ] Host OS/architecture:
- [ ] Working-tree cleanliness before start:
- [ ] Repository instructions read:
- [ ] Source files inspected:
- [ ] Tests inspected:
- [ ] Third-party docs consulted:
- [ ] Files created:
- [ ] Files modified:
- [ ] Files deleted (normally none):
- [ ] Database migration generated:
- [ ] Database migration applied locally:
- [ ] Remote database changed (must normally be no unless approved):
- [ ] New environment variables:
- [ ] Human setup required:
- [ ] C++ configure command:
- [ ] C++ build command:
- [ ] CTest command/result:
- [ ] Sanitizer command/result:
- [ ] Node test command/result:
- [ ] Typecheck result:
- [ ] Lint result:
- [ ] Build result:
- [ ] Distributed integration result:
- [ ] Security-negative test result:
- [ ] Fault-injection result:
- [ ] Benchmark command:
- [ ] Benchmark manifest path:
- [ ] Raw benchmark path:
- [ ] Summary benchmark path:
- [ ] Metric claims created (normally none until final):
- [ ] Phase-1 behaviors touched:
- [ ] Phase-1 regressions observed:
- [ ] Known limitations:
- [ ] Diff reviewed:
- [ ] git diff --check result:
- [ ] Secrets checked:
- [ ] Commit subject:
- [ ] Commit SHA:
- [ ] Next milestone readiness:

## Ledger for Milestone 22
- [ ] Milestone number/title:
- [ ] Start SHA:
- [ ] End SHA:
- [ ] Date/time:
- [ ] Host OS/architecture:
- [ ] Working-tree cleanliness before start:
- [ ] Repository instructions read:
- [ ] Source files inspected:
- [ ] Tests inspected:
- [ ] Third-party docs consulted:
- [ ] Files created:
- [ ] Files modified:
- [ ] Files deleted (normally none):
- [ ] Database migration generated:
- [ ] Database migration applied locally:
- [ ] Remote database changed (must normally be no unless approved):
- [ ] New environment variables:
- [ ] Human setup required:
- [ ] C++ configure command:
- [ ] C++ build command:
- [ ] CTest command/result:
- [ ] Sanitizer command/result:
- [ ] Node test command/result:
- [ ] Typecheck result:
- [ ] Lint result:
- [ ] Build result:
- [ ] Distributed integration result:
- [ ] Security-negative test result:
- [ ] Fault-injection result:
- [ ] Benchmark command:
- [ ] Benchmark manifest path:
- [ ] Raw benchmark path:
- [ ] Summary benchmark path:
- [ ] Metric claims created (normally none until final):
- [ ] Phase-1 behaviors touched:
- [ ] Phase-1 regressions observed:
- [ ] Known limitations:
- [ ] Diff reviewed:
- [ ] git diff --check result:
- [ ] Secrets checked:
- [ ] Commit subject:
- [ ] Commit SHA:
- [ ] Next milestone readiness:

## Ledger for Milestone 23
- [ ] Milestone number/title:
- [ ] Start SHA:
- [ ] End SHA:
- [ ] Date/time:
- [ ] Host OS/architecture:
- [ ] Working-tree cleanliness before start:
- [ ] Repository instructions read:
- [ ] Source files inspected:
- [ ] Tests inspected:
- [ ] Third-party docs consulted:
- [ ] Files created:
- [ ] Files modified:
- [ ] Files deleted (normally none):
- [ ] Database migration generated:
- [ ] Database migration applied locally:
- [ ] Remote database changed (must normally be no unless approved):
- [ ] New environment variables:
- [ ] Human setup required:
- [ ] C++ configure command:
- [ ] C++ build command:
- [ ] CTest command/result:
- [ ] Sanitizer command/result:
- [ ] Node test command/result:
- [ ] Typecheck result:
- [ ] Lint result:
- [ ] Build result:
- [ ] Distributed integration result:
- [ ] Security-negative test result:
- [ ] Fault-injection result:
- [ ] Benchmark command:
- [ ] Benchmark manifest path:
- [ ] Raw benchmark path:
- [ ] Summary benchmark path:
- [ ] Metric claims created (normally none until final):
- [ ] Phase-1 behaviors touched:
- [ ] Phase-1 regressions observed:
- [ ] Known limitations:
- [ ] Diff reviewed:
- [ ] git diff --check result:
- [ ] Secrets checked:
- [ ] Commit subject:
- [ ] Commit SHA:
- [ ] Next milestone readiness:

## Ledger for Milestone 24
- [ ] Milestone number/title:
- [ ] Start SHA:
- [ ] End SHA:
- [ ] Date/time:
- [ ] Host OS/architecture:
- [ ] Working-tree cleanliness before start:
- [ ] Repository instructions read:
- [ ] Source files inspected:
- [ ] Tests inspected:
- [ ] Third-party docs consulted:
- [ ] Files created:
- [ ] Files modified:
- [ ] Files deleted (normally none):
- [ ] Database migration generated:
- [ ] Database migration applied locally:
- [ ] Remote database changed (must normally be no unless approved):
- [ ] New environment variables:
- [ ] Human setup required:
- [ ] C++ configure command:
- [ ] C++ build command:
- [ ] CTest command/result:
- [ ] Sanitizer command/result:
- [ ] Node test command/result:
- [ ] Typecheck result:
- [ ] Lint result:
- [ ] Build result:
- [ ] Distributed integration result:
- [ ] Security-negative test result:
- [ ] Fault-injection result:
- [ ] Benchmark command:
- [ ] Benchmark manifest path:
- [ ] Raw benchmark path:
- [ ] Summary benchmark path:
- [ ] Metric claims created (normally none until final):
- [ ] Phase-1 behaviors touched:
- [ ] Phase-1 regressions observed:
- [ ] Known limitations:
- [ ] Diff reviewed:
- [ ] git diff --check result:
- [ ] Secrets checked:
- [ ] Commit subject:
- [ ] Commit SHA:
- [ ] Next milestone readiness:

## Ledger for Milestone 25
- [ ] Milestone number/title:
- [ ] Start SHA:
- [ ] End SHA:
- [ ] Date/time:
- [ ] Host OS/architecture:
- [ ] Working-tree cleanliness before start:
- [ ] Repository instructions read:
- [ ] Source files inspected:
- [ ] Tests inspected:
- [ ] Third-party docs consulted:
- [ ] Files created:
- [ ] Files modified:
- [ ] Files deleted (normally none):
- [ ] Database migration generated:
- [ ] Database migration applied locally:
- [ ] Remote database changed (must normally be no unless approved):
- [ ] New environment variables:
- [ ] Human setup required:
- [ ] C++ configure command:
- [ ] C++ build command:
- [ ] CTest command/result:
- [ ] Sanitizer command/result:
- [ ] Node test command/result:
- [ ] Typecheck result:
- [ ] Lint result:
- [ ] Build result:
- [ ] Distributed integration result:
- [ ] Security-negative test result:
- [ ] Fault-injection result:
- [ ] Benchmark command:
- [ ] Benchmark manifest path:
- [ ] Raw benchmark path:
- [ ] Summary benchmark path:
- [ ] Metric claims created (normally none until final):
- [ ] Phase-1 behaviors touched:
- [ ] Phase-1 regressions observed:
- [ ] Known limitations:
- [ ] Diff reviewed:
- [ ] git diff --check result:
- [ ] Secrets checked:
- [ ] Commit subject:
- [ ] Commit SHA:
- [ ] Next milestone readiness:

## Ledger for Milestone 26
- [ ] Milestone number/title:
- [ ] Start SHA:
- [ ] End SHA:
- [ ] Date/time:
- [ ] Host OS/architecture:
- [ ] Working-tree cleanliness before start:
- [ ] Repository instructions read:
- [ ] Source files inspected:
- [ ] Tests inspected:
- [ ] Third-party docs consulted:
- [ ] Files created:
- [ ] Files modified:
- [ ] Files deleted (normally none):
- [ ] Database migration generated:
- [ ] Database migration applied locally:
- [ ] Remote database changed (must normally be no unless approved):
- [ ] New environment variables:
- [ ] Human setup required:
- [ ] C++ configure command:
- [ ] C++ build command:
- [ ] CTest command/result:
- [ ] Sanitizer command/result:
- [ ] Node test command/result:
- [ ] Typecheck result:
- [ ] Lint result:
- [ ] Build result:
- [ ] Distributed integration result:
- [ ] Security-negative test result:
- [ ] Fault-injection result:
- [ ] Benchmark command:
- [ ] Benchmark manifest path:
- [ ] Raw benchmark path:
- [ ] Summary benchmark path:
- [ ] Metric claims created (normally none until final):
- [ ] Phase-1 behaviors touched:
- [ ] Phase-1 regressions observed:
- [ ] Known limitations:
- [ ] Diff reviewed:
- [ ] git diff --check result:
- [ ] Secrets checked:
- [ ] Commit subject:
- [ ] Commit SHA:
- [ ] Next milestone readiness:

## Ledger for Milestone 27
- [ ] Milestone number/title:
- [ ] Start SHA:
- [ ] End SHA:
- [ ] Date/time:
- [ ] Host OS/architecture:
- [ ] Working-tree cleanliness before start:
- [ ] Repository instructions read:
- [ ] Source files inspected:
- [ ] Tests inspected:
- [ ] Third-party docs consulted:
- [ ] Files created:
- [ ] Files modified:
- [ ] Files deleted (normally none):
- [ ] Database migration generated:
- [ ] Database migration applied locally:
- [ ] Remote database changed (must normally be no unless approved):
- [ ] New environment variables:
- [ ] Human setup required:
- [ ] C++ configure command:
- [ ] C++ build command:
- [ ] CTest command/result:
- [ ] Sanitizer command/result:
- [ ] Node test command/result:
- [ ] Typecheck result:
- [ ] Lint result:
- [ ] Build result:
- [ ] Distributed integration result:
- [ ] Security-negative test result:
- [ ] Fault-injection result:
- [ ] Benchmark command:
- [ ] Benchmark manifest path:
- [ ] Raw benchmark path:
- [ ] Summary benchmark path:
- [ ] Metric claims created (normally none until final):
- [ ] Phase-1 behaviors touched:
- [ ] Phase-1 regressions observed:
- [ ] Known limitations:
- [ ] Diff reviewed:
- [ ] git diff --check result:
- [ ] Secrets checked:
- [ ] Commit subject:
- [ ] Commit SHA:
- [ ] Next milestone readiness:

## Ledger for Milestone 28
- [ ] Milestone number/title:
- [ ] Start SHA:
- [ ] End SHA:
- [ ] Date/time:
- [ ] Host OS/architecture:
- [ ] Working-tree cleanliness before start:
- [ ] Repository instructions read:
- [ ] Source files inspected:
- [ ] Tests inspected:
- [ ] Third-party docs consulted:
- [ ] Files created:
- [ ] Files modified:
- [ ] Files deleted (normally none):
- [ ] Database migration generated:
- [ ] Database migration applied locally:
- [ ] Remote database changed (must normally be no unless approved):
- [ ] New environment variables:
- [ ] Human setup required:
- [ ] C++ configure command:
- [ ] C++ build command:
- [ ] CTest command/result:
- [ ] Sanitizer command/result:
- [ ] Node test command/result:
- [ ] Typecheck result:
- [ ] Lint result:
- [ ] Build result:
- [ ] Distributed integration result:
- [ ] Security-negative test result:
- [ ] Fault-injection result:
- [ ] Benchmark command:
- [ ] Benchmark manifest path:
- [ ] Raw benchmark path:
- [ ] Summary benchmark path:
- [ ] Metric claims created (normally none until final):
- [ ] Phase-1 behaviors touched:
- [ ] Phase-1 regressions observed:
- [ ] Known limitations:
- [ ] Diff reviewed:
- [ ] git diff --check result:
- [ ] Secrets checked:
- [ ] Commit subject:
- [ ] Commit SHA:
- [ ] Next milestone readiness:

## Ledger for Milestone 29
- [ ] Milestone number/title:
- [ ] Start SHA:
- [ ] End SHA:
- [ ] Date/time:
- [ ] Host OS/architecture:
- [ ] Working-tree cleanliness before start:
- [ ] Repository instructions read:
- [ ] Source files inspected:
- [ ] Tests inspected:
- [ ] Third-party docs consulted:
- [ ] Files created:
- [ ] Files modified:
- [ ] Files deleted (normally none):
- [ ] Database migration generated:
- [ ] Database migration applied locally:
- [ ] Remote database changed (must normally be no unless approved):
- [ ] New environment variables:
- [ ] Human setup required:
- [ ] C++ configure command:
- [ ] C++ build command:
- [ ] CTest command/result:
- [ ] Sanitizer command/result:
- [ ] Node test command/result:
- [ ] Typecheck result:
- [ ] Lint result:
- [ ] Build result:
- [ ] Distributed integration result:
- [ ] Security-negative test result:
- [ ] Fault-injection result:
- [ ] Benchmark command:
- [ ] Benchmark manifest path:
- [ ] Raw benchmark path:
- [ ] Summary benchmark path:
- [ ] Metric claims created (normally none until final):
- [ ] Phase-1 behaviors touched:
- [ ] Phase-1 regressions observed:
- [ ] Known limitations:
- [ ] Diff reviewed:
- [ ] git diff --check result:
- [ ] Secrets checked:
- [ ] Commit subject:
- [ ] Commit SHA:
- [ ] Next milestone readiness:

## Ledger for Milestone 30
- [ ] Milestone number/title:
- [ ] Start SHA:
- [ ] End SHA:
- [ ] Date/time:
- [ ] Host OS/architecture:
- [ ] Working-tree cleanliness before start:
- [ ] Repository instructions read:
- [ ] Source files inspected:
- [ ] Tests inspected:
- [ ] Third-party docs consulted:
- [ ] Files created:
- [ ] Files modified:
- [ ] Files deleted (normally none):
- [ ] Database migration generated:
- [ ] Database migration applied locally:
- [ ] Remote database changed (must normally be no unless approved):
- [ ] New environment variables:
- [ ] Human setup required:
- [ ] C++ configure command:
- [ ] C++ build command:
- [ ] CTest command/result:
- [ ] Sanitizer command/result:
- [ ] Node test command/result:
- [ ] Typecheck result:
- [ ] Lint result:
- [ ] Build result:
- [ ] Distributed integration result:
- [ ] Security-negative test result:
- [ ] Fault-injection result:
- [ ] Benchmark command:
- [ ] Benchmark manifest path:
- [ ] Raw benchmark path:
- [ ] Summary benchmark path:
- [ ] Metric claims created (normally none until final):
- [ ] Phase-1 behaviors touched:
- [ ] Phase-1 regressions observed:
- [ ] Known limitations:
- [ ] Diff reviewed:
- [ ] git diff --check result:
- [ ] Secrets checked:
- [ ] Commit subject:
- [ ] Commit SHA:
- [ ] Next milestone readiness:

## Ledger for Milestone 31
- [ ] Milestone number/title:
- [ ] Start SHA:
- [ ] End SHA:
- [ ] Date/time:
- [ ] Host OS/architecture:
- [ ] Working-tree cleanliness before start:
- [ ] Repository instructions read:
- [ ] Source files inspected:
- [ ] Tests inspected:
- [ ] Third-party docs consulted:
- [ ] Files created:
- [ ] Files modified:
- [ ] Files deleted (normally none):
- [ ] Database migration generated:
- [ ] Database migration applied locally:
- [ ] Remote database changed (must normally be no unless approved):
- [ ] New environment variables:
- [ ] Human setup required:
- [ ] C++ configure command:
- [ ] C++ build command:
- [ ] CTest command/result:
- [ ] Sanitizer command/result:
- [ ] Node test command/result:
- [ ] Typecheck result:
- [ ] Lint result:
- [ ] Build result:
- [ ] Distributed integration result:
- [ ] Security-negative test result:
- [ ] Fault-injection result:
- [ ] Benchmark command:
- [ ] Benchmark manifest path:
- [ ] Raw benchmark path:
- [ ] Summary benchmark path:
- [ ] Metric claims created (normally none until final):
- [ ] Phase-1 behaviors touched:
- [ ] Phase-1 regressions observed:
- [ ] Known limitations:
- [ ] Diff reviewed:
- [ ] git diff --check result:
- [ ] Secrets checked:
- [ ] Commit subject:
- [ ] Commit SHA:
- [ ] Next milestone readiness:

## Ledger for Milestone 32
- [ ] Milestone number/title:
- [ ] Start SHA:
- [ ] End SHA:
- [ ] Date/time:
- [ ] Host OS/architecture:
- [ ] Working-tree cleanliness before start:
- [ ] Repository instructions read:
- [ ] Source files inspected:
- [ ] Tests inspected:
- [ ] Third-party docs consulted:
- [ ] Files created:
- [ ] Files modified:
- [ ] Files deleted (normally none):
- [ ] Database migration generated:
- [ ] Database migration applied locally:
- [ ] Remote database changed (must normally be no unless approved):
- [ ] New environment variables:
- [ ] Human setup required:
- [ ] C++ configure command:
- [ ] C++ build command:
- [ ] CTest command/result:
- [ ] Sanitizer command/result:
- [ ] Node test command/result:
- [ ] Typecheck result:
- [ ] Lint result:
- [ ] Build result:
- [ ] Distributed integration result:
- [ ] Security-negative test result:
- [ ] Fault-injection result:
- [ ] Benchmark command:
- [ ] Benchmark manifest path:
- [ ] Raw benchmark path:
- [ ] Summary benchmark path:
- [ ] Metric claims created (normally none until final):
- [ ] Phase-1 behaviors touched:
- [ ] Phase-1 regressions observed:
- [ ] Known limitations:
- [ ] Diff reviewed:
- [ ] git diff --check result:
- [ ] Secrets checked:
- [ ] Commit subject:
- [ ] Commit SHA:
- [ ] Next milestone readiness:

## Ledger for Milestone 33
- [ ] Milestone number/title:
- [ ] Start SHA:
- [ ] End SHA:
- [ ] Date/time:
- [ ] Host OS/architecture:
- [ ] Working-tree cleanliness before start:
- [ ] Repository instructions read:
- [ ] Source files inspected:
- [ ] Tests inspected:
- [ ] Third-party docs consulted:
- [ ] Files created:
- [ ] Files modified:
- [ ] Files deleted (normally none):
- [ ] Database migration generated:
- [ ] Database migration applied locally:
- [ ] Remote database changed (must normally be no unless approved):
- [ ] New environment variables:
- [ ] Human setup required:
- [ ] C++ configure command:
- [ ] C++ build command:
- [ ] CTest command/result:
- [ ] Sanitizer command/result:
- [ ] Node test command/result:
- [ ] Typecheck result:
- [ ] Lint result:
- [ ] Build result:
- [ ] Distributed integration result:
- [ ] Security-negative test result:
- [ ] Fault-injection result:
- [ ] Benchmark command:
- [ ] Benchmark manifest path:
- [ ] Raw benchmark path:
- [ ] Summary benchmark path:
- [ ] Metric claims created (normally none until final):
- [ ] Phase-1 behaviors touched:
- [ ] Phase-1 regressions observed:
- [ ] Known limitations:
- [ ] Diff reviewed:
- [ ] git diff --check result:
- [ ] Secrets checked:
- [ ] Commit subject:
- [ ] Commit SHA:
- [ ] Next milestone readiness:

## Ledger for Milestone 34
- [ ] Milestone number/title:
- [ ] Start SHA:
- [ ] End SHA:
- [ ] Date/time:
- [ ] Host OS/architecture:
- [ ] Working-tree cleanliness before start:
- [ ] Repository instructions read:
- [ ] Source files inspected:
- [ ] Tests inspected:
- [ ] Third-party docs consulted:
- [ ] Files created:
- [ ] Files modified:
- [ ] Files deleted (normally none):
- [ ] Database migration generated:
- [ ] Database migration applied locally:
- [ ] Remote database changed (must normally be no unless approved):
- [ ] New environment variables:
- [ ] Human setup required:
- [ ] C++ configure command:
- [ ] C++ build command:
- [ ] CTest command/result:
- [ ] Sanitizer command/result:
- [ ] Node test command/result:
- [ ] Typecheck result:
- [ ] Lint result:
- [ ] Build result:
- [ ] Distributed integration result:
- [ ] Security-negative test result:
- [ ] Fault-injection result:
- [ ] Benchmark command:
- [ ] Benchmark manifest path:
- [ ] Raw benchmark path:
- [ ] Summary benchmark path:
- [ ] Metric claims created (normally none until final):
- [ ] Phase-1 behaviors touched:
- [ ] Phase-1 regressions observed:
- [ ] Known limitations:
- [ ] Diff reviewed:
- [ ] git diff --check result:
- [ ] Secrets checked:
- [ ] Commit subject:
- [ ] Commit SHA:
- [ ] Next milestone readiness:

## Ledger for Milestone 35
- [ ] Milestone number/title:
- [ ] Start SHA:
- [ ] End SHA:
- [ ] Date/time:
- [ ] Host OS/architecture:
- [ ] Working-tree cleanliness before start:
- [ ] Repository instructions read:
- [ ] Source files inspected:
- [ ] Tests inspected:
- [ ] Third-party docs consulted:
- [ ] Files created:
- [ ] Files modified:
- [ ] Files deleted (normally none):
- [ ] Database migration generated:
- [ ] Database migration applied locally:
- [ ] Remote database changed (must normally be no unless approved):
- [ ] New environment variables:
- [ ] Human setup required:
- [ ] C++ configure command:
- [ ] C++ build command:
- [ ] CTest command/result:
- [ ] Sanitizer command/result:
- [ ] Node test command/result:
- [ ] Typecheck result:
- [ ] Lint result:
- [ ] Build result:
- [ ] Distributed integration result:
- [ ] Security-negative test result:
- [ ] Fault-injection result:
- [ ] Benchmark command:
- [ ] Benchmark manifest path:
- [ ] Raw benchmark path:
- [ ] Summary benchmark path:
- [ ] Metric claims created (normally none until final):
- [ ] Phase-1 behaviors touched:
- [ ] Phase-1 regressions observed:
- [ ] Known limitations:
- [ ] Diff reviewed:
- [ ] git diff --check result:
- [ ] Secrets checked:
- [ ] Commit subject:
- [ ] Commit SHA:
- [ ] Next milestone readiness:

## Ledger for Milestone 36
- [ ] Milestone number/title:
- [ ] Start SHA:
- [ ] End SHA:
- [ ] Date/time:
- [ ] Host OS/architecture:
- [ ] Working-tree cleanliness before start:
- [ ] Repository instructions read:
- [ ] Source files inspected:
- [ ] Tests inspected:
- [ ] Third-party docs consulted:
- [ ] Files created:
- [ ] Files modified:
- [ ] Files deleted (normally none):
- [ ] Database migration generated:
- [ ] Database migration applied locally:
- [ ] Remote database changed (must normally be no unless approved):
- [ ] New environment variables:
- [ ] Human setup required:
- [ ] C++ configure command:
- [ ] C++ build command:
- [ ] CTest command/result:
- [ ] Sanitizer command/result:
- [ ] Node test command/result:
- [ ] Typecheck result:
- [ ] Lint result:
- [ ] Build result:
- [ ] Distributed integration result:
- [ ] Security-negative test result:
- [ ] Fault-injection result:
- [ ] Benchmark command:
- [ ] Benchmark manifest path:
- [ ] Raw benchmark path:
- [ ] Summary benchmark path:
- [ ] Metric claims created (normally none until final):
- [ ] Phase-1 behaviors touched:
- [ ] Phase-1 regressions observed:
- [ ] Known limitations:
- [ ] Diff reviewed:
- [ ] git diff --check result:
- [ ] Secrets checked:
- [ ] Commit subject:
- [ ] Commit SHA:
- [ ] Next milestone readiness:

## Ledger for Milestone 37
- [ ] Milestone number/title:
- [ ] Start SHA:
- [ ] End SHA:
- [ ] Date/time:
- [ ] Host OS/architecture:
- [ ] Working-tree cleanliness before start:
- [ ] Repository instructions read:
- [ ] Source files inspected:
- [ ] Tests inspected:
- [ ] Third-party docs consulted:
- [ ] Files created:
- [ ] Files modified:
- [ ] Files deleted (normally none):
- [ ] Database migration generated:
- [ ] Database migration applied locally:
- [ ] Remote database changed (must normally be no unless approved):
- [ ] New environment variables:
- [ ] Human setup required:
- [ ] C++ configure command:
- [ ] C++ build command:
- [ ] CTest command/result:
- [ ] Sanitizer command/result:
- [ ] Node test command/result:
- [ ] Typecheck result:
- [ ] Lint result:
- [ ] Build result:
- [ ] Distributed integration result:
- [ ] Security-negative test result:
- [ ] Fault-injection result:
- [ ] Benchmark command:
- [ ] Benchmark manifest path:
- [ ] Raw benchmark path:
- [ ] Summary benchmark path:
- [ ] Metric claims created (normally none until final):
- [ ] Phase-1 behaviors touched:
- [ ] Phase-1 regressions observed:
- [ ] Known limitations:
- [ ] Diff reviewed:
- [ ] git diff --check result:
- [ ] Secrets checked:
- [ ] Commit subject:
- [ ] Commit SHA:
- [ ] Next milestone readiness:

## Ledger for Milestone 38
- [ ] Milestone number/title:
- [ ] Start SHA:
- [ ] End SHA:
- [ ] Date/time:
- [ ] Host OS/architecture:
- [ ] Working-tree cleanliness before start:
- [ ] Repository instructions read:
- [ ] Source files inspected:
- [ ] Tests inspected:
- [ ] Third-party docs consulted:
- [ ] Files created:
- [ ] Files modified:
- [ ] Files deleted (normally none):
- [ ] Database migration generated:
- [ ] Database migration applied locally:
- [ ] Remote database changed (must normally be no unless approved):
- [ ] New environment variables:
- [ ] Human setup required:
- [ ] C++ configure command:
- [ ] C++ build command:
- [ ] CTest command/result:
- [ ] Sanitizer command/result:
- [ ] Node test command/result:
- [ ] Typecheck result:
- [ ] Lint result:
- [ ] Build result:
- [ ] Distributed integration result:
- [ ] Security-negative test result:
- [ ] Fault-injection result:
- [ ] Benchmark command:
- [ ] Benchmark manifest path:
- [ ] Raw benchmark path:
- [ ] Summary benchmark path:
- [ ] Metric claims created (normally none until final):
- [ ] Phase-1 behaviors touched:
- [ ] Phase-1 regressions observed:
- [ ] Known limitations:
- [ ] Diff reviewed:
- [ ] git diff --check result:
- [ ] Secrets checked:
- [ ] Commit subject:
- [ ] Commit SHA:
- [ ] Next milestone readiness:

## Ledger for Milestone 39
- [ ] Milestone number/title:
- [ ] Start SHA:
- [ ] End SHA:
- [ ] Date/time:
- [ ] Host OS/architecture:
- [ ] Working-tree cleanliness before start:
- [ ] Repository instructions read:
- [ ] Source files inspected:
- [ ] Tests inspected:
- [ ] Third-party docs consulted:
- [ ] Files created:
- [ ] Files modified:
- [ ] Files deleted (normally none):
- [ ] Database migration generated:
- [ ] Database migration applied locally:
- [ ] Remote database changed (must normally be no unless approved):
- [ ] New environment variables:
- [ ] Human setup required:
- [ ] C++ configure command:
- [ ] C++ build command:
- [ ] CTest command/result:
- [ ] Sanitizer command/result:
- [ ] Node test command/result:
- [ ] Typecheck result:
- [ ] Lint result:
- [ ] Build result:
- [ ] Distributed integration result:
- [ ] Security-negative test result:
- [ ] Fault-injection result:
- [ ] Benchmark command:
- [ ] Benchmark manifest path:
- [ ] Raw benchmark path:
- [ ] Summary benchmark path:
- [ ] Metric claims created (normally none until final):
- [ ] Phase-1 behaviors touched:
- [ ] Phase-1 regressions observed:
- [ ] Known limitations:
- [ ] Diff reviewed:
- [ ] git diff --check result:
- [ ] Secrets checked:
- [ ] Commit subject:
- [ ] Commit SHA:
- [ ] Next milestone readiness:

## Ledger for Milestone 40
- [ ] Milestone number/title:
- [ ] Start SHA:
- [ ] End SHA:
- [ ] Date/time:
- [ ] Host OS/architecture:
- [ ] Working-tree cleanliness before start:
- [ ] Repository instructions read:
- [ ] Source files inspected:
- [ ] Tests inspected:
- [ ] Third-party docs consulted:
- [ ] Files created:
- [ ] Files modified:
- [ ] Files deleted (normally none):
- [ ] Database migration generated:
- [ ] Database migration applied locally:
- [ ] Remote database changed (must normally be no unless approved):
- [ ] New environment variables:
- [ ] Human setup required:
- [ ] C++ configure command:
- [ ] C++ build command:
- [ ] CTest command/result:
- [ ] Sanitizer command/result:
- [ ] Node test command/result:
- [ ] Typecheck result:
- [ ] Lint result:
- [ ] Build result:
- [ ] Distributed integration result:
- [ ] Security-negative test result:
- [ ] Fault-injection result:
- [ ] Benchmark command:
- [ ] Benchmark manifest path:
- [ ] Raw benchmark path:
- [ ] Summary benchmark path:
- [ ] Metric claims created (normally none until final):
- [ ] Phase-1 behaviors touched:
- [ ] Phase-1 regressions observed:
- [ ] Known limitations:
- [ ] Diff reviewed:
- [ ] git diff --check result:
- [ ] Secrets checked:
- [ ] Commit subject:
- [ ] Commit SHA:
- [ ] Next milestone readiness:

# APPENDIX Z — Layer-specific code-review heuristics
## C++ scheduler core
- [ ] Review 01: No UI/Next.js dependency leaks into core.
- [ ] Review 02: No transport dependency in pure DAG tests.
- [ ] Review 03: Ownership clear.
- [ ] Review 04: State transition atomic at logical level.
- [ ] Review 05: Exceptions handled.
- [ ] Review 06: No hidden global mutable state.
- [ ] Review 07: Deterministic test hooks available.
- [ ] Review 08: Clock injectable/fakeable where timing state machines need deterministic tests.
- [ ] Review 09: No UI/Next.js dependency leaks into core.
- [ ] Review 10: No transport dependency in pure DAG tests.
- [ ] Review 11: Ownership clear.
- [ ] Review 12: State transition atomic at logical level.
- [ ] Review 13: Exceptions handled.
- [ ] Review 14: No hidden global mutable state.
- [ ] Review 15: Deterministic test hooks available.
- [ ] Review 16: Clock injectable/fakeable where timing state machines need deterministic tests.
- [ ] Review 17: No UI/Next.js dependency leaks into core.
- [ ] Review 18: No transport dependency in pure DAG tests.
- [ ] Review 19: Ownership clear.
- [ ] Review 20: State transition atomic at logical level.

## gRPC boundary
- [ ] Review 01: Every request validated.
- [ ] Review 02: Run/org IDs bounded/validated.
- [ ] Review 03: Status codes meaningful.
- [ ] Review 04: Auth metadata checked where required.
- [ ] Review 05: Deadlines respected.
- [ ] Review 06: No secret echoed.
- [ ] Review 07: Server shutdown graceful.
- [ ] Review 08: Protocol version compatible.
- [ ] Review 09: Every request validated.
- [ ] Review 10: Run/org IDs bounded/validated.
- [ ] Review 11: Status codes meaningful.
- [ ] Review 12: Auth metadata checked where required.
- [ ] Review 13: Deadlines respected.
- [ ] Review 14: No secret echoed.
- [ ] Review 15: Server shutdown graceful.
- [ ] Review 16: Protocol version compatible.
- [ ] Review 17: Every request validated.
- [ ] Review 18: Run/org IDs bounded/validated.
- [ ] Review 19: Status codes meaningful.
- [ ] Review 20: Auth metadata checked where required.

## Redis transport
- [ ] Review 01: Keys namespaced.
- [ ] Review 02: Consumer groups idempotently initialized.
- [ ] Review 03: Reconnect bounded.
- [ ] Review 04: Payload size bounded.
- [ ] Review 05: Malformed payload rejected.
- [ ] Review 06: Ack ordering correct.
- [ ] Review 07: Duplicate result harmless.
- [ ] Review 08: No Pub/Sub-only durability assumption.
- [ ] Review 09: Keys namespaced.
- [ ] Review 10: Consumer groups idempotently initialized.
- [ ] Review 11: Reconnect bounded.
- [ ] Review 12: Payload size bounded.
- [ ] Review 13: Malformed payload rejected.
- [ ] Review 14: Ack ordering correct.
- [ ] Review 15: Duplicate result harmless.
- [ ] Review 16: No Pub/Sub-only durability assumption.
- [ ] Review 17: Keys namespaced.
- [ ] Review 18: Consumer groups idempotently initialized.
- [ ] Review 19: Reconnect bounded.
- [ ] Review 20: Payload size bounded.

## Postgres persistence
- [ ] Review 01: Parameterized queries.
- [ ] Review 02: Transactions wrap multi-row invariants.
- [ ] Review 03: Indexes support access patterns.
- [ ] Review 04: Unique constraints back logical uniqueness.
- [ ] Review 05: Immutable version not updated.
- [ ] Review 06: Tenant/org scope enforced.
- [ ] Review 07: Migration additive.
- [ ] Review 08: No remote destructive test.
- [ ] Review 09: Parameterized queries.
- [ ] Review 10: Transactions wrap multi-row invariants.
- [ ] Review 11: Indexes support access patterns.
- [ ] Review 12: Unique constraints back logical uniqueness.
- [ ] Review 13: Immutable version not updated.
- [ ] Review 14: Tenant/org scope enforced.
- [ ] Review 15: Migration additive.
- [ ] Review 16: No remote destructive test.
- [ ] Review 17: Parameterized queries.
- [ ] Review 18: Transactions wrap multi-row invariants.
- [ ] Review 19: Indexes support access patterns.
- [ ] Review 20: Unique constraints back logical uniqueness.

## TypeScript worker
- [ ] Review 01: Existing executors reused.
- [ ] Review 02: Secrets server-side.
- [ ] Review 03: Email test sink used in automated tests.
- [ ] Review 04: Session manager cleanup robust.
- [ ] Review 05: Cancellation handled.
- [ ] Review 06: Lease renewals stop on terminal attempt.
- [ ] Review 07: Result publication durable semantics documented.
- [ ] Review 08: No unbounded concurrency outside scheduler admission.
- [ ] Review 09: Existing executors reused.
- [ ] Review 10: Secrets server-side.
- [ ] Review 11: Email test sink used in automated tests.
- [ ] Review 12: Session manager cleanup robust.
- [ ] Review 13: Cancellation handled.
- [ ] Review 14: Lease renewals stop on terminal attempt.
- [ ] Review 15: Result publication durable semantics documented.
- [ ] Review 16: No unbounded concurrency outside scheduler admission.
- [ ] Review 17: Existing executors reused.
- [ ] Review 18: Secrets server-side.
- [ ] Review 19: Email test sink used in automated tests.
- [ ] Review 20: Session manager cleanup robust.

## Next.js integration
- [ ] Review 01: Legacy mode preserved.
- [ ] Review 02: Server-side auth before engine submission.
- [ ] Review 03: No auto-run from planner.
- [ ] Review 04: Run IDs normalized.
- [ ] Review 05: SSE/event access org-authorized.
- [ ] Review 06: React provider shared.
- [ ] Review 07: Stale run state not painted.
- [ ] Review 08: Rerun uses current approved graph snapshot.
- [ ] Review 09: Legacy mode preserved.
- [ ] Review 10: Server-side auth before engine submission.
- [ ] Review 11: No auto-run from planner.
- [ ] Review 12: Run IDs normalized.
- [ ] Review 13: SSE/event access org-authorized.
- [ ] Review 14: React provider shared.
- [ ] Review 15: Stale run state not painted.
- [ ] Review 16: Rerun uses current approved graph snapshot.
- [ ] Review 17: Legacy mode preserved.
- [ ] Review 18: Server-side auth before engine submission.
- [ ] Review 19: No auto-run from planner.
- [ ] Review 20: Run IDs normalized.

## Benchmarks
- [ ] Review 01: Release build.
- [ ] Review 02: Clean SHA.
- [ ] Review 03: Warmup.
- [ ] Review 04: Repeated trials.
- [ ] Review 05: Hardware manifest.
- [ ] Review 06: Raw samples.
- [ ] Review 07: Summary generated.
- [ ] Review 08: No selective deletion of slow samples without documented exclusion rule.
- [ ] Review 09: Release build.
- [ ] Review 10: Clean SHA.
- [ ] Review 11: Warmup.
- [ ] Review 12: Repeated trials.
- [ ] Review 13: Hardware manifest.
- [ ] Review 14: Raw samples.
- [ ] Review 15: Summary generated.
- [ ] Review 16: No selective deletion of slow samples without documented exclusion rule.
- [ ] Review 17: Release build.
- [ ] Review 18: Clean SHA.
- [ ] Review 19: Warmup.
- [ ] Review 20: Repeated trials.

# APPENDIX AA — Command discipline and safety

Use commands deliberately. Do not blindly paste all commands at once.
## Repository: `git status --short`
- Before and after every milestone.
- Check exit status; do not infer success from partial output.
- Capture relevant output in milestone report when it proves completion.
- Do not add `|| true` to hide failure except when intentionally probing availability and clearly documenting it.
- Do not run against production credentials by accident.

## Repository: `git diff --check`
- Before every commit.
- Check exit status; do not infer success from partial output.
- Capture relevant output in milestone report when it proves completion.
- Do not add `|| true` to hide failure except when intentionally probing availability and clearly documenting it.
- Do not run against production credentials by accident.

## Repository: `git diff`
- Review intended changes.
- Check exit status; do not infer success from partial output.
- Capture relevant output in milestone report when it proves completion.
- Do not add `|| true` to hide failure except when intentionally probing availability and clearly documenting it.
- Do not run against production credentials by accident.

## Node: `npm test`
- Phase-1 regression gate.
- Check exit status; do not infer success from partial output.
- Capture relevant output in milestone report when it proves completion.
- Do not add `|| true` to hide failure except when intentionally probing availability and clearly documenting it.
- Do not run against production credentials by accident.

## Node: `npm run typecheck`
- TypeScript static correctness.
- Check exit status; do not infer success from partial output.
- Capture relevant output in milestone report when it proves completion.
- Do not add `|| true` to hide failure except when intentionally probing availability and clearly documenting it.
- Do not run against production credentials by accident.

## Node: `npm run lint`
- Lint gate.
- Check exit status; do not infer success from partial output.
- Capture relevant output in milestone report when it proves completion.
- Do not add `|| true` to hide failure except when intentionally probing availability and clearly documenting it.
- Do not run against production credentials by accident.

## Node: `npm run build`
- Production integration gate.
- Check exit status; do not infer success from partial output.
- Capture relevant output in milestone report when it proves completion.
- Do not add `|| true` to hide failure except when intentionally probing availability and clearly documenting it.
- Do not run against production credentials by accident.

## CMake: `cmake -S engine -B <build-dir> ...`
- Configure in an out-of-source directory.
- Check exit status; do not infer success from partial output.
- Capture relevant output in milestone report when it proves completion.
- Do not add `|| true` to hide failure except when intentionally probing availability and clearly documenting it.
- Do not run against production credentials by accident.

## CMake: `cmake --build <build-dir> --config Release`
- Release build for benchmark binaries.
- Check exit status; do not infer success from partial output.
- Capture relevant output in milestone report when it proves completion.
- Do not add `|| true` to hide failure except when intentionally probing availability and clearly documenting it.
- Do not run against production credentials by accident.

## CTest: `ctest --test-dir <build-dir> --output-on-failure`
- Run C++ tests.
- Check exit status; do not infer success from partial output.
- Capture relevant output in milestone report when it proves completion.
- Do not add `|| true` to hide failure except when intentionally probing availability and clearly documenting it.
- Do not run against production credentials by accident.

## Docker: `docker compose -f <phase2-compose> config`
- Validate compose before startup.
- Check exit status; do not infer success from partial output.
- Capture relevant output in milestone report when it proves completion.
- Do not add `|| true` to hide failure except when intentionally probing availability and clearly documenting it.
- Do not run against production credentials by accident.

## Docker: `docker compose ... up -d`
- Start local Phase-2 services only.
- Check exit status; do not infer success from partial output.
- Capture relevant output in milestone report when it proves completion.
- Do not add `|| true` to hide failure except when intentionally probing availability and clearly documenting it.
- Do not run against production credentials by accident.

## Docker: `docker compose ... down`
- Stop local services; do not use destructive volume flags unless test reset is intended.
- Check exit status; do not infer success from partial output.
- Capture relevant output in milestone report when it proves completion.
- Do not add `|| true` to hide failure except when intentionally probing availability and clearly documenting it.
- Do not run against production credentials by accident.

---

# FINAL EXECUTION DIRECTIVE TO THE CODING AGENT

Start at **Milestone 01**.

Do not jump directly to C++ implementation before reconciling and certifying Phase 1.

At each milestone:

1. inspect;
2. plan;
3. implement only that milestone;
4. test;
5. run required Phase-1 regression gates;
6. review diff;
7. commit locally;
8. record evidence;
9. continue automatically.

Stop only for a genuine human-only blocker under the Human-Intervention Contract.

When human intervention is needed, state exactly what must be configured or approved and do not ask the user to paste a secret.

Do not stop because a benchmark result is unimpressive.

Do not alter raw evidence to make the project look better.

Do not turn this project into a keyword collection.

The final project should be impressive because the scheduler, distributed worker semantics, failure handling, observability, tests, and benchmark evidence are real.

The final resume wording comes **after** the evidence.

Begin Milestone 01 now.
