# Phase 2 — Milestone 01: Phase-1 Source Reconciliation

**Recorded:** 2026-08-18
**Source SHA:** `5005768` (`main`, in sync with `origin/main`)
**Working tree:** clean (`git status --short` empty) — no uncommitted user work to protect.
**Git identity:** configured (Utkarsh Khajuria <utkarshkhajuria7@gmail.com>).

This document reconciles the checked-out Phase-1 source against the baseline
described in the Phase-2 master prompt. No production code was changed in this
milestone.

## 1. Baseline claims verified against the checked-out source

Every Phase-1 property the master prompt relies on was re-verified from the
actual working tree, not assumed:

| Claim | Evidence | Status |
| :--- | :--- | :--- |
| AI planner converts goal → validated plan, catalog derived from live registry | `features/workflows/lib/planner-service.ts`, `planner-catalog.ts`, `planner-types.ts`, `components/planner-start.tsx` exist | ✅ |
| Generated workflows become editable React Flow + Liveblocks state | `features/workflows/lib/convert-plan.ts`, `components/canvas.tsx` (`useLiveblocksFlow`) | ✅ |
| Nothing executes until explicit Run | `runWorkflowAction` in `features/workflows/actions.ts` is only invoked from the Run button path; lifecycle suite asserts no auto-execution | ✅ |
| Execution path: action → save graph → `tasks.trigger` → `runWorkflowTask` → toposort → sequential node walk | `actions.ts:112` `tasks.trigger<typeof runWorkflowTask>`; `tasks/run-workflow.ts:56` `task({...})`; `run-workflow.ts:73` `toposort` | ✅ |
| Browser nodes share one Browserbase/Stagehand session per run | `getStagehand()` single-instance pattern in `run-workflow.ts` | ✅ |
| Live-view gate: browser steps wait for the Live Browser panel | `run-workflow.ts:180` `waitForLiveView`, `:209` `hasBrowserStep`, `:212-214` eager open + wait | ✅ |
| In-page highlights for Act/Observe/Extract/Open URL/Agent | `features/workflows/lib/highlight-element.ts` + call sites in `nodes/act.ts`, `observe.ts`, `extract.ts`, `open-url.ts`, `agent.ts` | ✅ |
| Step metadata streamed to UI | `metadata.set/flush` + `publishSteps` in `run-workflow.ts`; `workflow-runs-provider.tsx` realtime subscription | ✅ |
| Stop cancels the run; Stagehand cleanup in `finally` | `cancelWorkflowRunAction` in `actions.ts`; `run-workflow.ts:302` `finally` → `closeStagehand()` at `:316` | ✅ |
| Final screenshot captured before session close | `captureFinalScreenshot()` `:137`, saved via `saveRunArtifact` `:306-308` inside `finally` | ✅ |
| Readable results UI (output, timing, final URL, screenshot, replay) | `components/run-results-dialog.tsx`, `step-output-view.tsx`, `inspector-panel.tsx`, `session-replay.tsx` | ✅ |
| Workflow editable after terminal states; rerun gets fresh identity/session | lifecycle suite scenarios; `runWorkflowAction` creates a new Trigger run per click | ✅ |
| Clerk org + plan gates server-enforced | `lib/auth.ts` (`readAuthWithRetry`, `resolveActiveOrgId`), `has({ plan: "pro" })` gates in `actions.ts` and replay route | ✅ |
| Neon + Drizzle is the DB layer | `lib/db/schema.ts`, `drizzle.config.ts`, `@neondatabase/serverless` | ✅ |
| Sentry present | `@sentry/nextjs` in deps, `next.config.ts` wiring | ✅ |

## 2. Node catalog (verified)

`features/workflows/nodes/node-registry.ts` contains exactly the seven node
types the master prompt lists:

`start`, `open-url`, `act`, `extract`, `observe`, `agent`, `send-email`

Executor files exist for all six action nodes under `features/workflows/nodes/`
and are registered in `node-executors.ts`. No new public node types will be
added to manufacture concurrency numbers (per master prompt §1.2).

## 3. Database state (verified)

`lib/db/schema.ts` currently defines exactly three tables:

- `workflows`
- `live_view_connections`
- `run_artifacts`

There is **no** Phase-2 durable model yet (workflow versions, engine-neutral
runs, node runs, task attempts, worker leases, idempotency records). These are
additive Phase-2 work (Milestone 19+). Schema management convention in this
repository is `drizzle-kit push` for prototyping; the `migrations/` directory
holds only `.gitkeep`.

## 4. Tests (verified present; counts to be re-measured in M02)

`npm test` runs three suites via `tsx`:

- `features/workflows/lib/convert-plan.test.ts` — conversion & editability
- `features/workflows/lib/integration.test.ts` — execution regression
- `features/workflows/lib/lifecycle.test.ts` — full lifecycle regression (11 scenarios)

Exact pass counts will be re-measured from a live run in Milestone 02, per the
master prompt's rule against copying counts from documentation.

## 5. Environment snapshot (recorded, not upgraded)

| Item | Value |
| :--- | :--- |
| Node.js | v20.20.2 |
| npm | 10.8.2 |
| next | 16.2.6 (non-stock; `node_modules/next/dist/docs/` is authoritative per AGENTS.md) |
| react / react-dom | 19.2.4 |
| @browserbasehq/stagehand | ^3.6.0 |
| @browserbasehq/sdk | ^2.15.0 |
| @trigger.dev/sdk | ^4.5.11 |
| @liveblocks/* | ^3.22.0 |
| @clerk/nextjs | ^7.5.12 |
| drizzle-orm / drizzle-kit | ^0.45.2 / ^0.31.10 |
| @xyflow/react | ^12.11.2 |
| typescript | ^5 |

## 6. C++ / distributed toolchain survey (for Milestone 04+ planning)

| Tool | Status |
| :--- | :--- |
| CMake | ✅ 4.2.1 |
| C++ compiler | ✅ Apple clang 21.0.0 (Xcode toolchain) |
| ninja / make | ✅ /opt/homebrew/bin/ninja, /usr/bin/make |
| Redis | ✅ redis-server 8.4.0 installed **and running** (`redis-cli ping` → PONG) |
| Docker | ❌ **not installed** — needed for isolated local Postgres (Milestone 18). Human action required at that milestone; local Redis already satisfies the Redis prerequisite. |
| vcpkg | ❌ not installed — Milestone 04 will decide dependency strategy (CMake FetchContent vs vcpkg manifest). |
| Platform | macOS (darwin 25.5.0, arm64 / Apple Silicon) |

## 7. Discrepancies found

None material. The checked-out source matches the master prompt's Phase-1
baseline description in every checked property. Two notes:

1. The prompt's run-path diagram shows `saveWorkflowGraph` as a distinct step;
   in the source this is the graph-persistence call inside `runWorkflowAction`
   (`features/workflows/data.ts`). Same semantics.
2. `npm run dev`/`build` pass `--webpack` (this Next.js build is configured
   against the webpack pipeline). No bearing on Phase-2 engine work.

## 8. Conclusion

The working tree at `5005768` is the verified Phase-1 baseline. It is clean,
committed, and pushed. Phase 2 may proceed to Milestone 02 (baseline
certification) without touching any uncommitted user work.
