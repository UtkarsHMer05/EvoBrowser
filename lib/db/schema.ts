import type { Edge } from "@xyflow/react";
import {
  index,
  integer,
  jsonb,
  pgTable,
  text,
  timestamp,
  unique,
  uuid,
} from "drizzle-orm/pg-core";

import type { StepNodeType } from "@/features/workflows/nodes/node-registry";

// Canonical, server-readable snapshot of the flow. Mirrors React Flow's own
// shape 1:1 so a future executor can read it without remapping. Persisted by the
// Run action; the live editing copy still lives in the Liveblocks room.
export type WorkflowGraph = { nodes: StepNodeType[]; edges: Edge[] };

export const workflows = pgTable("workflows", {
  id: uuid("id").primaryKey().defaultRandom(),
  orgId: text("org_id").notNull(),
  name: text("name").notNull(),
  graph: jsonb("graph").$type<WorkflowGraph>(),
  createdAt: timestamp("created_at").defaultNow().notNull(),
  updatedAt: timestamp("updated_at").defaultNow().notNull(),
});

export type Workflow = typeof workflows.$inferSelect;

// A handshake between the watching browser and the run task. When the Live
// Browser iframe finishes loading, the client writes a row keyed by the
// Browserbase session id; the run task polls for it and holds its first browser
// step until it appears, so the automation never races ahead of the view.
export const liveViewConnections = pgTable("live_view_connections", {
  sessionId: text("session_id").primaryKey(),
  runId: text("run_id"),
  connectedAt: timestamp("connected_at").defaultNow().notNull(),
});

export type LiveViewConnection = typeof liveViewConnections.$inferSelect;

// Per-run artifacts for the results popup. The run task saves a final
// screenshot of the browser (base64 JPEG) before closing the session; the
// results dialog fetches it through an org-checked API route.
export const runArtifacts = pgTable("run_artifacts", {
  runId: text("run_id").primaryKey(),
  orgId: text("org_id").notNull(),
  screenshotBase64: text("screenshot_base64"),
  createdAt: timestamp("created_at").defaultNow().notNull(),
});

export type RunArtifact = typeof runArtifacts.$inferSelect;

// ---------------------------------------------------------------------------
// Phase 2 — durable engine-neutral audit state (Milestone 19).
//
// These tables are ADDITIVE: Phase-1 tables above are untouched, and nothing
// in the Phase-1 app reads or writes them yet. They give the Evo engine a
// durable, engine-neutral record of workflow versions, runs, node runs, task
// attempts, and idempotency keys. Legacy Trigger.dev and Evo runs coexist via
// the `engine` discriminator on workflow_runs.
//
// All timestamps here are wall-clock UTC (database `now()`); in-process
// scheduling latency stays steady_clock inside the C++ engine and is never
// persisted here.
// ---------------------------------------------------------------------------

// Immutable snapshot of a workflow graph at a point in time. Once created, a
// version row is never mutated (M20 enforces this at the application layer);
// runs reference a version, so re-running an old run replays the exact graph.
export const workflowVersions = pgTable(
  "workflow_versions",
  {
    id: uuid("id").primaryKey().defaultRandom(),
    workflowId: uuid("workflow_id")
      .notNull()
      .references(() => workflows.id),
    orgId: text("org_id").notNull(),
    versionNumber: integer("version_number").notNull(),
    graph: jsonb("graph").$type<WorkflowGraph>().notNull(),
    graphHash: text("graph_hash"),
    createdAt: timestamp("created_at").defaultNow().notNull(),
  },
  (t) => [
    // One monotonically increasing version per workflow.
    unique("uq_workflow_versions_workflow_version").on(
      t.workflowId,
      t.versionNumber,
    ),
    index("ix_workflow_versions_org").on(t.orgId),
  ],
);

export type WorkflowVersion = typeof workflowVersions.$inferSelect;

// Engine-neutral run record. `engine` discriminates legacy Trigger.dev runs
// from Evo runs so both can coexist in one audit table. `id` is an
// engine-neutral run id, deliberately separate from any provider run/task id.
export const workflowRuns = pgTable(
  "workflow_runs",
  {
    id: text("id").primaryKey(),
    orgId: text("org_id").notNull(),
    workflowId: uuid("workflow_id")
      .notNull()
      .references(() => workflows.id),
    workflowVersionId: uuid("workflow_version_id").references(
      () => workflowVersions.id,
    ),
    engine: text("engine").notNull().default("legacy"),
    status: text("status").notNull().default("queued"),
    outcome: text("outcome"),
    cancelReason: text("cancel_reason"),
    // Phase 2 (M30): wall-clock UTC timestamp of the FIRST cancellation request
    // for this run. Written once (idempotent — the first request wins) when Stop
    // reaches the engine, before the run is marked terminal. Lets audit/latency
    // tooling distinguish "cancel requested" from "cancel finalized" and proves
    // the request was durably recorded even if the process dies mid-cancel.
    cancelRequestedAt: timestamp("cancel_requested_at"),
    // Phase 2 (M29): the Browserbase session an Evo run drove. The distributed
    // worker stamps it as soon as the session opens, so replay / live-view /
    // screenshot can resolve the session from durable state (the event stream
    // is best-effort and not authoritative). Legacy runs carry their session id
    // in Trigger.dev run output instead and leave this null.
    browserbaseSessionId: text("browserbase_session_id"),
    // Phase 2 (M35): the canonical engine DAG (evo JSON) the run executes,
    // persisted at submission so a restarted scheduler can reconstruct the run's
    // topology from durable state. Postgres is the durable source for run
    // history (Redis alone is not the audit database). NULL for legacy runs and
    // pre-M35 rows.
    dagJson: text("dag_json"),
    createdAt: timestamp("created_at").defaultNow().notNull(),
    startedAt: timestamp("started_at"),
    finishedAt: timestamp("finished_at"),
  },
  (t) => [
    index("ix_workflow_runs_org_created").on(t.orgId, t.createdAt),
    index("ix_workflow_runs_workflow").on(t.workflowId),
    index("ix_workflow_runs_status").on(t.status),
  ],
);

