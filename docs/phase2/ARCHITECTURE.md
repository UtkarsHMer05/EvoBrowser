# Phase 2 — Architecture

**Status:** design baseline (Milestone 03). Implementation follows in M04+.
**Companion docs:** `FAILURE_MODEL.md`, `BENCHMARK_METHODOLOGY.md`, `PROGRESS.md`.

## 1. Goal

Add a second, opt-in execution engine — the **Evo engine** — beneath the
existing Phase-1 product. The Phase-1 control plane (AI planner, collaborative
canvas, explicit Run/Stop, results, replay) is preserved untouched; the
Phase-1 Trigger.dev task remains the **legacy engine** and the default until
the Milestone 40 compatibility decision.

Phase 2 introduces, in order:

1. A C++20 **local concurrent DAG scheduler** (M04–M15) — dependency-aware
   scheduling, bounded `std::jthread` pool, resource classes, cancellation,
   evidence-grade instrumentation, benchmarks.
2. A **distributed runtime** (M16–M26) — gRPC control surface, Redis Streams
   task transport, TypeScript workers reusing the existing node executors,
   durable Postgres state.
3. **Product integration** (M27–M30) — engine abstraction + feature flag in
   Next.js, engine-neutral run events, UI parity, end-to-end cancellation.
4. **Fault tolerance & fairness** (M31–M37) — leases/heartbeats, node-level
   retry, idempotency, crash/restart recovery, tenant quotas, fair scheduling.
5. **Hardening & evidence** (M38–M40) — observability, security, CI gates,
   final benchmark campaign, audit.

## 2. Target architecture

```text
                             USER
                              │
                              ▼
                 AI Planner + Visual Canvas
                     (existing Phase 1 — unchanged)
                              │
                              ▼
                    Explicit Run / Stop
                              │
                              ▼
                    Execution Abstraction (M27)
                       /              \
                      /                \
             Legacy Engine           Evo Engine
             Trigger.dev             opt-in via feature flag
             (Phase-1 task,              │
              unchanged)                 ▼
                 │                 gRPC / Protobuf (M16–17)
                 │                       │
                 │                       ▼
                 │               C++20 Orchestrator (engine/)
                 │              ┌───────────────────────┐
                 │              │ DAG model (M05)       │
                 │              │ state machines (M07)  │
                 │              │ ready queue (M08)     │
                 │              │ jthread pool (M09)    │
                 │              │ resource classes (M12)│
                 │              │ tenant fairness (M37) │
                 │              │ cancellation (M11/30) │
                 │              │ retries/leases (M31/32)│
                 │              └─────────┬─────────────┘
                 │                        │
                 │                   Redis Streams (M21)
                 │                        │
                 │          ┌─────────────┼─────────────┐
                 │          ▼             ▼             ▼
                 │      TS Worker 1   TS Worker 2   TS Worker N   (M23–24)
                 │          │             │             │
                 │          └─────────────┼─────────────┘
                 │                        │
                 │            existing node executors (reused, not forked)
                 │                        │
                 │          Stagehand / Browserbase / Resend
                 │                        │
                 └──────────────┬─────────┘
                                ▼
                     Normalized run events/state (M28)
                                │
                                ▼
                  Existing EvoBrowser run experience (M29 UI parity)
```

## 3. Dual-engine strategy

- **Legacy engine (Trigger.dev)**: the certified Phase-1 path. It stays the
  default and remains functional through all of Phase 2. No Phase-2 milestone
  may delete or weaken it.
- **Evo engine**: selected per-run by a server-side feature flag evaluated in
  `runWorkflowAction` (M27). The flag is fail-closed to legacy: any error
  resolving the engine choice runs the workflow on the legacy path.
- Both engines consume the **same validated graph snapshot** and produce
  **engine-neutral run events** (M28), so the canvas/console/results UI does
  not branch on engine type.
- The final decision on whether legacy remains permanently available is made
  at M40 with evidence, not in advance.

## 4. Component responsibilities

### 4.1 Next.js control plane (existing + M27–M29)
- Auth (Clerk), org tenancy, plan gates — unchanged, server-side.
- Graph validation + snapshot persistence — unchanged.
- Engine selection (M27), run submission to the Evo orchestrator via gRPC,
  cancellation forwarding (M30).
- Engine-neutral event fan-out to the UI (M28) with the same UX contract as
  Phase 1: live step statuses, live-view gate, results popup on terminal
  status only.

### 4.2 C++20 orchestrator (`engine/`)
- Owns scheduling decisions only: dependency readiness, dispatch order,
  resource admission, tenant fairness, retry timing, cancellation
  coordination, lease supervision.
