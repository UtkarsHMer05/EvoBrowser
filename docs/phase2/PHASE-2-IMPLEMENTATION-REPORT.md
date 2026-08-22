# Phase 2 — Implementation Report

**Status:** Phase 2 complete (M01–M40). This report is the truthful, evidence-backed
summary of what was built, how it was validated, and what is explicitly NOT claimed.
Every performance number here is derived from a raw artifact captured on the reference
machine; none was chosen in advance. See `RESUME_EVIDENCE.md` for the claim-by-claim
registry and `BENCHMARK_METHODOLOGY.md` for the binding rules these numbers satisfy.

## 1. What Phase 2 is

Phase 2 adds a **C++20 concurrent DAG scheduler + distributed worker runtime** beneath
the existing Phase-1 TypeScript/Next.js product, behind an opt-in, fail-closed feature
flag. Phase 1 (Trigger.dev execution) remains the default and is untouched; Phase 2 is
a second engine selectable via `EXECUTION_ENGINE=evo`.

The two engines share one engine-neutral adapter interface (`start/cancel/query`) and
one durable audit schema, so a run's provenance (`engine = legacy | evo`) is recorded
and queryable regardless of which engine executed it.

## 2. Milestone summary (M01–M40)

All 40 milestones are DONE. The full table with commit SHAs lives in `PROGRESS.md`.
Grouped by theme:

- **Baseline & architecture (M01–M03):** reconciled the real Phase-1 source, certified
  and froze the Phase-1 behavioral baseline, and defined Phase-2 architecture +
  invariants + this progress scaffold.
- **C++ scheduler core (M04–M15):** reproducible C++20 toolchain; canonical DAG model;
  deterministic sequential reference scheduler; state machines + dependency counters;
  thread-safe blocking ready queue; bounded `std::jthread` worker pool; local
  concurrent dependency-aware scheduler; cooperative cancellation + graceful shutdown;
  resource classes + browser-affinity policy; evidence-grade timestamps/counters;
  sanitizer + concurrency-stress hardening; first local benchmark corpus.
- **Contract & service (M16–M17):** shared Protobuf/gRPC execution contract; C++
  scheduler service over gRPC (SubmitRun/CancelRun/GetRun/Health).
- **Durable distributed runtime (M18–M26):** isolated local Redis + Postgres infra;
  additive Phase-2 Drizzle schema + migrations; immutable workflow versions +
  optimistic concurrency; Redis Streams transport; task/result envelope semantics;
  TypeScript distributed worker; reuse of existing interpolation + node executors in
  workers; distributed browser-session ownership + live-view parity; persist-results-
  then-unlock distributed loop.
- **Product integration (M27–M29):** Next.js execution-engine abstraction + feature
  flag; engine-neutral Evo run events + realtime frontend transport; UI parity for Evo
  runs (9/9 legacy-vs-evo parity scenarios).
- **Reliability (M30–M35):** end-to-end cancellation (app→scheduler→queue→worker→
  browser); worker registry + leases + heartbeats; node-level retry policy + backoff +
  jitter + dead-lettering; idempotency + duplicate suppression; worker crash recovery
  + failure injection; scheduler restart recovery + durable reconciliation.
- **Multi-tenancy (M36–M37):** per-tenant quotas + backpressure; fair scheduling +
  starvation resistance (opt-in weighted least-served-first).
- **Observability, security, CI (M38):** structured JSON logging with secret redaction
  (C++ + TS); trace/correlation id propagation; Prometheus metrics; service-to-service
  engine-token auth (constant-time compare); input size limits + identifier validation;
  GitHub Actions CI (no paid keys); secret scanning; SECURITY.md threat model.
