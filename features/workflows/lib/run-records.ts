// Phase 2 — engine-neutral run records (Milestone 27).
//
// Creates the durable engine-neutral run row (workflow_runs) BEFORE a run is
// submitted to either engine (M27 step 6). The `engine` discriminator records
// which engine owns the run so legacy Trigger.dev runs and Evo runs coexist in
// one audit table. The run id is engine-neutral and deliberately separate
// from any provider id.
//
// Idempotency: workflow_runs.id is the primary key, so inserting the same run
// id twice is a no-op conflict (ON CONFLICT DO NOTHING) — a re-submission with
// the same client-generated run id never creates a second row.
//
// Timestamps are wall-clock UTC (database now()), consistent with M19/M20.

import { sql } from "drizzle-orm";

import { getDb } from "@/lib/db";
import { workflowRuns, type WorkflowRun } from "@/lib/db/schema";

import type { ExecutionEngine } from "./execution-engine";
import type { VersioningDb } from "./workflow-versions";

export interface CreateRunRecordArgs {
  /** Engine-neutral run id (client-generated; idempotent on re-submit). */
  runId: string;
  orgId: string;
  workflowId: string;
  workflowVersionId?: string;
  engine: ExecutionEngine;
  db?: VersioningDb;
}

/**
 * Insert the engine-neutral run row. Returns the row. Idempotent on runId:
 * a duplicate insert returns the existing row instead of failing.
 */
export async function createWorkflowRunRecord({
  runId,
  orgId,
  workflowId,
  workflowVersionId,
  engine,
  db = getDb(),
}: CreateRunRecordArgs): Promise<WorkflowRun> {
  try {
    const [inserted] = await db
      .insert(workflowRuns)
      .values({
        id: runId,
        orgId,
        workflowId,
        workflowVersionId: workflowVersionId ?? null,
        engine,
        status: "queued",
      })
      .returning();
    return inserted;
  } catch (error) {
    if (!isPrimaryKeyViolation(error)) throw error;
    // Idempotent re-submit: return the existing row.
    const [existing] = await db
      .select()
      .from(workflowRuns)
      .where(sql`${workflowRuns.id} = ${runId}`);
    if (!existing) throw error;
    return existing;
  }
}

function isPrimaryKeyViolation(error: unknown): boolean {
  const code =
    (error as { code?: string })?.code ??
    (error as { cause?: { code?: string } })?.cause?.code;
  return code === "23505";
}

/**
 * Resolve which engine owns a run by its engine-neutral id. Returns the
 * `engine` discriminator from workflow_runs, or undefined when no row exists
 * (legacy Trigger.dev runs predate the run table and have no row). Callers
 * treat undefined as legacy.
 */
export async function getRunEngine(
  runId: string,
  db: VersioningDb = getDb(),
): Promise<ExecutionEngine | undefined> {
  const [row] = await db
    .select({ engine: workflowRuns.engine })
    .from(workflowRuns)
    .where(sql`${workflowRuns.id} = ${runId}`);
  if (!row) return undefined;
  return row.engine === "evo" ? "evo" : "legacy";
}