- **Does not** execute browser/email work itself. It dispatches task envelopes
  and consumes result envelopes.
- Local mode (M04–M15): in-process execution of synthetic/benchmark tasks via
  the internal jthread pool.
- Distributed mode (M17+): gRPC service for control; Redis Streams for task
  transport to TS workers; Postgres for durable state.

### 4.3 Redis (Streams)
- Durable task transport between orchestrator and TS workers: append-only
  streams, consumer groups, pending entries, explicit ACK.
- Application-level leases, attempt semantics, retry policy, and idempotency
  are implemented by us (M31–M33) — Redis provides transport durability, not
  exactly-once semantics.

### 4.4 PostgreSQL (Neon)
- Remains the durable application/audit database. Drizzle stays the
  schema/migration authority (M19 additive tables):
  `workflow_versions`, `evo_runs`, `evo_node_runs`, `evo_task_attempts`,
  `evo_worker_leases`, `evo_idempotency`.
- The C++ service reads/writes selected Phase-2 tables through a native
  Postgres client with parameterized SQL; it never owns migrations.

### 4.5 TypeScript distributed workers (M23–M24)
- Reuse the **existing** interpolation engine and node executors — no fork.
- Load the immutable workflow version + upstream outputs, execute one node
  attempt, publish result/failure envelopes.
- Own the Stagehand session for browser-affinity work pinned to that worker
  (see §5).

## 5. Browser session affinity (why same-session nodes cannot blindly parallelize)

All default Phase-1 browser node types (`open-url`, `act`, `extract`,
`observe`, `agent`) in one run share **one Browserbase/Stagehand session** and
mutate the same page/context. Therefore:

- Browser-mutating nodes with the same **browser affinity key** (default:
  `run:<runId>`) must never execute concurrently against the same
  page/context. The scheduler models this as a resource class with capacity 1
  per affinity key (M12), derived from execution policy — never from planner
  prose.
- Independent DAG branches that are *both* browser nodes still serialize on
  the browser resource; concurrency wins in Phase 2 come from overlapping
  browser work with non-browser work (e.g. `send-email`), from multi-run
  scheduling, and from future explicitly-tested multi-session designs — not
  from pretending one page can be driven twice at once.
- In distributed mode, a live browser resource group is pinned to one owning
  worker while the session exists (M25). No second worker is dispatched to the
  same affinity key simultaneously. If the owning worker dies, the browser
  resource is treated as **lost** unless reconnection is verified against real
  Browserbase/Stagehand APIs — no claimed transparent session continuation.
- The live-view gate, final screenshot, and replay semantics are preserved in
  Evo mode (M25).

## 6. State machines (target semantics; implemented/tested in M07+)

### 6.1 Workflow run
`QUEUED → RUNNING → { SUCCEEDED | FAILED | CANCELED }`
A terminal run never returns to RUNNING. A rerun is a new run row with a new
run ID.

### 6.2 Node (logical)
`BLOCKED → READY → DISPATCHED → RUNNING →
 { SUCCEEDED | RETRY_WAIT → READY | FAILED | DEAD_LETTERED | CANCELED }`
A logical node may have multiple task attempts but at most one terminal
logical success.

### 6.3 Task attempt
`ENQUEUED → LEASED → { SUCCEEDED | FAILED | EXPIRED | CANCELED }`
An expired attempt may coexist historically with its replacement. A late
completion from an expired attempt must not corrupt the newer logical result
(attempt-scoped idempotency, M33).

### 6.4 Dependency invariant
A node becomes READY iff: the run is not terminal; every required predecessor
is logically SUCCEEDED; the node is not already logically terminal; and
resource/tenant admission has not permanently rejected it. Resource
availability controls **dispatch**, never dependency correctness.

### 6.5 Fan-in invariant
For `A→D, B→D, C→D`, D becomes ready only after **all** predecessors succeed.
The scheduler survives duplicated completion messages without decrementing the
dependency counter twice (completion events are idempotent per
(predecessor, successor) pair).

## 7. Timestamps and clocks

- In-process durations and scheduling latency: `std::chrono::steady_clock`.
- Durable/audit timestamps persisted to Postgres: wall clock (UTC), recorded
  by the writer. Benchmark reports state which clock each metric uses
  (`BENCHMARK_METHODOLOGY.md`).

## 8. What Phase 2 explicitly does NOT do

- No rewrite of Stagehand/Browserbase integrations in C++.
- No new public planner node types invented to manufacture concurrency.
- No claim that Redis alone provides exactly-once semantics.
- No distributed-scheduler performance claims until M15/M39 evidence exists.
- No default-behavior change for Phase-1 users until the M40 decision.
