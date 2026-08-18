# Phase 2 — Benchmark Methodology

**Status:** binding rules for all Phase-2 performance work (Milestone 03).
Every performance number that leaves the repository (README, report, resume
evidence) must satisfy this methodology. Numbers that do not are labeled
*diagnostic* and never published as results.

## 1. No-fabrication rules

1. **Never choose target numbers in advance.** Measure first, preserve raw
   evidence, then derive wording from the evidence.
2. Every published number must have a **reproducible command** and a **raw
   result artifact** stored under `engine/benchmarks/results/` (structure
   defined in M15).
3. **Never compare Debug C++ against Release C++** as a performance claim.
   Release-vs-Release (or an explicitly labeled configuration comparison)
   only.
4. Every result record must capture: hardware, OS, compiler + version, build
   mode, thread count, worker count, workload definition, RNG seed, warmup
   count, sample count, and commit SHA.
5. Use **repeated trials**; store **raw samples**, not only averages. Report
   median + percentiles (p50/p95/p99) and the observed spread. If a metric is
   noisy, report the noise — never hide it.
6. **Separate workload classes** and never conflate them:
   - *Scheduler-only synthetic* (in-process C++ DAGs, no external I/O).
   - *Simulated I/O-bound* (sleep-based tasks) — always labeled
     "simulated I/O-bound workload".
   - *Synthetic CPU* (compute tasks) — always labeled "synthetic CPU
     workload".
   - *End-to-end external* (real Browserbase/LLM) — separate campaign (M39),
     never mixed into scheduler speedup claims.
7. **Never generalize** a synthetic speedup into a browser-automation
   speedup. Browser end-to-end performance is dominated by network + LLM
   latency, not scheduling.
8. No shared-CI-runner timing is ever final performance evidence. Final
   numbers come from the local machine recorded in the result artifact.
9. A result becomes **evidence-grade** only when it identifies workload, build
   mode, hardware, sample count, and commit SHA together.

## 2. Metric definitions (used consistently in code, reports, README)

| Metric | Definition | Notes |
| :--- | :--- | :--- |
| **Workflow makespan** | `terminal_ts − run_start_ts` | Queue wait before admission is excluded unless the report says so |
| **Ready-to-dispatch latency** | `task_enqueued_for_dispatch_ts − node_became_ready_ts` | Scheduler/resource wait after dependency satisfaction |
| **Queue latency** | `worker_started_ts − task_transport_enqueue_ts` | Transport + worker availability |
| **Node execution latency** | `worker_completed_ts − worker_started_ts` | Excludes retry wait; one attempt |
| **Logical node latency** | `logical_terminal_ts − logical_first_ready_ts` | May include retries and lease recovery |
| **Throughput** | Clearly defined numerator: logical tasks completed/s or workflows completed/min | Never mix attempts with logical tasks |
| **Speedup** | `sequential_reference_makespan / concurrent_makespan` | Identical DAG, durations, machine, build config, trial protocol |
| **Parallel efficiency** | `speedup / worker_or_thread_count` | Label which count is used |
| **Recovery time** | `replacement_attempt_started_ts − failure_injection_ts` | Report lease duration alongside (bounds detection) |
| **Cancellation latency** | (a) request → scheduler marks canceled; (b) request → worker/browser actually stops | Report both; never hide the difference |
| **Duplicate suppression** | Duplicates injected, duplicate attempts observed, logical commits accepted, duplicate commits rejected, external side effects observed in the controlled sink | Full accounting table |
| **Fairness** | Jain's index `J = (Σxᵢ)² / (n·Σxᵢ²)` plus max/median queue wait per tenant | One scalar never hides starvation |
| **Memory** | Peak RSS, process-level | Record method + platform |
| **CPU** | Process CPU and/or machine utilization | Only when collection is reliable on the platform; never derived from wall time |

## 3. Clock discipline

- Durations/latencies inside the engine: `std::chrono::steady_clock`.
- Durable audit timestamps: wall clock (UTC) at the writer.
- Benchmark reports state which clock backs each metric.

## 4. Result artifact format (binding from M15)

Each benchmark run produces a directory:

```text
engine/benchmarks/results/<YYYYMMDD-HHMMSS>_<slug>_<short-sha>/
├── manifest.json      # hardware, OS, compiler, build mode, counts, seed, commit
├── samples.jsonl      # one raw sample per line (never only aggregates)
├── summary.json       # derived statistics (median, p95, p99, min, max)
└── command.txt        # exact reproducible command line
```

README prose may quote a number only by referencing its artifact directory.

## 5. Publication gate

A number may appear in README/resume evidence only if:

1. Its artifact directory exists in-repo at the quoted commit.
2. Its workload class label is attached.
3. Its trial count and spread are stated.
4. It does not extrapolate beyond the measured workload class.
