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
import { getPhase2Db } from "@/lib/db/phase2";
import { workflowRuns, workflows, type WorkflowRun } from "@/lib/db/schema";

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
 * Ensure the workflow FK row exists in the Phase-2 (local) database before an
 * Evo run row references it. workflow_runs.workflow_id references workflows.id,
 * and the Phase-2 workflows table is separate from the app's Neon copy — the
 * C++ engine's `ensure_workflow` does the same idempotent insert. Mirrors it so
 * an Evo run row can be created without a FK violation. Idempotent on the PK.
 */
export async function ensurePhase2Workflow(
  db: VersioningDb,
  args: { id: string; orgId: string; name: string },
): Promise<void> {
  try {
    await db
      .insert(workflows)
      .values({ id: args.id, orgId: args.orgId, name: args.name })
      .onConflictDoNothing();
  } catch (error) {
    if (!isPrimaryKeyViolation(error)) throw error;
  }
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

/**
 * Resolve the owning engine across BOTH run stores (Milestone 29). Evo run
 * rows live in the local Phase-2 Postgres; legacy run rows (best-effort) live
 * in Neon. Checks the Phase-2 store first (Evo runs are the ones that need
 * routing), then Neon. Returns undefined when neither store knows the run —
 * callers treat that as legacy (pre-table Trigger.dev runs).
 *
 * Each lookup is fail-open: if a store is unreachable it is skipped rather
 * than failing the cancel, so a Phase-2 outage never breaks legacy Stop.
 */
export async function resolveRunEngine(
  runId: string,
  args: {
    phase2Db?: VersioningDb;
    legacyDb?: VersioningDb;
  } = {},
): Promise<ExecutionEngine | undefined> {
  const stores: VersioningDb[] = [];
  try {
    stores.push(args.phase2Db ?? getPhase2Db());
  } catch {
    // Phase-2 store unavailable — fall through to legacy.
  }
  stores.push(args.legacyDb ?? getDb());

  for (const db of stores) {
    try {
      const engine = await getRunEngine(runId, db);
      if (engine) return engine;
    } catch {
      // Store unreachable or table missing — try the next store.
    }
  }
  return undefined;
}
