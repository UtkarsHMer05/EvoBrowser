# Phase-1 Implementation Report — EvoBrowser AI Workflow Builder

**Date:** 2026-08-18
**Scope:** Milestones 1–15 — the complete plan → edit → run → watch → replay → rerun loop.

This report documents exactly what Phase 1 implements. It makes **no claims** about a
distributed scheduler, custom multithreading, Redis workers, or performance improvements —
those belong to the next major development phase (see *Future Work*).

---

## 1. Architecture

Phase 1 is a multi-tenant Next.js application with four planes:

```
┌────────────────────────────────────────────────────────────────────┐
│ CONTROL PLANE — Next.js App Router (client)                        │
│  PlannerStart → Canvas (React Flow + Liveblocks) → RightSidebar    │
│  ConsolePanel (logs + completion dashboard) → LiveBrowser panel    │
├────────────────────────────────────────────────────────────────────┤
│ SERVER PLANE — Server Actions + Route Handlers                     │
│  planWorkflowAction → planner-service (server-only LLM call)       │
│  runWorkflowAction / cancelWorkflowRunAction                       │
│  /api/live-view/[sessionId]  /api/replays/[sessionId]              │
│  /api/liveblocks/auth  /api/liveblocks/users                       │
├────────────────────────────────────────────────────────────────────┤
│ EXECUTION PLANE — Trigger.dev v4 durable task                      │
│  runWorkflowTask: toposort → interpolate → nodeExecutors           │
│  One lazily-opened Browserbase session per run, reused by all      │
│  browser nodes in that run; steps streamed via realtime metadata.  │
├────────────────────────────────────────────────────────────────────┤
│ BROWSER/AI PLANE — Browserbase + Stagehand V3                      │
│  act / observe / extract / agent via Model Gateway (Gemini 2.5)    │
│  Session recording (HLS replay) + live debug view                  │
└────────────────────────────────────────────────────────────────────┘
```

**State ownership:**

- **Graph state** lives in the Liveblocks room (collaborative, CRDT-synced). The Postgres
  `workflows.graph` JSONB column is the snapshot persisted at Run time.
- **Run state** lives in Trigger.dev, subscribed client-side via
  `useRealtimeRunsWithTag("workflow:<id>")` through a single shared
  `WorkflowRunsProvider`.
- **Run identity:** every `tasks.trigger` call creates a new run with its own id, tags,
  metadata, and output. A failed or canceled run returns no output; its steps survive in
  flushed metadata.
- **Session identity:** each run opens at most one Browserbase session (lazily, on the
  first browser node). The session id is published to run metadata immediately and
  returned in the final output, so live view and replay resolve per-run.

**The implemented lifecycle:**

```
Prompt → AI plan → editable workflow preview → explicit Run → live browser
      → Stop → results/replay → edit → rerun
```

---

## 2. Files Introduced (Phase 1)

| File | Milestone | Purpose |
| :--- | :--- | :--- |
| `features/workflows/components/planner-start.tsx` | M2 | "What do you want to automate?" screen: prompt capture, validation, loading/error states, Build-manually escape |
| `features/workflows/lib/planner-types.ts` | M3–M4 | Zod `WorkflowPlan` schema + planner I/O types |
| `features/workflows/lib/planner-catalog.ts` | M4 | Derives the machine-readable planner catalog from the live `nodeRegistry` (UI fields stripped) |
| `features/workflows/lib/planner-service.ts` | M4 | Server-only planner call: provider request, JSON parse, schema + semantic validation |
| `features/workflows/lib/convert-plan.ts` | M5 | Plan → `StepNodeType[]`/`Edge[]` with deterministic depth-layered layout + validation backstop |
| `features/workflows/lib/convert-plan.test.ts` | M6 | Conversion, layout, editability, interpolation, and rejection tests (7 groups) |
| `features/workflows/lib/integration.test.ts` | M8, M12–M14 | Execution-pipeline regression suite (10 cases) |
| `app/api/live-view/[sessionId]/route.ts` | M10, hardened M14 | Server proxy for the Browserbase live debug URL with org + run ownership checks |
| `features/workflows/components/live-browser.tsx` | M10–M11 | Live view panel: waiting/connecting/live/unavailable/ended states, "Running: <step>" header |
| `features/workflows/lib/lifecycle.test.ts` | M15 | Full Phase-1 lifecycle regression suite (10 scenarios) |
| `.env.example` | M15 | Environment template matching actual code usage |
| `docs/PHASE-1-IMPLEMENTATION-REPORT.md` | M15 | This document |

