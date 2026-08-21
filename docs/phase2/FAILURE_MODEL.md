# Phase 2 — Failure Model

**Status:** design baseline (Milestone 03). Each assumption here becomes a
test requirement in the milestone that implements the affected component.

## 1. Transport & delivery assumptions

| Assumption | Consequence | Mitigation (milestone) |
| :--- | :--- | :--- |
| Messages may be duplicated (Redis redelivery, worker re-publish, network retry) | Duplicate task deliveries and duplicate result envelopes are normal, not bugs | Attempt-scoped idempotency keys; unique constraints; duplicate completion events are no-ops (M33) |
| A worker may disappear after receiving a task | A leased task can go silent with no failure message | Time-bounded leases + heartbeats; expiry triggers a replacement attempt, never an inference of death from slowness alone (M31) |
| Acknowledgements may be delayed or lost | A task may complete while its attempt is already marked EXPIRED | Late results from expired attempts are rejected for logical-state mutation but recorded for audit; the newer attempt owns the logical result (M33, M34) |
| Redis operations may fail transiently | Enqueue/ACK/claim calls can error mid-flight | Bounded retries with backoff at the transport boundary; failures surface as attempt failures, not silent loss (M21) |
| Postgres operations may fail transiently | Durable writes can error | Parameterized SQL in transactions; retryable-error classification; a failed durable write fails the attempt loudly (M19+) |

## 2. Process assumptions

| Assumption | Consequence | Mitigation |
| :--- | :--- | :--- |
| The scheduler can restart at any time | In-memory ready queues and lease tables are lost | Durable reconciliation on startup: Postgres is the authority for logical state; Redis pending entries are re-claimed; no run is silently dropped or double-driven (M35) |
| A TS worker process can crash mid-node | Browser session may be orphaned; node attempt incomplete | Lease expiry → replacement attempt; browser resource treated as lost (no claimed session continuation); Stagehand cleanup is best-effort via Browserbase session TTL (M34) |
| The Next.js server can restart | In-flight gRPC submissions can be interrupted | Run submission is idempotent per (workflow version, run request id); the control plane re-queries orchestrator state instead of assuming (M27) |

## 3. Execution semantics assumptions

- **At-least-once delivery** is the baseline. Exactly-once is achieved only at
  the *logical result commit* layer via idempotency keys — never assumed from
  the transport.
- **Irreversible external side effects** (sending an email, mutating a web
  page) have an ambiguity window: if a worker dies after performing the side
  effect but before publishing the result, a retry may repeat the side effect.
  This window is documented per node type (M32) and is not hidden:
  - `send-email`: at-least-once delivery possible on crash-retry.
  - browser nodes: page mutations may repeat; acceptable for the product's
    automation semantics, documented.
- **Cancellation races completion.** A cancel request and a success envelope
  can cross in flight. Resolution rule: the first durable logical-state
  transition wins; the loser is recorded as a no-op event. A run marked
  CANCELED never later reports SUCCEEDED (M11 local, M30 distributed).
- **Slow ≠ dead.** A worker holding a lease and heartbeating is alive even if
  its node execution is slow. Only lease expiry (missed heartbeats past the
  bound) triggers recovery (M31).

## 4. Browser-session failure rules

1. One owning worker per browser affinity key at a time (M25).
2. Owning worker dies → browser resource **lost**; dependent browser nodes
   fail or retry per policy on a fresh session only if the run policy allows
   session re-creation; by default the run fails the affected branch rather
   than silently restarting a session mid-run.
3. Live-view gate, final screenshot capture, and session cleanup in the
   finally-equivalent path are preserved in Evo mode (M25).
4. No concurrent mutation of one page/context, ever (resource capacity 1,
   M12).

## 5. Data-integrity rules

- Terminal logical states are immutable: `SUCCEEDED`, `FAILED`,
  `DEAD_LETTERED`, `CANCELED` on a node; `SUCCEEDED`, `FAILED`, `CANCELED` on
  a run. Late events cannot reopen them.
