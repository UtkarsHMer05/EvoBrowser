import { createHash } from "node:crypto";

import { and, desc, eq, sql } from "drizzle-orm";

import { getDb } from "@/lib/db";
import {
  WorkflowGraph,
  workflowVersions,
  type WorkflowVersion,
} from "@/lib/db/schema";
import { saveWorkflowGraph } from "@/features/workflows/data";

// ---------------------------------------------------------------------------
// Phase 2 — immutable workflow versions (Milestone 20).
//
// A workflow version is an immutable snapshot of the canonical graph taken at
// run-submission time. Runs reference a version id, so a run always executes
// the exact graph the user approved even while collaborators keep editing the
// live (Liveblocks) copy. Version rows are never mutated after creation.
//
// Concurrency model:
//  - The unique constraint (workflow_id, version_number) is the invariant
//    guard. Two concurrent snapshot creations race on the same next number;
//    the loser gets a unique violation, re-reads the max, and retries
//    (bounded). This is safe on the pooled neon-http driver, which offers no
//    multi-statement transactions.
//  - Identical graphs are deduplicated by graph_hash: if the latest version
//    already has the same canonical hash, it is reused instead of inserting a
//    duplicate snapshot (rerun-without-edit references the same version).
//
// Every function takes an optional `db` so integration tests can run against
// the local Phase-2 Postgres via the node-postgres adapter; production uses
// the default getDb() (neon-http).
//
// Timestamps are wall-clock UTC (database now()), consistent with M19.
// ---------------------------------------------------------------------------

// The drizzle instance type. Tests pass a node-postgres-backed instance
// (structurally compatible query builder) cast to this type.
export type VersioningDb = ReturnType<typeof getDb>;

export class WorkflowVersionConflictError extends Error {
  constructor(message: string) {
    super(message);
    this.name = "WorkflowVersionConflictError";
  }
}

// Canonical hash of a graph snapshot. Deterministic: nodes/edges are
// serialized in a stable order (nodes by id, edges by from->to) so two
// structurally identical graphs hash identically regardless of array order.
export function canonicalGraphHash(graph: WorkflowGraph): string {
  const nodes = [...graph.nodes].sort((a, b) => a.id.localeCompare(b.id));
  const edges = [...graph.edges].sort((a, b) => {
    const bySource = a.source.localeCompare(b.source);
    return bySource !== 0 ? bySource : a.target.localeCompare(b.target);
  });
  const canonical = JSON.stringify({ nodes, edges });
  return createHash("sha256").update(canonical).digest("hex");
}

const MAX_VERSION_RETRIES = 5;

function isUniqueViolation(error: unknown): boolean {
  const code = (e: unknown): string | undefined =>
    typeof e === "object" && e !== null && "code" in e
      ? (e as { code?: string }).code
      : undefined;
  // node-postgres surfaces the raw pg error; drizzle wraps it in a
  // DrizzleQueryError with the original on `.cause`. Check both.
  if (code(error) === "23505") return true;
  const cause =
    typeof error === "object" && error !== null && "cause" in error
      ? (error as { cause?: unknown }).cause
      : undefined;
  return code(cause) === "23505";
}