export type WorkflowRun = typeof workflowRuns.$inferSelect;

// One row per DAG node within a run. Identity is (run_id, node_id); the
// unique constraint makes duplicate node-creation idempotent.
export const nodeRuns = pgTable(
  "node_runs",
  {
    id: uuid("id").primaryKey().defaultRandom(),
    runId: text("run_id")
      .notNull()
      .references(() => workflowRuns.id),
    nodeId: text("node_id").notNull(),
    nodeType: text("node_type").notNull(),
    status: text("status").notNull().default("blocked"),
    attemptCount: integer("attempt_count").notNull().default(0),
    output: jsonb("output"),
    failureReason: text("failure_reason"),
    // Phase 2 (M32): node-level retry evidence. When a retryable failure parks
    // the node in RETRY_WAIT, the scheduler stamps the backoff due-time
    // (wall-clock UTC) and the retry reason here; the node is re-dispatched as
    // a new attempt once the due-time passes. `retry_wait_until` NULL => not
    // waiting for retry.
    retryWaitUntil: timestamp("retry_wait_until"),
    retryReason: text("retry_reason"),
    startedAt: timestamp("started_at"),
    finishedAt: timestamp("finished_at"),
  },
  (t) => [
    unique("uq_node_runs_run_node").on(t.runId, t.nodeId),
    index("ix_node_runs_run").on(t.runId),
  ],
);

export type NodeRun = typeof nodeRuns.$inferSelect;

// One row per execution attempt of a node (retries create new attempts).
// Identity is (node_run_id, attempt_number); the unique constraint prevents a
// duplicate delivery from creating a second attempt with the same number.
export const taskAttempts = pgTable(
  "task_attempts",
  {
    id: uuid("id").primaryKey().defaultRandom(),
    nodeRunId: uuid("node_run_id")
      .notNull()
      .references(() => nodeRuns.id),
    attemptNumber: integer("attempt_number").notNull(),
    workerId: text("worker_id"),
    status: text("status").notNull().default("queued"),
    output: jsonb("output"),
    error: text("error"),
    startedAt: timestamp("started_at"),
    finishedAt: timestamp("finished_at"),
    // Phase 2 (M31): task-lease evidence. A worker acquires a lease when it
    // claims the attempt and renews it while work legitimately runs. The
    // scheduler scans for leases whose expires_at has passed and transitions
    // those attempts to `lease_expired` (a recoverable state — NOT a permanent
    // node failure). All wall-clock UTC.
    leaseAcquiredAt: timestamp("lease_acquired_at"),
    leaseRenewedAt: timestamp("lease_renewed_at"),
    leaseExpiresAt: timestamp("lease_expires_at"),
    leaseExpiredAt: timestamp("lease_expired_at"),
  },
  (t) => [
    unique("uq_task_attempts_node_attempt").on(t.nodeRunId, t.attemptNumber),
    index("ix_task_attempts_node_run").on(t.nodeRunId),
    index("ix_task_attempts_lease_expires").on(t.leaseExpiresAt),
  ],
);

export type TaskAttempt = typeof taskAttempts.$inferSelect;

// Phase 2 (M31): worker registry / liveness. One row per worker process,
// upserted on each heartbeat. Heartbeat cadence + expiry are defined SEPARATELY
// from the per-task lease duration (ARCHITECTURE.md): a heartbeat proves the
// PROCESS is alive; a task lease proves a specific ATTEMPT is being worked.
// `last_heartbeat_at` is wall-clock UTC; a worker whose heartbeat is older than
// the heartbeat expiry is considered lost (its leases will expire too).
export const workers = pgTable(
  "workers",
  {
    workerId: text("worker_id").primaryKey(),
    envPrefix: text("env_prefix").notNull(),
    status: text("status").notNull().default("alive"),
    registeredAt: timestamp("registered_at").defaultNow().notNull(),
    lastHeartbeatAt: timestamp("last_heartbeat_at"),
  },
  (t) => [index("ix_workers_env_prefix").on(t.envPrefix)],
);

export type WorkerRegistry = typeof workers.$inferSelect;

// App-level idempotency for duplicate requests/events (M33). The key is the
// primary key, so a second insert with the same key is a no-op conflict that
// callers resolve by reading the stored response.
export const idempotencyRecords = pgTable("idempotency_records", {
  key: text("key").primaryKey(),
  runId: text("run_id"),
  response: jsonb("response"),
  createdAt: timestamp("created_at").defaultNow().notNull(),
});

export type IdempotencyRecord = typeof idempotencyRecords.$inferSelect;