- A rerun never mutates a historical run row; it inserts a new run.
- Workflow versions are immutable once a run references them (M20); editing
  the canvas creates a new version for the next run.
- Secrets are never stored in run/task/attempt tables; workers read secrets
  from their own environment (M19, M23).

## 6. Failure-injection test surface (M34, M39)

The chaos campaign must demonstrate, with recorded evidence:

- Worker kill mid-attempt → lease expiry → replacement attempt → correct
  logical result (no duplicate success, no lost success). **Demonstrated in
  M34** (`engine/tests/crash_recovery_test.cpp`, real SIGKILL of the
  lease-holding worker process group; raw samples committed under
  `engine/bench-results/m34/`).
- Duplicate delivery of a task envelope → single logical execution.
- Duplicate delivery of a result envelope → single logical commit.
- Scheduler restart with in-flight work → reconciliation resumes without
  double-driving completed nodes.
- Cancellation during dispatch, during execution, and racing completion.
- Redis unavailability window → bounded degradation, loud failure, recovery.

## 7. Verified crash-recovery behavior (M34)

Recovery is demonstrated — not merely designed — and differs by resource
class. Both paths share the same mechanism (lease expiry → reap →
re-dispatch as a NEW attempt; the killed attempt is `lease_expired`, never
`failed`, and reaping consumes no retry budget), but the *resource* outcome
differs:

### 7.1 Synthetic / non-browser work (resource class INTERNAL)

- Fault: `SIGKILL` to the lease-holding worker's entire process group
  (`npx tsx` tree) mid-attempt on a `bench:sleep 4000ms` node, 2-worker
  fleet, short leases (1500ms, renew 400ms, scan 100ms).
- Observed sequence (3/3 trials, wall-clock UTC ms from the durable store):
  1. the killed attempt stops renewing; the scheduler's expired-lease scan
     reaps it to `lease_expired` (reap latency ≈ lease duration + scan
     interval; measured ≈ 1.5–1.6s with the test cadence),
  2. the node is re-dispatched as attempt 2 (reassign latency ≈ 0.1s),
  3. a DIFFERENT surviving worker acquires the new attempt's lease and
     completes it; the run succeeds.
- No logical task lost; exactly 2 attempts for the killed node; the node's
  single terminal success is the replacement attempt's output. A late result
  from the killed attempt (had it published before dying) is rejected by the
  late-result rule + M33 ledger and cannot corrupt state.
- Recovery latency (SIGKILL → run complete) is workload-bound: the
  replacement must re-run the full task. Diagnostic samples: median ≈ 6.5s
  for the 4s-sleep workload (see the committed raw artifact directory; these
  are single-local-stack diagnostic numbers, not benchmark claims).

### 7.2 Browser-affinity work (resource class BROWSER)

- Same reap/re-dispatch mechanism, plus the browser resource rule from §4:
  the killed worker's browser slot (capacity-1 affinity key) is RELEASED on
  reap, so the replacement attempt can acquire it.
- There is NO transparent session continuation: the replacement attempt
  starts a FRESH browser session (the old Browserbase session is treated as
  lost; cleanup is best-effort via Browserbase TTL). Verified in
  `engine/tests/distributed_run_loop_test.cpp` test 19: after killing the
  lease-holding worker mid browser-affinity chain, the downstream browser
  node runs on the freed capacity-1 slot, the killed node has exactly 2
  attempts, and the run succeeds.
- Side effects already performed in the lost session (page mutations) are
  NOT rolled back; the fresh session re-executes the node from scratch
  (documented ambiguity, §3).

### 7.3 What recovery does NOT cover (yet)

- Scheduler-process crash mid-run: durable reconciliation is M35.
- Whole-fleet outage (all workers dead): tasks wait in the queue until a
  worker returns; queue-wait leases are deliberately generous so a
  slow-to-claim live worker is not reaped (M31 two-phase lease).

## 8. What we do NOT claim

- No transparent browser-session continuation after worker crash.
- No exactly-once external side effects (email/page mutations).
- No fault tolerance beyond what is implemented and tested at the milestone
  that claims it.
