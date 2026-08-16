<div align="center">

# 🌐 Evo Builder (EvoBrowser Engine)
### Real-Time Collaborative Visual Browser Automation SaaS

**Design workflows collaboratively in real-time. Execute autonomously in managed cloud browsers. Replay every run with frame-accurate observability.**

[![Next.js](https://img.shields.io/badge/Next.js-16.2.6-black?style=flat-square&logo=next.js)](https://nextjs.org/)
[![React](https://img.shields.io/badge/React-19.2.4-blue?style=flat-square&logo=react)](https://react.dev/)
[![Stagehand](https://img.shields.io/badge/Stagehand-v3.6.0-orange?style=flat-square)](https://stagehand.dev/)
[![Browserbase](https://img.shields.io/badge/Browserbase-Cloud_Browsers-purple?style=flat-square)](https://browserbase.com/)
[![Trigger.dev](https://img.shields.io/badge/Trigger.dev-v4_Durable_Tasks-darkgreen?style=flat-square)](https://trigger.dev/)
[![Liveblocks](https://img.shields.io/badge/Liveblocks-Multiplayer_CRDT-red?style=flat-square)](https://liveblocks.io/)
[![Clerk](https://img.shields.io/badge/Clerk-Auth_%26_Billing-6C47FF?style=flat-square&logo=clerk)](https://clerk.com/)
[![Neon](https://img.shields.io/badge/Neon-Serverless_Postgres-00E599?style=flat-square&logo=postgresql)](https://neon.tech/)
[![Drizzle ORM](https://img.shields.io/badge/Drizzle_ORM-v0.45.2-C5F74F?style=flat-square)](https://orm.drizzle.team/)
[![Sentry](https://img.shields.io/badge/Sentry-Full_Stack_Tracing-362D59?style=flat-square&logo=sentry)](https://sentry.io/)

<br />

<p align="center">
  <a href="#-project-overview">Overview</a> &nbsp;&bull;&nbsp;
  <a href="#-key-features">Key Features</a> &nbsp;&bull;&nbsp;
  <a href="#-workflow-node-catalog">Node Catalog</a> &nbsp;&bull;&nbsp;
  <a href="#-data-flow--dynamic-interpolation">Data Flow</a> &nbsp;&bull;&nbsp;
  <a href="#-system-architecture">Architecture</a> &nbsp;&bull;&nbsp;
  <a href="#-getting-started">Getting Started</a> &nbsp;&bull;&nbsp;
  <a href="#-deployment-guide">Deployment</a> &nbsp;&bull;&nbsp;
  <a href="#-observability--security">Observability</a>
</p>

<br />

![Collaborative browser automation workflow canvas](./design/canvas-2.png)

<p align="center"><sub>Interactive React Flow canvas synchronized in real-time across users via Liveblocks, executing durable AI browser actions with live step telemetry.</sub></p>

</div>

---

## 📖 Project Overview

**Evo Builder** is an enterprise-ready, multi-tenant visual workflow automation platform that bridges the gap between no-code visual workflow builders and cutting-edge AI browser automation.

Traditional browser automation tools are brittle, difficult to maintain, and prone to breaking whenever web layouts change. Furthermore, standard automation builders execute invisibly in headless silos with no live feedback or team collaboration.

**Evo Builder solves this by combining:**
1. **Multiplayer Visual Canvas**: A drag-and-drop graph canvas built on `@xyflow/react` and `@liveblocks/react-flow`, allowing entire teams to build, edit, and observe workflows together with live cursors and presence.
2. **AI-Native Browser Engine (Stagehand V3 + Gemini 2.5 Flash)**: Natural language commands (`act`, `observe`, `extract`, and autonomous `agent`) that automatically adapt to DOM changes and dynamic web applications without hardcoded fragile XPath or CSS selectors.
3. **Managed Cloud Infrastructure (Browserbase)**: Fast, resilient cloud browser instances running in isolated environments with built-in stealth, proxy capabilities, and session recording.
4. **Durable Distributed Execution (Trigger.dev v4)**: Workflows execute as background DAGs (Directed Acyclic Graphs) with topological dependency resolution, automatic retries with exponential backoff, manual run cancellation, and real-time metadata streaming.
5. **Full Session Observability & Replay**: Complete step-by-step execution metrics, live node spinner states, structured output inspection, and server-proxied HLS video replays of actual browser sessions.
6. **Multi-Tenant SaaS Architecture**: Complete organization isolation with Clerk Authentication, Clerk Billing subscription gates (Free vs. Pro plans), and serverless Neon Postgres managed via Drizzle ORM.

---

## ✨ Key Features

<table>
  <tr>
    <td width="50%" valign="top">
      <h3>🎨 Visual Workflow Canvas</h3>
      <ul>
        <li>Interactive graph interface powered by <strong>@xyflow/react</strong>.</li>
        <li>Smooth step bezier connections with smart handle snapping.</li>
        <li>Custom designed node components with responsive status borders, loading spinners, and error states.</li>
        <li>Canvas zoom, pan, minimap, and responsive controls.</li>
      </ul>
    </td>
    <td width="50%" valign="top">
      <h3>👥 Real-Time Multiplayer Collaboration</h3>
      <ul>
        <li>Collaborative editing powered by <strong>Liveblocks</strong> CRDTs.</li>
        <li>Real-time shared node positions, property changes, and edge connections.</li>
        <li>Multiplayer live cursors, selection highlights, and presence awareness.</li>
        <li>Organization-scoped room authorization and live user avatar stacks.</li>
      </ul>
    </td>
  </tr>
  <tr>
    <td width="50%" valign="top">
      <h3>🤖 AI Browser Automation (Stagehand V3)</h3>
      <ul>
        <li><strong>Act</strong>: Execute human-like actions using plain English instructions.</li>
        <li><strong>Extract</strong>: Intelligently parse and extract structured data from any webpage.</li>
        <li><strong>Observe</strong>: Discover and score interactive DOM elements dynamically.</li>
        <li><strong>Agent</strong>: Multi-step autonomous AI agent executing complex end-to-end goals.</li>
      </ul>
    </td>
    <td width="50%" valign="top">
      <h3>☁️ Managed Cloud Browsers (Browserbase)</h3>
      <ul>
        <li>Zero local Chromium configuration or maintenance.</li>
        <li>Single persistent cloud browser session maintained throughout the entire workflow lifecycle.</li>
        <li>Integrated AI Model Gateway powering Stagehand (no individual model API keys needed).</li>
        <li>Built-in fingerprint evasion, anti-bot mitigation, and cloud scaling.</li>
      </ul>
    </td>
  </tr>
  <tr>
    <td width="50%" valign="top">
      <h3>⚡ Durable Background Execution (Trigger.dev)</h3>
      <ul>
        <li>Topological sorting of workflow graphs to ensure exact execution dependency order.</li>
        <li>Long-running background tasks with configurable timeouts (up to 3600s).</li>
        <li>Automatic retries with randomized exponential backoff.</li>
        <li>Real-time metadata streaming directly from worker tasks to frontend subscribers.</li>
        <li>Instant one-click cancellation of running workflows.</li>
      </ul>
    </td>
    <td width="50%" valign="top">
      <h3>🎥 Frame-Accurate Session Replay</h3>
      <ul>
        <li>Every cloud browser run records an HLS video stream.</li>
        <li>Secure server-side proxy route (`/api/replays/[sessionId]`) guarding private Browserbase keys.</li>
        <li>Integrated HLS.js video player with adaptive playback and native Safari fallback.</li>
        <li>Gated access tied directly to Clerk Pro subscription plans.</li>
      </ul>
    </td>
  </tr>
  <tr>
    <td width="50%" valign="top">
      <h3>🔗 Dynamic Variable Interpolation</h3>
      <ul>
        <li>Pass outputs seamlessly between nodes using <code>{{ nodeId.path }}</code> syntax.</li>
        <li>Supports deep dot-notation and array index lookups (e.g. <code>{{ observe_1.matches[0].selector }}</code>).</li>
        <li>Interactive autocomplete connection chips in the Node Inspector.</li>
        <li>Safe null-coalescing and automatic JSON object stringification.</li>
      </ul>
    </td>
    <td width="50%" valign="top">
      <h3>🏢 Enterprise SaaS & Observability</h3>
      <ul>
        <li><strong>Clerk Auth & Organizations</strong>: Multi-tenant tenant separation and team member switching.</li>
        <li><strong>Clerk Billing</strong>: Native pricing tables and Pro plan feature gates.</li>
        <li><strong>Resend Email Integration</strong>: Transactional notification node for alert workflows.</li>
        <li><strong>Full-Stack Sentry</strong>: End-to-end distributed tracing across client, server actions, route handlers, and workers.</li>
      </ul>
    </td>
  </tr>
</table>

<br />

---

## 🧩 Workflow Node Catalog

The engine is built around a modular node registry (`features/workflows/nodes/node-registry.ts`) where each node defines its metadata, kind, input fields, and output schema.

| Node | Kind | Accent Color | Description | Inputs | Available Outputs |
| :--- | :--- | :--- | :--- | :--- | :--- |
| **Start** | `trigger` | Blue | Entry point for the workflow. Exactly one required per valid graph. | *None* | *None* |
| **Open URL** | `action` | Emerald | Navigates the shared browser session to a target web page. | `url` *(required)* | `url`<br />`title` |
| **Act** | `action` | Violet | Executes an atomic natural-language action on the current page. | `instruction` *(multiline, required)* | `success`<br />`message`<br />`url` |
| **Extract** | `action` | Amber | Extracts structured content or text from the page using natural language. | `instruction` *(multiline, required)* | `extraction` |
| **Observe** | `action` | Sky | Scans the active page and discovers matching candidate selectors and descriptions. | `instruction` *(multiline, required)* | `matches`<br />`matches[0].selector`<br />`matches[0].description` |
| **Agent** 🔒 | `action` | Rose | Runs an autonomous multi-step AI agent (Requires Pro plan). | `instruction` *(multiline, required)* | `success`<br />`message`<br />`completed` |
| **Send Email** | `action` | Teal | Sends an HTML formatted email through the Resend API. | `to` *(required)*<br />`subject` *(required)*<br />`body` *(multiline, required)* | `id` *(Email ID)* |

<br />

### Adding a New Node Type
Adding a new node requires three simple steps:
1. **Implement the executor logic** in `features/workflows/nodes/<node-name>.ts`.
2. **Register the executor** in `features/workflows/nodes/node-executors.ts` (enforced via TypeScript `satisfies Record<ActionNodeType, NodeExecutor>`).
3. **Define the manifest** in `features/workflows/nodes/node-registry.ts` specifying icon, accent color, input fields, and output paths.

*The canvas rendering, node inspector, data connection tokens, and execution runtime are 100% registry-driven.*

---

## 🔄 Data Flow & Dynamic Interpolation

Evo Builder features an expressive template interpolation engine (`features/workflows/lib/interpolate.ts`) that allows nodes to reference results produced by upstream steps.

```
┌─────────────┐       ┌─────────────────┐       ┌─────────────────┐
│  Open URL   │ ────> │     Extract     │ ────> │   Send Email    │
│  id: "nav"  │       │  id: "scrape"   │       │   id: "notify"  │
└─────────────┘       └─────────────────┘       └─────────────────┘
                                                         │
                     References upstream output:         │
                     "Price is: {{ scrape.extraction }}" ◄
```

### Interpolation Mechanics:
- **Syntax**: `{{ <nodeId>.<path> }}`
- **Path Resolution**: Supports nested object keys and array indexing (e.g., `{{ node_1.matches[0].selector }}`).
- **Dependency Ordering**: Workflows are topologically sorted before execution. When a node runs, all referenced upstream outputs are guaranteed to be evaluated and populated in the runtime memory map.
- **Visual Token Picker**: The Node Inspector detects upstream connected nodes and renders clickable chips that insert the corresponding expression into the focused input field.

---

## 🏗️ System Architecture

```mermaid
flowchart TD
    subgraph Client ["Client Browser (React 19 / Next.js 16)"]
        Canvas["React Flow Canvas (@xyflow/react)"]
        Inspector["Right Sidebar & Node Inspector"]
        Console["Live Run Console & Step Telemetry"]
        VideoPlayer["Session Replay (HLS.js)"]
    end

    subgraph Realtime ["Collaboration & Auth Layer"]
        Liveblocks["Liveblocks Room (CRDT Sync & Presence)"]
        Clerk["Clerk Auth & Billing (Pro Plan Gating)"]
    end

    subgraph Server ["Next.js App Server (Server Actions & Route Handlers)"]
        ServerAction["saveWorkflowGraph / runWorkflowAction"]
        ReplayProxy["GET /api/replays/[sessionId]"]
        DB[(Neon Serverless Postgres)]
        Drizzle["Drizzle ORM"]
    end

    subgraph Execution ["Durable Execution Engine (Trigger.dev v4)"]
        Worker["runWorkflowTask (Worker Task)"]
        TopoSort["Topological Graph Sorter"]
        InterpolateEngine["Interpolation Engine"]
    end

    subgraph BrowserCloud ["Cloud Browser & AI Infrastructure"]
        BB["Browserbase Managed Browser Session"]
        SH["Stagehand V3 Orchestrator"]
        LLM["Browserbase Model Gateway (Gemini 2.5 Flash)"]
        ResendAPI["Resend Email API"]
    end

    Canvas <-->|Bidirectional Sync| Liveblocks
    Canvas -->|Run Workflow Action| ServerAction
    ServerAction -->|Validate & Persist Graph| Drizzle
    Drizzle --> DB
    ServerAction -->|Trigger Background Task| Worker

    Worker --> TopoSort
    TopoSort --> InterpolateEngine
    Worker -->|Initialize Session| BB
    Worker -->|Execute Actions| SH
    SH -->|Model Calls| LLM
    SH -->|Browser Actions| BB
    Worker -->|Send Emails| ResendAPI
    Worker -.->|Stream Realtime Metadata| Console

    VideoPlayer -->|Request Replay HLS| ReplayProxy
    ReplayProxy -->|Fetch Stream Manifest| BB
```

### Execution Lifecycle Breakdown:
1. **Design & Real-Time Sync**: Users modify nodes and connections on the React Flow canvas. `useLiveblocksFlow` syncs graph state, cursors, and presence in real time across all connected organization members.
2. **Pre-Flight Validation**: Clicking **Run** triggers client and server-side graph validation (`validateGraph`), verifying that exactly one `Start` trigger exists, nodes are connected, and no cyclic loops exist.
3. **Graph Persistence**: The validated graph snapshot is stored in Neon Postgres using Drizzle ORM.
4. **Trigger.dev Scheduling**: The server action triggers the durable background task `runWorkflowTask`, tagged with `workflow:<id>`.
5. **Topological Sort**: The worker topologically sorts the connected nodes into a deterministic linear execution queue.
6. **Unified Browser Session**: A single persistent Browserbase browser session is lazily spawned on the first browser step. All subsequent `open-url`, `act`, `observe`, `extract`, and `agent` nodes operate on the same live page context.
7. **Step Execution & Live Metadata**: As each node starts, executes, and finishes, the worker publishes updated step states (`pending` ➔ `running` ➔ `done` / `failed`) with wall-clock timing and outputs to Trigger.dev metadata. The frontend console streams these updates live.
8. **Session Replay Retrieval**: When the run finishes, the Browserbase session ID is returned. The UI polls the secure proxy route `/api/replays/[sessionId]`, which fetches the HLS `.m3u8` playlist and feeds it to Hls.js for instant video playback.

---

## 📁 Project Structure

```text
├── app/
│   ├── (auth)/                         # Clerk auth routes (sign-in, sign-up)
│   ├── (dashboard)/
│   │   ├── billing/                    # Clerk billing and subscription management
│   │   ├── workflows/
│   │   │   └── [id]/                   # Main collaborative workflow editor page
│   │   ├── layout.tsx                  # Dashboard layout with sidebar navigation
│   │   └── page.tsx                    # Workflows dashboard index & empty states
│   ├── api/
│   │   ├── liveblocks/
│   │   │   └── auth/                   # Liveblocks token minting & org-scoped authorization
│   │   └── replays/
│   │   │   └── [sessionId]/            # Server-side proxy for Browserbase HLS session replays
│   ├── globals.css                     # Design system tokens and Tailwind CSS styling
│   └── layout.tsx                      # Root application layout with theme & auth providers
│
├── components/
│   ├── ui/                             # Radix UI + Tailwind design system primitives
│   ├── app-sidebar.tsx                 # Organization switcher, workflow list, and user footer
│   └── theme-provider.tsx              # Dark/Light theme switching provider
│
├── features/workflows/
│   ├── components/
│   │   ├── canvas.tsx                  # React Flow canvas with Liveblocks multiplayer hooks
│   │   ├── step-node.tsx               # Custom React Flow step node component
│   │   ├── right-sidebar.tsx           # Toolbar (node palette) and Inspector (node editor)
│   │   ├── logs-panel.tsx              # Real-time run console, step list, and replay triggers
│   │   ├── session-replay.tsx          # HLS.js video playback component
│   │   ├── console-panel.tsx           # Collapsible bottom execution drawer
│   │   ├── room.tsx                    # Liveblocks RoomProvider wrapper with Suspense
│   │   ├── workflow-nav.tsx            # Left sidebar workflow list navigation
│   │   ├── workflow-runs-provider.tsx  # Trigger.dev realtime run subscription context
│   │   └── workflow-shell.tsx          # Resizable split-pane layout container
│   ├── hooks/
│   │   ├── use-pro-plan.ts             # Clerk organization subscription check hook
│   │   └── use-upstream-connections.ts # Detects accessible upstream nodes for interpolation
│   ├── lib/
│   │   ├── interpolate.ts              # Mustache-style {{ nodeId.path }} substitution engine
│   │   └── validate-graph.ts           # Cycle detection and topological graph validation
│   ├── nodes/
│   │   ├── act.ts                      # Stagehand 'act' executor
│   │   ├── agent.ts                    # Stagehand 'agent' autonomous executor
│   │   ├── extract.ts                  # Stagehand 'extract' structured data executor
│   │   ├── observe.ts                  # Stagehand 'observe' element candidate executor
│   │   ├── open-url.ts                 # Stagehand browser page navigation executor
│   │   ├── send-email.ts               # Resend email delivery executor
│   │   ├── node-executors.ts           # Action node executor registry
│   │   └── node-registry.ts            # Declarative node manifest registry & metadata
│   ├── tasks/
│   │   └── run-workflow.ts             # Trigger.dev durable background workflow runner
│   ├── actions.ts                      # Next.js Server Actions (create, delete, run, cancel)
│   └── data.ts                         # Drizzle database queries for workflows
│
├── lib/
│   ├── db/
│   │   ├── index.ts                    # Neon Serverless Postgres client initialization
│   │   └── schema.ts                   # Drizzle ORM schema definition
│   ├── browserbase.ts                  # Server-side Browserbase SDK client
│   ├── liveblocks.ts                   # Server-side Liveblocks client
│   ├── resend.ts                       # Resend API client
│   └── utils.ts                        # Tailwind class merge utilities
│
├── design/                             # UI/UX design assets, screenshots, and diagrams
├── trigger.config.ts                   # Trigger.dev SDK configuration and runtime settings
├── drizzle.config.ts                   # Drizzle Kit migration configuration
└── next.config.ts                      # Next.js configuration with Sentry instrumentation
```

---

## 🚀 Getting Started

Follow these steps to set up and run Evo Builder locally on your machine.

### 1. Prerequisites

Ensure you have the following installed:
- **Node.js**: v20.x or higher
- **npm**: v10.x or higher
- Accounts / API Keys for:
  - [Clerk](https://clerk.com/) (Authentication & Billing)
  - [Neon](https://neon.tech/) (Serverless Postgres)
  - [Browserbase](https://browserbase.com/) (Cloud Browser Automation)
  - [Trigger.dev](https://trigger.dev/) (Durable Task Engine)
  - [Liveblocks](https://liveblocks.io/) (Real-time Collaboration)
  - [Resend](https://resend.com/) (Transactional Email)
  - [Sentry](https://sentry.io/) *(Optional)* (Application Monitoring)

---

### 2. Clone the Repository & Install Dependencies

```bash
git clone https://github.com/UtkarsHMer05/EvoBrowser.git
cd "evo builder"
npm install
```

---

### 3. Environment Configuration

Create a `.env.local` file by copying the example file:

```bash
cp .env.example .env.local
```

Populate the required environment variables:

```bash
# ==============================================================================
# Clerk Authentication & Multi-Tenancy
# ==============================================================================
NEXT_PUBLIC_CLERK_PUBLISHABLE_KEY=pk_test_...
CLERK_SECRET_KEY=sk_test_...
NEXT_PUBLIC_CLERK_SIGN_IN_URL=/sign-in
NEXT_PUBLIC_CLERK_SIGN_UP_URL=/sign-up
NEXT_PUBLIC_CLERK_SIGN_IN_FALLBACK_REDIRECT_URL=/
NEXT_PUBLIC_CLERK_SIGN_UP_FALLBACK_REDIRECT_URL=/

# ==============================================================================
# Neon Postgres Database (Drizzle ORM)
# ==============================================================================
DATABASE_URL=postgresql://user:password@ep-xyz-pooler.us-east-2.aws.neon.tech/neondb?sslmode=require
DATABASE_URL_UNPOOLED=postgresql://user:password@ep-xyz.us-east-2.aws.neon.tech/neondb?sslmode=require
NEON_BRANCH=main

# ==============================================================================
# Trigger.dev v4 (Durable Execution)
# ==============================================================================
TRIGGER_SECRET_KEY=tr_dev_...

# ==============================================================================
# Liveblocks (Multiplayer CRDT Collaboration)
# ==============================================================================
NEXT_PUBLIC_LIVEBLOCKS_PUBLIC_KEY=pk_dev_...
LIVEBLOCKS_SECRET_KEY=sk_...

# ==============================================================================
# Browserbase (Stagehand Cloud Browsers & Replays)
# ==============================================================================
BROWSERBASE_API_KEY=bb_api_...

# ==============================================================================
# Resend (Email Notification Node)
# ==============================================================================
RESEND_API_KEY=re_...

# ==============================================================================
# Sentry (Observability & Tracing) [Optional]
# ==============================================================================
NEXT_PUBLIC_SENTRY_DSN=https://...@ingest.sentry.io/...
SENTRY_DSN=https://...@ingest.sentry.io/...
SENTRY_AUTH_TOKEN=sntrys_...
```

---

### 4. Database Setup & Migrations

Push the Drizzle ORM schema to your Neon Postgres database:

```bash
# Push schema directly for local prototyping:
npm run db:push

# Or generate and run standard migrations:
npm run db:generate
npm run db:migrate
```

*(Optional)* Launch Drizzle Studio to inspect and edit your database records visually:
```bash
npm run db:studio
```

---

### 5. Configure Clerk Organizations & Billing

1. In your **Clerk Dashboard**, go to **Organization Settings** and enable **Organizations**.
2. Go to **Billing** and create a plan with the slug `pro`.
3. The application uses Clerk's `has({ plan: "pro" })` server check and `useProPlan()` client hook to gate:
   - The **Agent** node execution.
   - The **Session Replay** video player.

---

### 6. Run the Development Services

Run the Trigger.dev background worker and the Next.js development server in parallel terminals.

**Terminal 1 — Trigger.dev Background Task Worker:**
```bash
npx trigger.dev dev
```

**Terminal 2 — Next.js Web Application:**
```bash
npm run dev
```

Open [http://localhost:3000](http://localhost:3000) in your browser:
1. Sign up or log in via Clerk.
2. Create or select an Organization.
3. Click **New Workflow** to start designing your first automated browser flow!

---

## 🚢 Deployment Guide

Evo Builder is engineered to run seamlessly across serverless hosting platforms (such as **Railway** or **Vercel**) paired with Trigger.dev Cloud.

### 1. Deploying the Next.js Web App on Railway

1. Push your repository to GitHub.
2. In [Railway](https://railway.app/), click **New Project** ➔ **Deploy from GitHub repo**.
3. Railway automatically detects the Next.js app using Railpack.
4. Set the Build and Start commands:
   - **Build Command**: `npm run build`
   - **Start Command**: `npm start`
5. Add all production environment variables from `.env.local` into the Railway service settings.
6. Run database migrations against your production database:
   ```bash
   npm run db:migrate
   ```

---

### 2. Deploying Trigger.dev Background Tasks

Trigger.dev tasks are deployed separately to the Trigger.dev cloud execution engine:

```bash
npx trigger.dev deploy
```

Ensure that the `TRIGGER_SECRET_KEY` configured in your Railway production environment matches the production environment key in Trigger.dev.

---

## 🔍 Observability & Security

### 🔒 Enterprise Security Posture
- **Private Key Isolation**: All Browserbase API keys, Stagehand operations, Resend keys, and Trigger secret keys remain strictly on the server or worker tasks. Client bundles never receive secret tokens.
- **Server-Side Replay Proxy**: Browserbase HLS replay manifests are proxied server-side via `app/api/replays/[sessionId]/route.ts`. Direct access is validated against the active Clerk user session and organization plan permissions.
- **Multi-Tenant Room Scoping**: Liveblocks room tokens are minted dynamically via `app/api/liveblocks/auth/route.ts`, binding room access strictly to members of the matching Clerk Organization ID.

### 📊 Full-Stack Sentry Instrumentation
- Distributed request tracing connects client user actions, Server Actions (`createWorkflowAction`, `runWorkflowAction`), API route handlers, and background tasks.
- Sentry Isolation Scopes automatically tag every event with `userId`, `orgId`, `workflowId`, and `runId`.

---

## 💻 Available CLI Scripts

| Command | Description |
| :--- | :--- |
| `npm run dev` | Starts the Next.js development server with Webpack |
| `npm run build` | Builds the production bundle |
| `npm start` | Starts the production Next.js server |
| `npm run lint` | Runs ESLint 9 code quality checks |
| `npm run format` | Formats all TS and TSX files with Prettier & Tailwind plugin |
| `npm run typecheck` | Validates TypeScript types across the entire repository |
| `npm run db:generate`| Generates SQL migration files from the Drizzle schema |
| `npm run db:migrate` | Applies pending Drizzle database migrations |
| `npm run db:push` | Pushes the Drizzle schema directly to Neon Postgres |
| `npm run db:studio` | Launches Drizzle Studio in your browser |

---

## 🛠️ Complete Tech Stack Reference

| Layer | Technologies |
| :--- | :--- |
| **Frontend Framework** | [Next.js 16 (App Router)](https://nextjs.org/) & [React 19](https://react.dev/) |
| **Workflow Canvas** | [@xyflow/react (React Flow)](https://reactflow.dev/) |
| **Multiplayer / CRDT** | [@liveblocks/client](https://liveblocks.io/), `@liveblocks/react`, `@liveblocks/react-flow` |
| **AI Browser Automation**| [@browserbasehq/stagehand v3.6](https://stagehand.dev/) & [Google Gemini 2.5 Flash](https://deepmind.google/technologies/gemini/) |
| **Cloud Browser Infra** | [@browserbasehq/sdk](https://browserbase.com/) (Sessions, Model Gateway, HLS Replays) |
| **Durable Task Engine** | [@trigger.dev/sdk v4](https://trigger.dev/) |
| **Authentication & SaaS**| [@clerk/nextjs](https://clerk.com/) (Organizations, User Management, Billing) |
| **Database & ORM** | [Neon Serverless Postgres](https://neon.tech/) with [Drizzle ORM](https://orm.drizzle.team/) |
| **Styling & UI** | [Tailwind CSS v4](https://tailwindcss.com/), [Radix UI](https://www.radix-ui.com/), Lucide Icons |
| **Video Playback** | [HLS.js](https://github.com/video-dev/hls.js/) |
| **Email Delivery** | [Resend](https://resend.com/) |
| **Monitoring** | [@sentry/nextjs](https://sentry.io/) |

---

<div align="center">

**Built with precision by Utkarsh Khajuria**

</div>
