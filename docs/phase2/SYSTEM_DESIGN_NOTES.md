# Phase 2 — System Design Interview Notes

Working notes for discussing the Phase-2 engine in a system-design interview. Each
section states the design, the invariant it protects, the failure mode considered, and
the *measured* tradeoff (from `RESUME_EVIDENCE.md` artifacts) rather than an aspirational
one.

## 1. The scheduler (local, in-process)

**Design.** A DAG of nodes with dependency edges. A sequential reference scheduler
(topological, single-threaded) is the correctness oracle. The concurrent scheduler adds
a thread-safe blocking ready queue + a bounded `std::jthread` worker pool. Nodes become
READY when their dependency counter hits zero; a dispatcher pops READY nodes and submits
them to the pool; completions decrement successors' counters under the state machine.

**Invariant.** A node is dispatched only when all predecessors are terminal-success, and
its terminal state is recorded exactly once. The state machine (M07) makes illegal
transitions impossible rather than checked-after-the-fact.

**Concurrency model.** One dispatcher thread owns the ready queue + dependency counters;
worker threads only execute tasks and report completion. This avoids a lock-per-node
design and keeps the hot path to a queue push/pop + an atomic decrement.

**Measured tradeoff.** Simulated I/O-bound work scales near-linearly (8.01x at 8
threads, efficiency ~1.00) because tasks sleep and threads overlap wall time. Synthetic
CPU work scales 5.57x at 8 threads (sub-linear: 8 cores shared with the dispatcher +
OS, and the cooperative task polls its stop_token). For microsecond-scale tasks,
coordination overhead dominates — concurrency only pays when tasks are not trivial.

## 2. Thread pool

**Design.** Bounded `std::jthread` pool (M09): fixed worker count, RAII join on
destruction, `std::stop_token`-based cooperative shutdown. No unbounded thread creation;
the pool size is the concurrency budget.

**Failure mode.** Shutdown while tasks run: the stop token is observed by cooperative
tasks (sleep in 2ms slices; CPU loop checks every 256 iters), so cancellation latency is
bounded by the check interval, not the task duration. Non-cooperative tasks run to
completion (documented; the stop_token overload exists precisely so tasks can opt in).

**Tradeoff.** Cooperative cancellation adds polling overhead to every task (why CPU
seq-vs-con speedup is understated). The price of prompt cancellation.

## 3. Distributed runtime: transport + durable store

**Design.** The scheduler loop is single-threaded per run and talks to two durable
dependencies: Redis Streams (task/result/control/event streams, consumer groups,
at-least-once delivery with pending-entry lists) and Postgres (runs, node_runs,
task_attempts, leases, idempotency ledger). The loop persists a node's terminal state
BEFORE unlocking successor counters.

**Invariant (the load-bearing one).** *Unlock only after durable logical success.* A
duplicate successful result cannot decrement successors twice, because the run store
rejects a second terminal completion (unique constraint + status check) and only a
first-time apply unlocks.

**Why Redis Streams + Postgres (not one or the other).** Streams give durable,
fan-out, at-least-once message delivery with consumer-group redelivery (the transport);
Postgres gives transactional, queryable, authoritative logical state (the source of
truth). The transport can lose/duplicate messages; the store makes the outcome
deterministic anyway.

**Measured tradeoff.** This is where the honest scaling ceiling lives: the
single-threaded result-consumption loop + per-message Redis round-trips bottleneck
distributed throughput. Measured: 2 workers ≈ 1.08–1.14x, 4 workers SLOWER than 1
(0.81–0.87x) for fine-grained synthetic tasks. The TS worker already parallelizes
internally via async, so more worker processes add claim/contention overhead without
adding parallelism. Scaling further needs a multi-threaded consumption path and/or
batched transport — a deliberate future item, not claimed as done.

## 4. Leases + heartbeats (failure detection)

**Design.** Two distinct liveness signals (M31): a worker *heartbeat* proves the
process is alive; a per-attempt *lease* proves a specific task is being worked. The
worker acquires a lease on claim and renews it while work runs. The scheduler scans for
expired leases and reaps them to `lease_expired` (terminal for the attempt, recoverable
for the node → re-dispatched as a new attempt).

**Invariant.** Lease expiry must not double-complete a node: `mark_attempt_lease_expired`
only applies to a still-running, non-terminal attempt, so a racing completion wins.

**Failure mode.** A killed worker stops renewing; its lease expires after the lease
duration; the scan reaps it; the node is re-dispatched. Measured recovery (SIGKILL
mid-task): median 6470ms end-to-end, bounded by lease duration + scan interval.

**Tradeoff.** Short leases → fast failure detection but risk reaping a slow-but-alive
worker. Mitigation (M31 test 12): a slow worker that still RENEWS is never reaped — the
lease, not a heartbeat timeout, is the reap trigger. A separate queue-wait lease covers
the dispatch→claim window generously so a live-but-slow-to-claim worker isn't reaped.

## 5. Idempotency + duplicate suppression

**Design.** Three layers (M33): (a) a durable idempotency ledger keyed on a logical
operation key (primary-key conflict = no-op; duplicate deliveries reuse the committed
response); (b) result dedupe by attempt id (M22) so a redelivered result is applied once;
(c) the at-most-once terminal-completion guard in the run store.

**Invariant.** No matter how many times a message is delivered, the logical effect is
applied exactly once. (This is at-least-once transport + at-most-once application — we
deliberately do NOT claim "exactly-once execution".)