## 3. Files Modified (Phase 1)

| File | Milestones | Changes |
| :--- | :--- | :--- |
| `features/workflows/components/workflow-shell.tsx` | M1–M5, M10, M12, M14 | Planner/canvas view switch, `?new` handling, plan→Liveblocks mutation, live-browser split pane, run-id plumbing |
| `app/(dashboard)/workflows/[id]/page.tsx` | M1 | Reads `?new=true`, passes `isNew` to the shell |
| `features/workflows/actions.ts` | M3–M4, M7 | `planWorkflowAction` (server-side goal validation + planner call); existing create/run/cancel/delete preserved |
| `features/workflows/components/canvas.tsx` | M5 | "AI Workflow Preview" indicator panel with dismiss |
| `features/workflows/components/right-sidebar.tsx` | M7 | Run button pre-flight validation + double-submit guard; Stop via `cancelWorkflowRunAction` |
| `features/workflows/tasks/run-workflow.ts` | M9, M12–M13 | Live session-id metadata publish + flush; `finally`-based Stagehand cleanup; final URL + total duration in output/metadata |
| `features/workflows/components/workflow-runs-provider.tsx` | M9, M13–M14 | Live session-id hook, console run shaping (duration/counts/final URL), `useLatestRun` |
| `features/workflows/components/step-node.tsx` | M11 | Live run-status paint gated on run liveness (no stale "running") |
| `features/workflows/components/logs-panel.tsx` | M13 | Per-run summary rows, step rows, replay rows |
| `features/workflows/components/inspector-panel.tsx` | M13–M14 | Completion dashboard (RunSummary), Watch Replay, Run Again |
| `features/workflows/components/console-panel.tsx` | M14–M15 | Auto-open completion summary on live→finished transition; dismissible |
| `eslint.config.mjs` | M14 audit | Restored default ignores (`node_modules`, `.trigger`, `templates`) |
| `hooks/use-mobile.ts`, `components/ui/carousel.tsx` | M14 audit | Fixed `react-hooks/set-state-in-effect` lint errors |
| `package.json` | M15 | Added `test` script; `tsx` devDependency |

---

## 4. Planner Provider / Configuration

The AI planner (Milestones 1–7) is a **server-only** OpenAI-compatible chat-completions
integration:

| Variable | Required | Default | Purpose |
| :--- | :--- | :--- | :--- |
| `TOKENROUTER_API_KEY` | Yes (for Generate) | — | Bearer key for the planner endpoint |
| `TOKENROUTER_BASE_URL` | No | `https://api.tokenrouter.com/v1` | Any OpenAI-compatible base URL |
| `PLANNER_MODEL` | No | `deepseek/deepseek-v4-pro-0813-free` | Model id sent to the endpoint |

**Request shape:** `POST {base}/chat/completions` with a system prompt containing the
registry-derived catalog and strict output rules, the user goal as the user message,
`temperature: 0.1`, and `response_format: { type: "json_object" }`.

**Validation pipeline (all server-side):**
1. HTTP error mapping (401 auth, 429 rate limit, generic).
2. Markdown-fence stripping + `JSON.parse`.
3. Zod `WorkflowPlanSchema` runtime validation.
4. Semantic checks: unique node ids, exactly one `start`, only registered types, edges
   reference existing ids, ≥1 edge for multi-node plans, cycle detection via toposort.
5. Unbuildable goals return `canBuild: false` + `unsupportedReason` — never invented nodes.

The provider is swappable: any endpoint speaking OpenAI chat-completions with JSON output
works by changing the two optional variables.

---

