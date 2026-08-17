<div align="center">

# 🌐 Evo Builder (EvoBrowser Engine)
### AI-Planned, Real-Time Collaborative Visual Browser Automation

**Describe a goal in plain language, get an editable workflow, run it in a managed cloud browser, watch it live, and replay every run.**

[![Next.js](https://img.shields.io/badge/Next.js-16.2.6-black?style=flat-square&logo=next.js)](https://nextjs.org/)
[![React](https://img.shields.io/badge/React-19.2.4-blue?style=flat-square&logo=react)](https://react.dev/)
[![Stagehand](https://img.shields.io/badge/Stagehand-v3.6.0-orange?style=flat-square)](https://stagehand.dev/)
[![Browserbase](https://img.shields.io/badge/Browserbase-Cloud_Browsers-purple?style=flat-square)](https://browserbase.com/)
[![Trigger.dev](https://img.shields.io/badge/Trigger.dev-v4_Durable_Tasks-darkgreen?style=flat-square)](https://trigger.dev/)
[![Liveblocks](https://img.shields.io/badge/Liveblocks-Multiplayer_CRDT-red?style=flat-square)](https://liveblocks.io/)
[![Clerk](https://img.shields.io/badge/Clerk-Auth_%26_Billing-6C47FF?style=flat-square&logo=clerk)](https://clerk.com/)
[![Neon](https://img.shields.io/badge/Neon-Serverless_Postgres-00E599?style=flat-square&logo=postgresql)](https://neon.tech/)
[![Drizzle ORM](https://img.shields.io/badge/Drizzle_ORM-v0.45.2-C5F74F?style=flat-square&logo=postgresql)](https://orm.drizzle.team/)
[![Sentry](https://img.shields.io/badge/Sentry-Full_Stack_Tracing-362D59?style=flat-square&logo=sentry)](https://sentry.io/)

<br />

<p align="center">
  <a href="#-project-overview">Overview</a> &nbsp;&bull;&nbsp;
  <a href="#-workflow-lifecycle">Lifecycle</a> &nbsp;&bull;&nbsp;
  <a href="#-key-features">Key Features</a> &nbsp;&bull;&nbsp;
  <a href="#-workflow-node-catalog">Node Catalog</a> &nbsp;&bull;&nbsp;
  <a href="#-system-architecture">Architecture</a> &nbsp;&bull;&nbsp;
  <a href="#-getting-started">Getting Started</a> &nbsp;&bull;&nbsp;
  <a href="#-testing">Testing</a> &nbsp;&bull;&nbsp;
  <a href="#-observability--security">Security</a>
</p>

<br />

![Collaborative browser automation workflow canvas](./design/canvas-2.png)

<p align="center"><sub>Interactive React Flow canvas synchronized in real-time across users via Liveblocks, executing durable AI browser actions with live step telemetry.</sub></p>

</div>

---

## 📖 Project Overview

**Evo Builder** is a multi-tenant visual browser-automation platform. It combines a natural-language **AI workflow planner** with a **multiplayer visual canvas**, executes workflows as **durable background tasks** in managed cloud browsers, and provides **live viewing and frame-accurate replay** of every run.

The core loop:

1. **Describe** an automation goal in plain language.
2. The **AI planner** converts it into a validated, editable workflow graph — using only the node types the engine actually supports.
3. You **review and edit** the generated workflow on the collaborative canvas.
4. You **explicitly press Run** — nothing executes automatically.
5. The workflow runs as a **durable Trigger.dev task**, driving a real Browserbase cloud browser.
6. You **watch the live browser** beside the canvas while node statuses stream in.
7. You can **Stop** at any time; the browser session is always cleaned up.
8. When the run finishes you get a **measured completion summary** and a **video replay**.
9. The workflow stays an **ordinary editable graph** — edit it and run it again, with a fresh session each time.

**Evo Builder is built on:**
1. **AI Workflow Planner**: A server-only planner that turns a natural-language goal into a strict, schema-validated workflow plan derived from the live node registry — it can never invent node types the engine doesn't have.
2. **Multiplayer Visual Canvas**: A drag-and-drop graph canvas built on `@xyflow/react` and `@liveblocks/react-flow`, with live cursors, presence, and organization-scoped rooms.
3. **AI-Native Browser Engine (Stagehand V3 + Gemini 2.5 Flash)**: Natural-language `act`, `observe`, `extract`, and autonomous `agent` actions that adapt to DOM changes instead of brittle selectors.
4. **Managed Cloud Browsers (Browserbase)**: Isolated cloud browser sessions with built-in recording, live debug view, and HLS replay.
5. **Durable Execution (Trigger.dev v4)**: Workflows run as background tasks with topological ordering, automatic retries, manual cancellation, and real-time metadata streaming.
6. **Multi-Tenant SaaS**: Organization isolation via Clerk Auth, Pro-plan gates via Clerk Billing, and serverless Neon Postgres via Drizzle ORM.

> **Scope note:** This is the **Phase-1 product**. Execution is a **single sequential durable task** that walks the DAG node-by-node and drives **one shared cloud browser session per run**. There is no distributed scheduler, no custom thread pool, and no worker fleet — those belong to a future phase.

---

## 🔁 Workflow Lifecycle

The full lifecycle that Phase 1 implements and regression-tests end to end:

```
Prompt
  → AI plan (server-validated, registry-derived)
  → editable workflow preview (on the collaborative canvas)
  → explicit Run (user presses the Run button)
  → live browser (Browserbase live view beside the canvas)
  → Stop (optional; always cleans up the session)
  → results / replay (measured summary + HLS recording)
  → edit (the graph stays ordinary and editable)
  → rerun (fresh run identity + fresh browser session)
```

**Guarantees enforced across repeated runs:**

- Each run has an **independent run identity** and its **own Browserbase session**.
- Run #2 never shows Run #1's live view; replays always resolve the **selected historical run's** recording.
- **Stop** cancels only the live run and never touches a completed one.
- Node status paint always reflects the **newest/live run** — no stale "running" state after a run ends.
- The planner never leaves a **stale loading state**, never **duplicates generation**, and nothing **executes automatically**.

---

## ✨ Key Features

<table>
  <tr>
    <td width="50%" valign="top">
      <h3>🧠 AI Workflow Planner</h3>
      <ul>
        <li>New workflows open a <strong>"What do you want to automate?"</strong> screen.</li>
        <li>Server-only planner call — the provider key never reaches the client.</li>
        <li>The node catalog is <strong>derived from the live registry</strong>, so the AI can only use real node types.</li>
        <li>Strict runtime schema validation + semantic checks (one Start, unique IDs, no cycles, valid edges).</li>
        <li>If a goal can't be built, it returns <code>canBuild: false</code> with a reason instead of hallucinating nodes.</li>
        <li><strong>Build manually</strong> escape hatch to the normal canvas at any time.</li>
      </ul>
    </td>
    <td width="50%" valign="top">
      <h3>🎨 Visual Workflow Canvas</h3>
      <ul>
        <li>Interactive graph interface powered by <strong>@xyflow/react</strong>.</li>
        <li>Smooth-step connections with smart handle snapping.</li>
        <li>Custom node components with live status borders, spinners, and error states.</li>
        <li>Deterministic layered layout applied to AI-generated graphs.</li>
        <li>Generated graphs become <strong>ordinary editable canvas state</strong> — no separate "AI editor".</li>
      </ul>
    </td>
  </tr>
  <tr>
    <td width="50%" valign="top">
      <h3>👥 Real-Time Collaboration</h3>
      <ul>
        <li>Collaborative editing via <strong>Liveblocks</strong> CRDTs.</li>
        <li>Shared node positions, properties, and edges in real time.</li>
        <li>Live cursors, selection highlights, and avatar presence.</li>
        <li>Organization-scoped room authorization.</li>
      </ul>
    </td>
    <td width="50%" valign="top">
      <h3>🤖 AI Browser Automation (Stagehand V3)</h3>
      <ul>
        <li><strong>Act</strong>: human-like actions from plain-English instructions.</li>
        <li><strong>Extract</strong>: structured data from any page.</li>
        <li><strong>Observe</strong>: discover candidate interactive elements.</li>
        <li><strong>Agent</strong>: autonomous multi-step goals (Pro plan).</li>
      </ul>
    </td>
  </tr>
  <tr>
    <td width="50%" valign="top">
      <h3>🖥️ Live Browser View</h3>
      <ul>
        <li>Watch the real Browserbase session <strong>beside the canvas</strong> while a run is in flight.</li>
        <li>Live session ID is streamed via Trigger.dev realtime metadata the moment the browser opens.</li>
        <li>Debug URL is fetched through a <strong>server-side proxy</strong> that verifies org + run ownership.</li>
        <li>View-only during execution to avoid user/agent input conflicts.</li>
        <li>Clear states: waiting → connecting → live → ended/unavailable.</li>
      </ul>
    </td>
    <td width="50%" valign="top">
      <h3>⚡ Durable Execution (Trigger.dev)</h3>
      <ul>
        <li>Topological ordering of the graph for deterministic dependency order.</li>
        <li>Long-running background tasks (up to 3600s) with automatic retries.</li>
        <li>Real-time step metadata streaming to the canvas and console.</li>
        <li>One-click <strong>Stop</strong> with guaranteed browser-session cleanup.</li>
      </ul>
    </td>
  </tr>
  <tr>
    <td width="50%" valign="top">
      <h3>📊 Completion Dashboard & Replay</h3>
      <ul>
        <li>Measured run duration, completed/failed node counts, and final browser URL.</li>
        <li>Per-step outputs, timing, and error messages.</li>
        <li>Auto-opens the summary when a run finishes; dismissible back to the logs.</li>
        <li><strong>Run Again</strong> re-runs the graph as it exists on the canvas now.</li>
        <li>Server-proxied HLS video replay, gated to the Pro plan.</li>
      </ul>
    </td>
    <td width="50%" valign="top">
      <h3>🔗 Dynamic Interpolation</h3>
      <ul>
        <li>Pass outputs between nodes with <code>{{ nodeId.path }}</code>.</li>
        <li>Deep dot-notation and array indexing (e.g. <code>{{ observe_1.matches[0].selector }}</code>).</li>
        <li>Clickable connection chips in the node inspector.</li>
        <li>Safe null-coalescing and JSON stringification.</li>
      </ul>
    </td>
  </tr>
  <tr>
    <td width="50%" valign="top">
      <h3>🏢 Multi-Tenant SaaS</h3>
      <ul>
        <li><strong>Clerk Auth & Organizations</strong>: tenant separation and org switching.</li>
        <li><strong>Clerk Billing</strong>: Pro plan gates for the Agent node and replays.</li>
        <li><strong>Resend</strong>: transactional Send Email node.</li>
        <li><strong>Sentry</strong>: end-to-end tracing across client, server actions, routes, and workers.</li>
      </ul>
    </td>
    <td width="50%" valign="top">
      <h3>☁️ Managed Cloud Browsers</h3>
      <ul>
        <li>Zero local Chromium setup.</li>
        <li>One cloud browser session per run, shared across that run's browser nodes.</li>
        <li>AI routed through Browserbase's Model Gateway (no separate model key).</li>
        <li>Built-in session recording for replay.</li>
      </ul>
    </td>
  </tr>
</table>

<br />

---

## 🧩 Workflow Node Catalog

The engine is built around a modular node registry (`features/workflows/nodes/node-registry.ts`) where each node defines its metadata, kind, input fields, and output schema.

| Node | Kind | Accent | Description | Inputs | Available Outputs |
| :--- | :--- | :--- | :--- | :--- | :--- |
| **Start** | `trigger` | Blue | Entry point. Exactly one required per valid graph. | *None* | *None* |
| **Open URL** | `action` | Emerald | Navigates the run's browser session to a page. | `url` *(required)* | `url`<br />`title` |
| **Act** | `action` | Violet | Executes an atomic natural-language action. | `instruction` *(multiline, required)* | `success`<br />`message`<br />`url` |
| **Extract** | `action` | Amber | Extracts structured content via natural language. | `instruction` *(multiline, required)* | `extraction` |
| **Observe** | `action` | Sky | Discovers matching candidate selectors. | `instruction` *(multiline, required)* | `matches`<br />`matches[0].selector`<br />`matches[0].description` |
| **Agent** 🔒 | `action` | Rose | Autonomous multi-step AI agent (Pro plan). | `instruction` *(multiline, required)* | `success`<br />`message`<br />`completed` |
| **Send Email** | `action` | Teal | Sends an HTML email through Resend. | `to` *(required)*<br />`subject` *(required)*<br />`body` *(multiline, required)* | `id` |

<br />

### Adding a New Node Type
Adding a node requires three edits, all under `features/workflows/nodes/`:
1. **Implement the executor** in `<node-name>.ts`.
2. **Register it** in `node-executors.ts` (the `satisfies` contract makes a missing executor a compile error).
3. **Add its manifest** in `node-registry.ts` — kind, label, icon, accent, input `fields`, and `outputs`.

*The canvas, inspector, interpolation tokens, the AI planner catalog, and the execution runtime are all registry-driven — the run task and canvas node never need to change to add a node.*

---

## 🏗️ System Architecture

```mermaid
flowchart TD
    subgraph Client ["Client Browser (React 19 / Next.js 16)"]
        Planner["AI Planner Start Screen"]
        Canvas["React Flow Canvas (@xyflow/react)"]
        Inspector["Right Sidebar & Node Inspector"]
        Console["Run Console & Completion Dashboard"]
        LiveView["Live Browser View (iframe)"]
        VideoPlayer["Session Replay (HLS.js)"]
    end

    subgraph Realtime ["Collaboration & Auth Layer"]
        Liveblocks["Liveblocks Room (CRDT Sync & Presence)"]
        Clerk["Clerk Auth & Billing (Pro Gating)"]
    end

    subgraph Server ["Next.js App Server (Server Actions & Routes)"]
        PlanAction["planWorkflowAction"]
        PlannerService["Planner Service (server-only)"]
        RunAction["runWorkflowAction / cancelWorkflowRunAction"]
        LiveProxy["GET /api/live-view/[sessionId]"]
        ReplayProxy["GET /api/replays/[sessionId]"]
        DB[(Neon Serverless Postgres)]
        Drizzle["Drizzle ORM"]
    end

    subgraph Execution ["Durable Execution (Trigger.dev v4)"]
        Worker["runWorkflowTask"]
        TopoSort["Topological Sort"]
        InterpolateEngine["Interpolation Engine"]
    end

    subgraph BrowserCloud ["Cloud Browser & AI Infrastructure"]
        BB["Browserbase Session (one per run)"]
        SH["Stagehand V3 Orchestrator"]
        LLM["Browserbase Model Gateway (Gemini 2.5 Flash)"]
        PlannerLLM["Planner Provider (OpenAI-compatible)"]
        ResendAPI["Resend Email API"]
    end

    Planner -->|goal| PlanAction
    PlanAction --> PlannerService
    PlannerService -->|catalog + goal| PlannerLLM
    PlannerService -->|validated plan| Planner
    Planner -->|convert + apply| Liveblocks

    Canvas <-->|Bidirectional Sync| Liveblocks
    Canvas -->|Run| RunAction
    RunAction -->|Validate & Persist| Drizzle
    Drizzle --> DB
    RunAction -->|Trigger Task| Worker

    Worker --> TopoSort --> InterpolateEngine
    Worker -->|Open Session| BB
    Worker -->|Execute Actions| SH
    SH -->|Model Calls| LLM
    SH -->|Browser Actions| BB
    Worker -->|Send Emails| ResendAPI
    Worker -.->|Stream Realtime Metadata| Console
    Worker -.->|Live Session ID| LiveView

    LiveView -->|Debug URL| LiveProxy
    LiveProxy -->|sessions.debug| BB
    VideoPlayer -->|HLS Playlist| ReplayProxy
    ReplayProxy -->|Replay Manifest| BB
```

### Execution Lifecycle Breakdown
1. **Plan (optional)**: A new workflow can start from a natural-language goal. The server-only planner derives a catalog from the live registry, calls the provider, and returns a schema-validated plan.
2. **Preview & Edit**: The plan is converted to canvas nodes with a deterministic layered layout and applied through the same Liveblocks mutation manual edits use — it becomes ordinary collaborative state.
3. **Pre-Flight Validation**: Clicking **Run** validates the graph client- and server-side (`validateGraph`): exactly one Start, connected nodes, no cycles.
4. **Graph Persistence**: The validated snapshot is stored in Neon Postgres via Drizzle.
5. **Trigger.dev Scheduling**: The server action triggers `runWorkflowTask`, tagged `workflow:<id>`.
6. **Topological Sort**: The worker sorts connected nodes into a deterministic order.
7. **Unified Browser Session**: One Browserbase session is lazily opened on the first browser node and reused by the rest of the run; its ID is streamed to the frontend immediately.
8. **Step Execution & Live Metadata**: Each node publishes `pending → running → done/failed` with timing and output; the canvas and console render these live, and the live view shows the browser.
9. **Completion & Replay**: On finish, the run returns its steps, session ID, final URL, and duration. The console shows the summary; the replay proxy serves the HLS recording.

---

## 📁 Project Structure

```text
├── app/
│   ├── (auth)/                         # Clerk auth routes (sign-in, sign-up, choose-org)
│   ├── (dashboard)/
│   │   ├── billing/                    # Clerk billing & subscription management
│   │   ├── workflows/[id]/             # Collaborative workflow editor page
│   │   ├── layout.tsx                  # Dashboard layout with sidebar
│   │   └── page.tsx                    # Workflows dashboard index
│   ├── api/
│   │   ├── live-view/[sessionId]/      # Server proxy for Browserbase live debug view (org+run authorized)
│   │   ├── liveblocks/auth/            # Liveblocks token minting & org-scoped auth
│   │   ├── liveblocks/users/           # Liveblocks user resolution
│   │   └── replays/[sessionId]/        # Server proxy for Browserbase HLS replays (Pro-gated)
│   ├── globals.css
│   └── layout.tsx                      # Root layout with theme & auth providers
│
├── components/
│   ├── ui/                             # Radix/Base UI + Tailwind design primitives
│   ├── app-sidebar.tsx                 # Org switcher, workflow list, user footer
│   └── theme-provider.tsx
│
├── features/workflows/
│   ├── components/
│   │   ├── canvas.tsx                  # React Flow canvas + Liveblocks multiplayer hooks
│   │   ├── step-node.tsx               # Custom step node with live run status
│   │   ├── right-sidebar.tsx           # Toolbar (palette) + Inspector (editor) + Run/Stop
│   │   ├── planner-start.tsx           # AI planner "What do you want to automate?" screen
│   │   ├── live-browser.tsx            # Browserbase live view panel beside the canvas
│   │   ├── console-panel.tsx           # Bottom run console + auto-open completion summary
│   │   ├── logs-panel.tsx              # Run history, step list, replay triggers
│   │   ├── inspector-panel.tsx         # Step output / replay / completion dashboard
│   │   ├── session-replay.tsx          # HLS.js video playback
│   │   ├── room.tsx                    # Liveblocks RoomProvider wrapper
│   │   ├── workflow-nav.tsx            # Sidebar workflow list
│   │   ├── workflow-runs-provider.tsx  # Trigger.dev realtime run subscription context
│   │   └── workflow-shell.tsx          # Resizable split-pane layout + planner/canvas switch
│   ├── hooks/
│   │   ├── use-pro-plan.ts             # Clerk org subscription check
│   │   └── use-upstream-connections.ts # Upstream outputs for interpolation tokens
│   ├── lib/
│   │   ├── planner-types.ts            # Zod WorkflowPlan schema & planner I/O types
│   │   ├── planner-catalog.ts          # Derives the planner catalog from the node registry
│   │   ├── planner-service.ts          # Server-only AI planner call + validation
│   │   ├── convert-plan.ts             # Plan → canvas nodes/edges with layered layout
│   │   ├── interpolate.ts              # {{ nodeId.path }} substitution engine
│   │   ├── validate-graph.ts           # Cycle detection & structural validation
│   │   ├── generate-slug.ts            # Workflow name generator
│   │   ├── convert-plan.test.ts        # Conversion & editability tests
│   │   ├── integration.test.ts         # Execution pipeline regression tests
│   │   └── lifecycle.test.ts           # Full Phase-1 lifecycle regression tests
│   ├── nodes/
│   │   ├── act.ts / agent.ts / extract.ts / observe.ts / open-url.ts / send-email.ts
│   │   ├── node-executors.ts           # Action node executor registry
│   │   └── node-registry.ts            # Declarative node manifest registry
│   ├── tasks/
│   │   └── run-workflow.ts             # Trigger.dev durable workflow runner
│   ├── actions.ts                      # Server Actions (create, delete, run, cancel, plan)
│   └── data.ts                         # Drizzle database queries for workflows
│
├── lib/
│   ├── db/                             # Neon client + Drizzle schema
│   ├── browserbase.ts                  # Server-side Browserbase SDK client
│   ├── liveblocks.ts                   # Server-side Liveblocks client
│   ├── resend.ts                       # Resend API client
│   └── utils.ts
│
├── design/                             # UI screenshots and diagrams
├── docs/                               # Implementation reports
├── .env.example                        # Environment variable template
├── trigger.config.ts                   # Trigger.dev runtime settings
├── drizzle.config.ts                   # Drizzle Kit migration config
└── next.config.ts                      # Next.js + Sentry config
```

---

## 🚀 Getting Started

### 1. Prerequisites

- **Node.js** v20.x or higher, **npm** v10.x or higher
- Accounts / API keys for:
  - [Clerk](https://clerk.com/) (Auth & Billing)
  - [Neon](https://neon.tech/) (Serverless Postgres)
  - [Browserbase](https://browserbase.com/) (Cloud browsers, live view, replays)
  - [Trigger.dev](https://trigger.dev/) (Durable task engine)
  - [Liveblocks](https://liveblocks.io/) (Realtime collaboration)
  - [Resend](https://resend.com/) (Email node)
  - An **OpenAI-compatible chat-completions provider** for the AI planner (see below)
  - [Sentry](https://sentry.io/) *(optional)*

### 2. Clone & Install

```bash
git clone https://github.com/UtkarsHMer05/EvoBrowser.git
cd "evo builder"
npm install
```

### 3. Environment Configuration

```bash
cp .env.example .env.local
```

Fill in the values. The planner-specific variables are:

```bash
# AI Workflow Planner (required for "Generate Workflow")
TOKENROUTER_API_KEY=...
# TOKENROUTER_BASE_URL=https://api.tokenrouter.com/v1   # optional
# PLANNER_MODEL=deepseek/deepseek-v4-pro-0813-free      # optional
```

The planner talks to any **OpenAI-compatible `/chat/completions` endpoint** that supports JSON output. `TOKENROUTER_BASE_URL` and `PLANNER_MODEL` let you point it at a different gateway/model without code changes. See `.env.example` for the full list.

### 4. Database Setup

```bash
npm run db:push          # push schema for local prototyping
# or
npm run db:generate && npm run db:migrate
```

### 5. Configure Clerk Organizations & Billing

1. Enable **Organizations** in the Clerk dashboard.
2. Create a Billing plan with the slug `pro`.
3. The app gates the **Agent node** and **Session Replay** behind `has({ plan: "pro" })`.

### 6. Run the Development Services

**Terminal 1 — Trigger.dev worker:**
```bash
npx trigger.dev dev
```

**Terminal 2 — Next.js app:**
```bash
npm run dev
```

Open [http://localhost:3000](http://localhost:3000), sign in, pick an organization, and click **New workflow** — you'll land on the AI planner screen.

---

## 🧪 Testing

Phase 1 ships three automated suites, run together with:

```bash
npm test
```

| Suite | File | Covers |
| :--- | :--- | :--- |
| Conversion & editability | `features/workflows/lib/convert-plan.test.ts` | Plan→graph conversion, deterministic layout, manual edits on generated graphs, interpolation, invalid-plan rejection, pre-flight validation |
| Execution regression | `features/workflows/lib/integration.test.ts` | Topological execution, interpolation end-to-end, post-generation edits, invalid graphs, Stop/cancel cleanup, completion metrics, post-run editability |
| Lifecycle regression | `features/workflows/lib/lifecycle.test.ts` | The full Milestone-15 loop: manual + AI-generated + edited workflows, Run #1 → Run #2 with fresh sessions, stopped/failed runs, non-browser runs, session hygiene, Liveblocks editing, replay selection, no auto-execution |

Plus the standard checks:

```bash
npm run typecheck   # tsc --noEmit
npm run lint        # eslint
npm run build       # production build
```

---

## 🔍 Observability & Security

### 🔒 Security Model
- **Secret isolation**: Browserbase, Resend, Trigger, Liveblocks, and planner keys stay on the server/worker. Client bundles never receive secrets.
- **Live view proxy**: `/api/live-view/[sessionId]` requires a `runId`, retrieves the run, and verifies the run's org matches the caller, the workflow still exists in that org, and the session ID matches what that run published — so one org can never view another's live session.
- **Replay proxy**: `/api/replays/[sessionId]` validates the Clerk session and enforces the Pro plan server-side.
- **Multi-tenant rooms**: Liveblocks tokens are minted per user and scoped to the caller's Clerk organization.
- **Planner**: the provider call happens only in a server action; only the validated plan reaches the client.

### 📊 Sentry Instrumentation
- Distributed tracing connects client actions, server actions, route handlers, and background tasks.
- Isolation scopes tag events with `userId`, `orgId`, `workflowId`, and `runId`.

---

## 💻 Available CLI Scripts

| Command | Description |
| :--- | :--- |
| `npm run dev` | Start the Next.js dev server |
| `npm run build` | Build the production bundle |
| `npm start` | Start the production server |
| `npm run lint` | Run ESLint |
| `npm test` | Run all three Phase-1 test suites |
| `npm run typecheck` | Validate TypeScript types |
| `npm run format` | Format TS/TSX with Prettier |
| `npm run db:generate` | Generate SQL migrations from the Drizzle schema |
| `npm run db:migrate` | Apply pending migrations |
| `npm run db:push` | Push the schema to Neon |
| `npm run db:studio` | Launch Drizzle Studio |

---

## 🛠️ Tech Stack

| Layer | Technologies |
| :--- | :--- |
| **Frontend** | Next.js 16 (App Router), React 19 |
| **Canvas** | @xyflow/react (React Flow) |
| **Multiplayer** | @liveblocks/client, @liveblocks/react, @liveblocks/react-flow |
| **AI Planner** | OpenAI-compatible chat-completions endpoint (configurable) + Zod schema validation |
| **Browser Automation** | @browserbasehq/stagehand v3.6, Google Gemini 2.5 Flash (via Browserbase Model Gateway) |
| **Cloud Browsers** | @browserbasehq/sdk (sessions, live view, HLS replays) |
| **Durable Tasks** | @trigger.dev/sdk v4 |
| **Auth & SaaS** | @clerk/nextjs (Organizations, Billing) |
| **Database** | Neon Serverless Postgres + Drizzle ORM |
| **UI** | Tailwind CSS v4, Radix/Base UI, Lucide |
| **Video** | HLS.js |
| **Email** | Resend |
| **Monitoring** | @sentry/nextjs |

---

## 🗺️ Roadmap

Phase 1 (this release) delivers the full plan → edit → run → watch → replay → rerun loop on a **single sequential durable executor**.

A future phase will explore a real execution engine — e.g. a dependency-aware concurrent scheduler, distributed workers, and task-level fault tolerance — with benchmarks measured at that time. **No such system, and no performance claims, are part of this release.**

---

<div align="center">

**Built by Utkarsh Khajuria**

</div>
