# Phase 2 — Release Checklist

The gate for declaring Phase 2 complete and releasing the dual-engine product.
Every box below was executed on the reference machine (Apple M2, 8 cores,
Darwin arm64, Apple clang 21) during Milestone 40; results are recorded in
`PROGRESS.md` under M40. Re-run this checklist from a clean checkout before
any release.

## 1. Quality gates (must all be green)

- [ ] `npm test` — all Node suites incl. 9/9 legacy-vs-evo parity scenarios
- [ ] `npm run typecheck`
- [ ] `npm run lint`
- [ ] `npm run build` — production build
- [ ] `ctest --test-dir engine/build --output-on-failure` — 31/31 (Release)
- [ ] `ctest --test-dir engine/build-asan --output-on-failure` — 31/31 (ASan+UBSan)
- [ ] `ctest --test-dir engine/build-tsan --output-on-failure` — 31/31 (TSan)
- [ ] `scripts/secret-scan.sh` — clean
- [ ] `scripts/phase2/bench-smoke.sh` — PASS (benchmark harness smoke)
- [ ] Distributed integration legs pass inside ctest (distributed_e2e,
      crash_recovery, scheduler_restart, m39_scaling, m39_chaos) with
      `scripts/phase2/up.sh` infra running; they skip cleanly when it is down.

## 2. Evidence gates (no fabricated number survives)

- [ ] Every performance statement in README / report / resume registry traces
      to a row in `RESUME_EVIDENCE.md` with command + raw artifact + commit SHA.
- [ ] M39 campaign artifact present and checksummed
      (`engine/benchmarks/results/<ts>_m39_<sha>/checksums.sha256` verifies).
- [ ] Benchmark SHA vs release SHA: the only code delta is the benchmark
      harness itself (M39 test/campaign files) + docs — no engine-core change,
      so the frozen-SHA evidence remains valid for the release commit.
- [ ] No claim of "exactly once", "zero downtime", or "linear scaling"
      anywhere in shipped docs (grep before release).
- [ ] Scheduler-only synthetic numbers are never presented as browser
      end-to-end speedups.

## 3. Compatibility gates (Phase 1 preserved)

- [ ] Default engine is legacy: `EXECUTION_ENGINE` unset ⇒ Trigger.dev path.
- [ ] Flag is fail-closed: only the exact string `evo` selects the Evo engine.
- [ ] Legacy `runWorkflowTask` remains callable and unmodified in behavior.
- [ ] AI planner still requires explicit Run; generated graphs stay editable.
- [ ] Clerk auth/org + Pro-plan gates remain server-enforced before any engine.
- [ ] Live-view gate, final screenshot, replay, rerun semantics intact for
      both engines (parity suite).

## 4. Release decisions

- **Default engine:** `legacy` (Trigger.dev). Switching to Evo is an explicit
  operator action: set `EXECUTION_ENGINE=evo` plus `EVO_SCHEDULER_ADDR` (and
  `EVO_ENGINE_TOKEN` when scheduler auth is enabled), run the scheduler
  service + at least one worker, and have the Phase-2 schema migrated on the
  engine's Postgres. Rollback = unset the flag; in-flight Evo runs are
  recoverable via scheduler restart recovery, but the supported rollback path
  is to let them finish or cancel them via Stop.
- **Push/merge policy:** Phase-2 work stays on the `phase2` branch and is
  **not pushed or merged** without explicit user authorization. No force
  push, no history rewrite, ever.
- **Remote migrations:** Phase-2 migrations are applied automatically only to
  the isolated local Postgres. Applying them to a shared/remote (Neon)
  database requires explicit human approval — the scripts can never reach
  `DATABASE_URL`.

## 5. Human-intervention points (documented, not automated)

| Action | Who | Where documented |
| :--- | :--- | :--- |
| Provide API keys/secrets in `.env.local` (never in chat) | Human | `.env.example`, `SECURITY.md` |
| Install Docker / CMake / C++20 compiler | Human | `docs/phase2/BUILDING_ENGINE.md`, `LOCAL_INFRA.md` |
| Approve remote/shared database migration | Human | `LOCAL_INFRA.md` |
| Authorize `git push` / merge of `phase2` | Human | `PROGRESS.md` git contract |
| Authorize paid external (Browserbase) benchmarking | Human | `BENCHMARK_METHODOLOGY.md` |

## 6. Known limitations accepted at release

Recorded in full in `PHASE-2-IMPLEMENTATION-REPORT.md` §8 and
`RESUME_EVIDENCE.md` (E4, E14, E15): distributed worker scaling ceiling for
fine-grained tasks, single-scheduler-process recovery (not HA), loopback-only
service bindings without TLS, and no browser end-to-end performance claim.