// Create (or reuse) an immutable snapshot for a workflow's current graph.
// Returns the version row. Never mutates an existing version row.
export async function createWorkflowVersion({
  orgId,
  workflowId,
  graph,
  db = getDb(),
}: {
  orgId: string;
  workflowId: string;
  graph: WorkflowGraph;
  db?: VersioningDb;
}): Promise<WorkflowVersion> {
  const graphHash = canonicalGraphHash(graph);

  // Deduplicate: if the newest version already matches this exact graph,
  // reference it instead of inserting a duplicate snapshot.
  const [latest] = await db
    .select()
    .from(workflowVersions)
    .where(
      and(
        eq(workflowVersions.workflowId, workflowId),
        eq(workflowVersions.orgId, orgId),
      ),
    )
    .orderBy(desc(workflowVersions.versionNumber))
    .limit(1);

  if (latest && latest.graphHash === graphHash) {
    return latest;
  }

  let candidate = (latest?.versionNumber ?? 0) + 1;
  for (let attempt = 0; attempt < MAX_VERSION_RETRIES; attempt++) {
    try {
      const [inserted] = await db
        .insert(workflowVersions)
        .values({
          workflowId,
          orgId,
          versionNumber: candidate,
          graph,
          graphHash,
        })
        .returning();
      return inserted;
    } catch (error) {
      if (!isUniqueViolation(error)) throw error;
      // A concurrent writer claimed this version number; re-read the max and
      // retry with a fresh candidate.
      const maxNow = await maxVersionNumber(workflowId, db);
      candidate = Math.max(candidate, maxNow) + 1;
    }
  }
  throw new WorkflowVersionConflictError(
    `Could not allocate a workflow version number for workflow ${workflowId} after ${MAX_VERSION_RETRIES} attempts.`,
  );
}

// Fetch an immutable snapshot by id, scoped to the org (tenant guard).
export async function getWorkflowVersion(
  orgId: string,
  versionId: string,
  db: VersioningDb = getDb(),
): Promise<WorkflowVersion | undefined> {
  const [row] = await db
    .select()
    .from(workflowVersions)
    .where(
      and(
        eq(workflowVersions.id, versionId),
        eq(workflowVersions.orgId, orgId),
      ),
    );
  return row;
}

// List versions newest-first for the version history UI (M27+).
export function listWorkflowVersions(
  orgId: string,
  workflowId: string,
  db: VersioningDb = getDb(),
) {
  return db
    .select()
    .from(workflowVersions)
    .where(
      and(
        eq(workflowVersions.workflowId, workflowId),
        eq(workflowVersions.orgId, orgId),
      ),
    )
    .orderBy(desc(workflowVersions.versionNumber));
}

// Optimistic-concurrency guard for canonical saves. If the caller supplies the
// version number they last saw, and the canonical row has moved past it, the
// save is rejected with a clear conflict instead of silently overwriting a
// newer version. Callers that pass no expectedVersion keep Phase-1 behavior.
//
// `saveFn` is injectable so integration tests can exercise the conflict logic
// against local Postgres without touching Neon; production defaults to the
// Phase-1 saveWorkflowGraph.
export async function saveWorkflowGraphOptimistic({
  orgId,
  id,
  graph,
  expectedVersion,
  db = getDb(),
  saveFn = saveWorkflowGraph,
}: {
  orgId: string;
  id: string;
  graph: WorkflowGraph;
  expectedVersion?: number;
  db?: VersioningDb;
  saveFn?: (args: {
    orgId: string;
    id: string;
    graph: WorkflowGraph;
  }) => Promise<void>;
}): Promise<void> {
  if (expectedVersion !== undefined) {
    const [current] = await db
      .select({ versionNumber: workflowVersions.versionNumber })
      .from(workflowVersions)
      .where(
        and(
          eq(workflowVersions.workflowId, id),
          eq(workflowVersions.orgId, orgId),
        ),
      )
      .orderBy(desc(workflowVersions.versionNumber))
      .limit(1);

    const currentVersion = current?.versionNumber ?? 0;
    if (currentVersion > expectedVersion) {
      throw new WorkflowVersionConflictError(
        `This workflow was saved by someone else (version ${currentVersion} > your ${expectedVersion}). Reload the latest graph and re-apply your changes.`,
      );
    }
  }

  await saveFn({ orgId, id, graph });
}

// Reference helper used by tests and the run-submission hook to assert the
// monotonic-version invariant directly in SQL.
export async function maxVersionNumber(
  workflowId: string,
  db: VersioningDb = getDb(),
): Promise<number> {
  const [row] = await db
    .select({
      max: sql<number>`coalesce(max(${workflowVersions.versionNumber}), 0)`,
    })
    .from(workflowVersions)
    .where(eq(workflowVersions.workflowId, workflowId));
  return Number(row?.max ?? 0);
}
