# Phase 2 — Certified Phase-1 Behavioral Baseline

**Certified SHA:** `39fa8e1` (branch `main`; M01 record commit)
**Certified:** 2026-08-18
**Certifying branch for Phase-2 work:** `phase2` (created from this baseline)

This document freezes the Phase-1 product as the regression gate for all
Phase-2 work. Any Phase-2 change that breaks a behavior listed here is a
regression and must be fixed before the milestone can close.

## 1. Automated gates (measured, not copied)

All commands run from the repository root at the certified SHA.

| Gate | Command | Result |
| :--- | :--- | :--- |
| Unit/regression suites | `npm test` | ✅ PASS — 28/28 across 3 suites |
| — conversion & editability | `tsx features/workflows/lib/convert-plan.test.ts` | ✅ 7/7 |
| — execution integration | `tsx features/workflows/lib/integration.test.ts` | ✅ 10/10 |
| — lifecycle regression | `tsx features/workflows/lib/lifecycle.test.ts` | ✅ 11/11 (13 runs simulated) |
| Type check | `npm run typecheck` (`tsc --noEmit`) | ✅ exit 0 |
| Lint | `npm run lint` (`eslint`) | ✅ exit 0 |
| Production build | `npm run build` (`next build --webpack`) | ✅ all 15 routes compiled |

## 2. Protected behaviors (the regression contract)

These behaviors are frozen. Phase 2 builds **beneath and beside** them; it does
not rewrite them.

1. **Explicit Run only.** Nothing executes because the AI planner returned a
   plan. Execution starts only when the user presses Run
   (`runWorkflowAction` → `tasks.trigger`).
2. **Generated graphs are ordinary editable state.** AI plans become React Flow
   + Liveblocks CRDT state through the same mutations manual edits use; no
   separate "AI editor" exists.
3. **Pre-flight validation.** Run validates exactly one Start, connected nodes,
   and acyclicity client- and server-side before persisting the snapshot.
4. **Sequential legacy engine.** `runWorkflowTask` topologically sorts and walks
   nodes sequentially, driving **one shared Browserbase/Stagehand session per
   run**. This engine remains available throughout Phase 2.
5. **Live-view gate.** Runs containing browser steps hold execution until the
   Live Browser panel reports connected (1s poll, 60s bounded timeout);
   non-browser runs skip the gate.
6. **Live highlights.** Act/Observe/Extract/Open URL/Agent inject a real DOM
   overlay visible in the live video stream; highlight failures never break a
   run.
7. **Stop semantics.** Stop cancels only the live run; Stagehand cleanup runs
   in the task's `finally` path on every exit route.
8. **Final screenshot.** Captured before the session closes and stored as a run
   artifact, served via the org-authorized screenshot route.
9. **Readable results.** The results popup renders human-readable outputs (no
   raw JSON), opens only on terminal status, and includes timing, final URL,
   screenshot, and replay links.
10. **Run isolation.** Every run gets a fresh run identity and fresh browser
    session; replays resolve the selected historical run's recording; node
    status paint always reflects the newest run.
11. **Auth & tenancy.** Clerk auth/org checks are server-side on every route
    and action; the Agent node and replays are Pro-gated fail-closed; secrets
    never reach the client.
12. **Resilience behaviors.** `readAuthWithRetry` transient-miss retry,
    planner provider retries (408/429/5xx, 3 attempts, backoff), and
    storage-loaded gating before Liveblocks mutations.

## 3. Current execution model (the thing Phase 2 improves)

- One Trigger.dev task per run; nodes execute **sequentially** in topological
  order.
- Independent DAG branches are **not** scheduled concurrently.
- One browser session is shared by all browser nodes in a run.
- No immutable workflow-version history, no node-level retry model, no worker
  fleet, no leases/heartbeats, no custom crash recovery, no idempotency ledger,
  no tenant fairness/backpressure, no scheduler benchmark corpus.

These are the Phase-2 opportunities. They are addressed without regressing
section 2.

## 4. Behaviors covered only manually (not by automated tests)

The automated suites simulate the Trigger/Browserbase/Liveblocks boundaries.
The following require live external services and were last verified manually
during Phase-1 development; they remain protected and must be smoke-tested at
integration milestones (M27–M29) before release:

- Real Browserbase live-view connection and highlight visibility in-stream.
- Real HLS replay playback through the Pro-gated proxy.
- Real Clerk sign-in / org switching / billing gates.
- Real Liveblocks multi-user collaboration (two browsers, one room).
- Real Resend email delivery from the Send Email node.
- Real AI planner generation against the configured provider.

## 5. External dependencies the baseline assumes

Clerk, Neon, Browserbase, Trigger.dev, Liveblocks, Resend, an OpenAI-compatible
planner endpoint, and (optionally) Sentry — all configured via `.env.local`
(never committed). Local development additionally requires the Trigger.dev dev
agent (`npx trigger.dev dev`).

## 6. Freeze statement

From this SHA forward, the `phase2` branch carries all Phase-2 work. `main`
retains the certified Phase-1 product until the final Phase-2 compatibility
decision (Milestone 40). Phase-1 history is never rewritten; Phase-1 tests are
never weakened to make Phase 2 pass.