## 5. Security Model

- **Secret isolation.** `BROWSERBASE_API_KEY`, `TRIGGER_SECRET_KEY`, `LIVEBLOCKS_SECRET_KEY`,
  `RESEND_API_KEY`, `TOKENROUTER_API_KEY`, and `CLERK_SECRET_KEY` are read only in server
  actions, route handlers, server components, or the Trigger.dev worker. None are prefixed
  `NEXT_PUBLIC_`.
- **Live view authorization** (`/api/live-view/[sessionId]`): requires Clerk auth **and** a
  `runId` query param; retrieves the run via `runs.retrieve`, verifies
  `run.payload.orgId === caller orgId`, that the workflow still exists in that org, and that
  the requested session id matches the session the run actually published. Cross-org session
  viewing is rejected with 403.
- **Replay authorization** (`/api/replays/[sessionId]`): Clerk auth + server-side Pro-plan
  check (`has({ plan: "pro" })`) + run/session ownership via the shared `authorizeRunAccess`
  helper (the run must belong to the caller's org and have driven the requested session);
  the Browserbase secret key never leaves the server; the HLS manifest is served `no-store`.
- **Multi-tenancy.** Liveblocks rooms are created with `groupsAccesses: { [orgId]: ["room:write"] }`
  and auth tokens are minted with `groupIds: [orgId]`. All workflow queries filter by
  `orgId` from the Clerk session.
- **Plan gating.** The Agent node and session replays require the Clerk `pro` plan, enforced
  server-side in `runWorkflowAction` and the replay route (not just hidden in UI).
- **Execution is explicit.** No planner response, generation success, or run completion can
  trigger execution; only the Run / Run Again buttons call `runWorkflowAction`, which
  re-validates and re-persists the graph server-side before triggering.

---

## 6. Exact Manual Setup

```bash
# 1. Install
git clone https://github.com/UtkarsHMer05/EvoBrowser.git
cd "evo builder"
npm install

# 2. Environment
cp .env.example .env.local
#    Fill in: Clerk (publishable + secret keys, sign-in/up URLs),
#    DATABASE_URL + DATABASE_URL_UNPOOLED (Neon),
#    TRIGGER_SECRET_KEY, NEXT_PUBLIC_LIVEBLOCKS_PUBLIC_KEY + LIVEBLOCKS_SECRET_KEY,
#    BROWSERBASE_API_KEY, RESEND_API_KEY, TOKENROUTER_API_KEY
#    (optionally TOKENROUTER_BASE_URL, PLANNER_MODEL, Sentry vars)

# 3. Database
npm run db:push        # or: npm run db:generate && npm run db:migrate

# 4. Clerk dashboard
#    Enable Organizations; create a Billing plan with slug "pro"

# 5. Run (two terminals)
npx trigger.dev dev    # terminal 1 — Trigger.dev worker
npm run dev            # terminal 2 — Next.js app

# 6. Verify
npm run typecheck && npm run lint && npm test && npm run build
```

**Manual smoke test:** sign in → create/select an organization → **New workflow** → the
planner screen appears → enter a goal → **Generate Workflow** → preview appears on the
canvas → edit a node → **Run** → live browser panel appears beside the canvas → node
statuses stream → run completes → completion summary auto-opens → close it → edit the
graph → **Run Again** → a new session runs → open the first run's **Replay** from the
console. Also verify **Stop** mid-run cleans up and leaves the graph editable, and
**Build manually** on a fresh workflow opens the plain canvas.

---

## 7. Tests

All suites run with `npm test` (tsx; no browser or network required):

| Suite | Result | Coverage |
| :--- | :--- | :--- |
| `convert-plan.test.ts` | **7/7** | Linear + branching conversion, deterministic layout coordinates, field/email edits, add/delete/reconnect, interpolation tokens, cycle/unknown-type/unbuildable rejection, pre-flight validation |
| `integration.test.ts` | **10/10** | Topological execution of representative graphs, interpolation end-to-end, post-generation edits executed, missing-Start/cycle/disconnected rejection, Stop cleanup + rerun, completion metrics incl. failure counts, post-run editability + no stale paint + no auto-run |
| `lifecycle.test.ts` | **10/10** | The Milestone-15 matrix: manual workflow; AI-generated workflow; generated-then-edited → Run #1; edit → Run #2 (new id, new session, new result, consistent history); stopped run (cancels only the live run); failed run (accurate status, rerun); non-browser (email-only) run; browser session hygiene across runs; Liveblocks editing after runs; replay resolves the selected historical run |

Static checks: `npm run typecheck` (clean), `npm run lint` (clean, 0 problems),
`npm run build` (succeeds; all routes compile).

**Not covered automatically** (require live keys/browser): real Stagehand execution,
Browserbase live-view iframe rendering, HLS replay playback, Liveblocks network sync,
Clerk session flows. The manual smoke test in §6 covers these.

---

## 8. Known Limitations

1. **Sequential execution.** Nodes run one at a time in topological order; independent
   branches are not parallelized. One slow node delays everything downstream.
2. **One browser session per run, single page.** All browser nodes share one session and
   operate on `pages()[0]`; there is no multi-tab/multi-page orchestration.
3. **No workflow versioning.** Runs execute the graph snapshot saved at Run time; there is
   no immutable version history or optimistic-concurrency control on saves.
4. **No retry/backoff policy per node.** Trigger.dev retries the whole task (3 attempts,
   exponential backoff) — there is no node-level retry policy or dead-letter handling.
5. **Live view is view-only and best-effort.** It depends on Browserbase's debug URL being
   reachable while the run is live; after ~8 retries it degrades to "unavailable" and the
   recording remains the source of truth.
6. **Planner quality depends on the provider.** Plans are validated structurally, but field
   content quality (URLs, instructions) is only as good as the model; `canBuild` relies on
   the model's honesty about unsupported goals.
7. **Run history window.** The console shows runs delivered by the realtime subscription
   tag; there is no paginated persistent run-history API.
8. **No idempotency keys.** A retried task re-executes side-effecting nodes (e.g. email).
9. **Email node sends real email** via Resend in every environment — there is no dev-mode
   sink.

---

## 9. Future Work (next phase — not implemented, not claimed)

- **Concurrent DAG scheduler:** execute independent ready branches in parallel with
  dependency counters and a ready queue.
- **Distributed workers:** separate the scheduler from execution workers over a durable
  queue; task leases, heartbeats, and crash recovery.
- **Idempotency & retry policies:** per-node retry/backoff config, idempotency keys for
  side-effecting nodes, dead-letter queue.
- **Workflow versioning:** immutable snapshots per run + optimistic concurrency on save.
- **Multi-tenant fairness:** per-org concurrency quotas and backpressure.
- **Observability:** OpenTelemetry traces across app → task → browser, Prometheus-style
  metrics, structured run logs.
- **Failure-injection suite:** kill/restart workers and dependencies mid-run and record
  recovery behavior.

## 10. Candidate Benchmark Measurements (for the next phase)

These are **measurements to take when the next phase exists** — Phase 1 makes no
performance claims and publishes no numbers.

| Metric | Definition |
| :--- | :--- |
| Sequential baseline duration | Wall-clock time of the current sequential executor on fixed synthetic DAGs (e.g. 1/5/20-node graphs with controlled node latency) |
| Parallel duration & speedup | Same workloads on the concurrent scheduler; speedup = sequential / parallel |
| Scheduling latency p50/p95/p99 | Time from node becoming ready to dispatch |
| Workflow throughput | Workflows completed per minute under load |
| Scaling efficiency | Throughput at 1/2/4/8 workers vs. ideal linear |
| Recovery time | Worker killed mid-task → task re-assigned and resumed |
| Duplicate execution rate | Side-effecting executions per logical operation under failure injection |
| Cancellation latency | Stop pressed → browser session closed |
| Scheduler overhead | CPU/memory of the scheduler process under load |

Raw results should be stored under `benchmarks/results/<date>/` and only then used in any
documentation or resume claim.

---

*End of Phase-1 Implementation Report.*