**Failure mode tested.** Duplicate result storms, duplicate task delivery, and
stale/late results (from an expired attempt) are all injected in the 27-scenario suite;
none double-apply, double-unlock, or overwrite terminal state.

## 6. Browser affinity

**Design.** Browser nodes are a scarce, stateful resource (one live browser session).
The execution policy (M12) serializes all browser nodes in a run on a capacity-1
affinity key (one session per run), and a global browser-session capacity (M36) bounds
cross-run browser usage. Workers own the session for the run's lifetime and close it on
run end / graceful shutdown / cancel.

**Why.** Two concurrent `act`/`extract` calls on one browser session corrupt each other;
a session is per-run state. Affinity makes the serialization explicit and schedulable
rather than accidental.

**Tradeoff (measured, M37).** Affinity legitimately limits fairness: a tenant with a
deep browser backlog presents one browser task at a time, so its completion span is
proportional to tasks × duration even under perfect grant fairness. Fairness is
therefore guaranteed at slot-GRANT level (Jain's index 1.0), not completion spans.

## 7. Fairness + multi-tenant quotas

**Design.** M36: a shared `TenantQuotaGate` enforces per-org in-flight limits + global
resource-class capacities; a full gate DEFERS a node (left READY, re-examined next
iteration) rather than dropping it. M37 (opt-in): when a capped class is oversubscribed,
the gate grants the next slot to the least-served tenant (weighted least-served-first),
so a small tenant is served within a bounded number of grants.

**Invariant.** Demand is registered BEFORE the capacity check (no lost wakeup); demand
is cleared on grant (a granted tenant stops blocking others). The gate is the single
owner of its counters, locked per method — TSan-clean.

**Measured.** Jain's index 1.0 on slot grants for equal and unequal workloads; the slow
tenant holds the slot longer in aggregate but no tenant starves. Default remains FCFS
(backwards compatible); fairness is opt-in.

## 8. Retries + dead-lettering

**Design.** M32: per-resource-class retry policies (internal 3 attempts, browser 2,
external_io 1 — side effects need an idempotency strategy first). Transient failures
park the node in RETRY_WAIT with deterministic exponential backoff + jitter (seeded,
reproducible); permanent failures fail fast; exhausted retries dead-letter the node.

**Invariant.** A node in RETRY_WAIT is non-terminal, so the run stays alive until the
backoff elapses and the node is re-dispatched. Cancellation during backoff is honored.

**Tradeoff.** external_io gets NO retry by default — a deliberate safety choice, because
retrying a side-effecting node without an idempotency key can double-charge/double-send.

## 9. Crash recovery + restart recovery

**Design.** Worker crash (M34): lease expiry → reap → re-dispatch on a surviving worker.
Scheduler crash (M35): the run's logical state (node statuses, dependency counters,
attempt numbers, retry due-times, in-flight resource slots, durable cancel request) is
reconstructed from Postgres on restart (`resume=true`), and the consumer's pending
(unacked) result messages are drained so no result is lost across the restart.

**Invariant.** Restart never resurrects a terminal run; reconstruction is idempotent.

**Measured.** Worker-crash recovery median 6.5s (lease-bounded). Scheduler restart
completes the run to a consistent terminal outcome with no lost/duplicated work.
Chaos-tested: 2.5s Redis + 2.0s Postgres outages mid-run both recover to 100% task
completion via bounded reconnect backoff.

**What it is NOT.** Single-process recovery, not multi-instance HA. There is a
detection/recovery window; do not claim "zero downtime".

## 10. Cancellation

**Design.** M30 end-to-end: cancel is idempotent (first request wins, reason + timestamp
preserved), durable (stamps `cancel_requested_at` on the run row), and propagated
(control message to workers to abort in-flight attempts + close browser sessions). The
durable store + late-result rule are the backstop for workers that miss the control
message.

**Races handled (all tested).** Cancel-before-start (run starts already-canceled);
cancel racing completion (terminal no-op after finalization); late success after
terminal-canceled is rejected; stop-after-terminal is a no-op; repeated Stop is
idempotent.

## 11. Cross-cutting: clocks, trust boundaries, observability

- **Clocks.** `steady_clock` for all in-engine durations (monotonic, never NTP-jumped);
  wall-clock UTC ms only at durable/audit boundaries. Every metric states which clock
  backs it.
- **Trust boundaries.** gRPC input is validated (size limits + identifier charset)
  BEFORE quota admission and any durable write. Service-to-service auth is a
  constant-time-compared engine token; secrets are redacted from structured logs.
- **Observability.** Structured JSON logging (C++ + TS) with correlation/trace ids
  propagated scheduler→Redis→worker; Prometheus metrics on a loopback endpoint.

## 12. The measured tradeoffs I'd lead with (honest ones)

1. Concurrency pays for I/O-bound work (near-linear), is sub-linear for CPU work, and
   is negative for microsecond tasks — coordination overhead is real.
2. The distributed runtime does NOT scale linearly with worker count for fine-grained
   tasks (4 workers slower than 1, measured) — the single-threaded consumption loop is
   the bottleneck, and I'd describe the fix (multi-threaded consumption / batched
   transport) as future work.
3. Fairness is guaranteed at slot-grant level, not completion spans, because browser
   affinity serializes a tenant's browser backlog — a deliberate, documented limitation.
4. At-least-once transport + at-most-once logical application, not "exactly once".
5. Single-process restart recovery, not HA / "zero downtime".
