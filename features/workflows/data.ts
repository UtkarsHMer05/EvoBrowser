import { and, desc, eq } from "drizzle-orm";

import { getDb } from "@/lib/db";
import {
  WorkflowGraph,
  liveViewConnections,
  runArtifacts,
  workflows,
} from "@/lib/db/schema";
import { validateGraph } from "@/features/workflows/lib/validate-graph";

export async function saveWorkflowGraph({
  orgId,
  id,
  graph,
}: {
  orgId: string;
  id: string;
  graph: WorkflowGraph;
}) {
  const problems = validateGraph(graph);
  if (problems.length > 0) throw new Error(problems.join(" "));
  await getDb()
    .update(workflows)
    .set({ graph, updatedAt: new Date() })
    .where(and(eq(workflows.id, id), eq(workflows.orgId, orgId)));
}

export function listWorkflows(orgId: string) {
  return getDb()
    .select()
    .from(workflows)
    .where(eq(workflows.orgId, orgId))
    .orderBy(desc(workflows.createdAt));
}

export async function getWorkflow(orgId: string, id: string) {
  const [workflow] = await getDb()
    .select()
    .from(workflows)
    .where(and(eq(workflows.id, id), eq(workflows.orgId, orgId)));

  return workflow;
}

export async function createWorkflow(orgId: string, name: string) {
  const [workflow] = await getDb()
    .insert(workflows)
    .values({ orgId, name })
    .returning();

  return workflow;
}

export async function deleteWorkflow(orgId: string, id: string) {
  const [workflow] = await getDb()
    .delete(workflows)
    .where(and(eq(workflows.id, id), eq(workflows.orgId, orgId)))
    .returning();

  return workflow;
}

// --- Live-view connection handshake ---------------------------------------
// The watching browser writes a row when its Live Browser iframe finishes
// loading; the run task polls for it so browser steps don't race ahead of the
// view. Rows are keyed by Browserbase session id and cleaned up after the run.

export async function markLiveViewConnected(sessionId: string, runId?: string) {
  await getDb()
    .insert(liveViewConnections)
    .values({ sessionId, runId })
    .onConflictDoNothing();
}

export async function isLiveViewConnected(sessionId: string): Promise<boolean> {
  const [row] = await getDb()
    .select()
    .from(liveViewConnections)
    .where(eq(liveViewConnections.sessionId, sessionId));
  return Boolean(row);
}

export async function clearLiveViewConnection(sessionId: string) {
  await getDb()
    .delete(liveViewConnections)
    .where(eq(liveViewConnections.sessionId, sessionId));
}

// --- Run artifacts (final screenshot for the results popup) ----------------
// The run task upserts a row per run before closing the browser; the results
// dialog reads it through an org-checked API route.

export async function saveRunArtifact({
  runId,
  orgId,
  screenshotBase64,
}: {
  runId: string;
  orgId: string;
  screenshotBase64?: string;
}) {
  await getDb()
    .insert(runArtifacts)
    .values({ runId, orgId, screenshotBase64 })
    .onConflictDoUpdate({
      target: runArtifacts.runId,
      set: { screenshotBase64 },
    });
}

export async function getRunArtifact(orgId: string, runId: string) {
  const [row] = await getDb()
    .select()
    .from(runArtifacts)
    .where(and(eq(runArtifacts.runId, runId), eq(runArtifacts.orgId, orgId)));
  return row;
}