- **Evidence campaign (M39):** final reproducible performance, scaling, and chaos
  campaign (this report's numbers come from it).
- **Finalization (M40):** this audit, documentation, release checklist, and the resume
  evidence registry.

## 3. Architecture (as implemented)

```
Next.js app (Phase 1, default)
  └─ execution-engine.ts  EXECUTION_ENGINE=legacy|evo  (fail-closed; default legacy)
       ├─ legacy adapter ──► Trigger.dev runWorkflowTask   (Phase 1, unchanged)
       └─ evo adapter ─────► gRPC ControlService (C++ evo-scheduler-server)
                                └─ DistributedRunLoop (one per run)
                                     ├─ Redis Streams transport (task/result/control/event)
                                     ├─ Postgres durable run store (runs/nodes/attempts/leases)
                                     └─ TS workers (worker/src/main.ts)
                                          ├─ synthetic bench:* executors (tests/benchmarks)
                                          └─ product node executors + Stagehand/Browserbase
```

Key invariants enforced by code + tests:
- **Unlock only after durable logical success.** The loop persists a node's terminal
  state to Postgres BEFORE decrementing successor dependency counters, so a duplicate
  successful result can never unlock successors twice.
- **At-most-once terminal completion.** The run store rejects a second terminal
  completion for an already-terminal node (unique constraint + status check).
- **Leases + heartbeats bound failure detection.** A killed worker's attempt lease
  expires; the scheduler reaps it and re-dispatches the node as a new attempt.
- **Idempotency ledger.** Logical operation keys are claimed durably (primary-key
  conflict = no-op), so duplicate deliveries reuse the committed response.
- **Browser affinity.** All browser nodes in one run serialize on a capacity-1
  affinity key (one browser session per run); a global browser-session capacity bounds
  cross-run browser usage.
- **Clock discipline.** `steady_clock` for all in-engine durations; wall-clock UTC ms
  only at durable/audit boundaries.

## 4. Validation summary (final SHA)

All gates green on the reference machine (Apple M2, 8 cores, Darwin arm64,
Apple clang 21):

| Gate | Command | Result |
| :--- | :--- | :--- |
| Node tests (incl. M29 parity 9/9) | `npm test` | ✅ exit 0 |
| TypeScript | `npm run typecheck` | ✅ exit 0 |
| Lint | `npm run lint` | ✅ exit 0 |
| Production build | `npm run build` | ✅ exit 0 |
| C++ Release | `ctest` (engine/build) | ✅ 31/31 |
| C++ ASan+UBSan | `ctest` (engine/build-asan) | ✅ 31/31 |
| C++ TSan | `ctest` (engine/build-tsan) | ✅ 31/31 |
| Distributed integration | included in ctest (distributed_e2e, crash_recovery, scheduler_restart, m39_scaling, m39_chaos) | ✅ pass |
| Secret scan | `scripts/secret-scan.sh` | ✅ clean |
| Bench harness smoke | `scripts/phase2/bench-smoke.sh` | ✅ PASS |

The 31-test C++ suite includes the 27-scenario `distributed_run_loop_test` (duplicate
result, identity validation, failure path, malformed payload, cancellation races, lease
expiry/reassignment, retry/dead-letter, restart recovery, tenant isolation, starvation
resistance) plus transport/envelope/retry, Redis/Postgres integration, distributed E2E,
crash recovery, scheduler restart, and the M39 scaling + chaos tests.

## 5. Measured performance & reliability (from M39, frozen SHA `b4b651d`)

Artifact: `engine/benchmarks/results/20260822-141648_m39_b4b651d/` (gitignored by
convention; reproducible via `scripts/phase2/m39-campaign.sh`).

- **Local scheduler, simulated I/O-bound — speedup vs sequential reference:**
  t2 2.01x, t4 4.01x, **t8 8.01x** (parallel efficiency ~1.00). Near-linear.
- **Local scheduler, synthetic CPU — thread scaling (concurrent t1 vs tN):**
  t2 2.01x, t4 3.75x, **t8 5.57x**. (seq-vs-con not reported for CPU; see caveat in
  RESUME_EVIDENCE.md.)
- **Distributed worker scaling (wide DAG, bench:sleep):** 1w→2w ≈ 1.08–1.14x;
  **4 workers is slower than 1** (0.81–0.87x). Preserved and explained: the TS worker
  already parallelizes tasks internally via async, so extra worker processes add
  claim/contention overhead without adding parallelism for fine-grained synthetic
  tasks; the bottleneck is the single-threaded result-consumption loop + Redis
  round-trips. This is an honest architecture finding, not a defect.
- **Infrastructure outage chaos (Appendix T F09–F12):** Redis outage (2.5s pause) and
  Postgres outage (2.0s pause) both injected mid-run → run reached a terminal state and
  **recovered to success 30/30 tasks** in both cases, via bounded reconnect backoff.
- **Worker crash recovery (SIGKILL lease-holder):** recovery latency (SIGKILL → run
  complete) min 6413 / median 6470 / max 6489 ms over 3 trials.
- **Fair scheduling:** Jain index — equal workload span 0.995 / served 1.0; unequal
  workload span 0.997 / served 1.0 (no starvation).

## 6. What is explicitly NOT claimed

- **No "exactly once" execution.** The system provides at-least-once transport with
  at-most-once *logical* application (dedupe + idempotency + terminal-completion
  guard). Side-effecting external nodes still need an idempotency strategy.
- **No "zero downtime."** Scheduler restart recovery reconstructs and resumes runs, but
  there is a detection/recovery window (bounded by lease scan interval).
- **No "linear scaling" for the distributed runtime.** Local scheduler I/O-bound
  scaling is near-linear in threads; distributed worker scaling is NOT linear beyond
  ~2 workers for fine-grained synthetic tasks (measured regression preserved above).
- **No browser end-to-end speedup.** Local scheduler numbers are scheduler-only
  synthetic and must never be generalized to browser automation, which is dominated by
  network + LLM latency. No paid Browserbase campaign was run (no authorized keys).
- **No CI-runner timing as evidence.** Final numbers come from the local reference
  machine only; CI smoke-tests the harness without asserting timing.

## 7. Phase-1 preservation (checklist)

- [x] AI planner still requires explicit user Run before execution.
- [x] Generated graph remains editable React Flow / Liveblocks state.
- [x] Legacy Trigger.dev engine remains available (it is the default).
- [x] Clerk auth/org enforcement remains server-side.
- [x] Agent Pro-plan gate remains fail-closed.
- [x] Live browser credentials remain server/worker only.
- [x] Legacy Run/Stop/results/replay/rerun behavior not regressed (M29 parity 9/9).
- [x] Existing node registry/executors not duplicated (workers reuse them via adapter).
- [x] No test removed merely because Phase 2 changed an internal representation.

## 8. Known limitations

- Distributed worker scaling ceiling for fine-grained synthetic I/O tasks (~2 workers);
  a multi-threaded result-consumption path would be needed to scale further.
- Engine token is a single shared service secret, not per-org (tenant isolation is the
  app's Clerk auth + per-org quota). No TLS on the gRPC channel (loopback today).
  Redis has no auth by default (loopback binding). All documented in SECURITY.md §5.
- Per-node ready-to-dispatch/queue-latency percentiles are captured by the local
  campaign; the distributed campaign reports makespan/throughput/retries/errors.
- Benchmark results directories are gitignored by convention (matching M34) and
  referenced from PROGRESS.md / this report; they are reproducible via the campaign
  script at the recorded SHA.

## 9. Reproducibility

- Local scheduler + scaling + chaos: `scripts/phase2/m39-campaign.sh` (freezes SHA,
  rebuilds, runs all legs, assembles one checksummed results dir + REPORT.md).
- Full C++ suite: `cmake --build engine/build && ctest --test-dir engine/build`.
- Node suite: `npm test && npm run typecheck && npm run lint && npm run build`.
- Local infra: `scripts/phase2/up.sh` (Redis :6390, Postgres :5433), migrations via
  `scripts/phase2/migrate-local.sh`.
