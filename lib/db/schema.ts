import type { Edge } from "@xyflow/react";
import { jsonb, pgTable, text, timestamp, uuid } from "drizzle-orm/pg-core";

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
