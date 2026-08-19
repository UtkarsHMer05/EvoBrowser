// Milestone 20: immutable workflow versions + optimistic concurrency.
//
// Integration test against the LOCAL Phase-2 Postgres container
// (infra/phase2). It exercises:
//   1. concurrent version creation -> unique (workflow_id, version_number)
//      guard + bounded retry yields distinct monotonic version numbers
//   2. stale-save conflict -> saveWorkflowGraphOptimistic rejects a save whose
//      expectedVersion is behind the current version
//   3. run snapshot immutability -> identical graphs dedupe to one version;
//      an edited graph creates a NEW version (rerun-after-edit)
//   4. legacy compatibility -> no expectedVersion keeps Phase-1 save behavior
//
// Skips (exit 0) when the local Phase-2 Postgres is unreachable, so the
// Phase-1 `npm test` regression gate stays green on machines without Docker.
// Run `scripts/phase2/up.sh && scripts/phase2/migrate-local.sh` first.

import assert from "node:assert/strict";
import { randomUUID } from "node:crypto";

import { drizzle } from "drizzle-orm/node-postgres";
import pg from "pg";

import * as schema from "@/lib/db/schema";
import type { WorkflowGraph } from "@/lib/db/schema";
import type { StepNodeType } from "@/features/workflows/nodes/node-registry";
import {
  canonicalGraphHash,
  createWorkflowVersion,
  getWorkflowVersion,
  maxVersionNumber,
  saveWorkflowGraphOptimistic,
  WorkflowVersionConflictError,
  type VersioningDb,
} from "./workflow-versions";

const user = process.env.EVO_PHASE2_PG_USER ?? "evo";
const password = process.env.EVO_PHASE2_PG_PASSWORD ?? "evo_dev_password";
const port = process.env.EVO_PHASE2_PG_PORT ?? "5433";
const dbName = process.env.EVO_PHASE2_PG_DB ?? "evo_phase2";
const connectionString = `postgresql://${user}:${password}@127.0.0.1:${port}/${dbName}`;

function stepNode(id: string, title: string): StepNodeType {
  return {
    id,
    type: "step",
    position: { x: 0, y: 0 },
    data: { type: "start", kind: "trigger", title, values: {} },
  };
}

function graphOf(titles: string[]): WorkflowGraph {
  const nodes = titles.map((t, i) => stepNode(`n${i}`, t));
  const edges = nodes.slice(0, -1).map((n, i) => ({
    id: `e${i}`,
    source: n.id,
    target: nodes[i + 1].id,
  }));
  return { nodes, edges };
}

async function main() {
  const pool = new pg.Pool({ connectionString, connectionTimeoutMillis: 3000 });

  // Reachability probe — skip cleanly if the local stack is not up.
  try {
    const client = await pool.connect();
    client.release();
  } catch {
    console.log(
      "SKIP: M20 versioning integration (local Phase-2 Postgres unreachable at " +
        `127.0.0.1:${port}; run scripts/phase2/up.sh + migrate-local.sh to enable)`,
    );
    await pool.end();
    return;
  }

  const db = drizzle(pool, { schema, casing: "snake_case" }) as unknown as VersioningDb;
  const orgId = `org_m20_${randomUUID().slice(0, 8)}`;

  // Seed a workflow row (FK target for workflow_versions).
  const [workflow] = await db
    .insert(schema.workflows)
    .values({ orgId, name: "m20-versioning" })
    .returning();
  const workflowId = workflow.id;

  let passed = 0;
  const ok = (label: string) => {
    passed++;
    console.log(`  ok   ${label}`);
  };

  try {
    // --- 1. Concurrent version creation -------------------------------
    const graphs = [graphOf(["A"]), graphOf(["B"]), graphOf(["C"])];
    const versions = await Promise.all(
      graphs.map((g) =>
        createWorkflowVersion({ orgId, workflowId, graph: g, db }),
      ),
    );
    const numbers = versions.map((v) => v.versionNumber).sort((a, b) => a - b);
    assert.deepEqual(numbers, [1, 2, 3], "version numbers are 1,2,3");
    assert.equal(new Set(numbers).size, 3, "no duplicate version numbers");
    ok("concurrent creation -> distinct monotonic versions 1,2,3");

    // --- 2. Run snapshot immutability (dedupe + new-on-edit) ----------
    const rerunSame = await createWorkflowVersion({
      orgId,
      workflowId,
      graph: graphs[2], // identical to the latest (version 3)
      db,
    });
    assert.equal(
      rerunSame.versionNumber,
      3,
      "rerun with unchanged graph reuses version 3",
    );
    ok("unchanged rerun reuses the same immutable snapshot");

    const edited = graphOf(["C", "D"]);
    const afterEdit = await createWorkflowVersion({
      orgId,
      workflowId,
      graph: edited,
      db,
    });
    assert.equal(afterEdit.versionNumber, 4, "edit creates version 4");
    assert.notEqual(afterEdit.id, rerunSame.id, "new snapshot is a new row");
    ok("rerun after edit creates a NEW immutable snapshot (v4)");

    // Snapshot content round-trips and is fetchable by id (tenant-scoped).
    const fetched = await getWorkflowVersion(orgId, afterEdit.id, db);
    assert.ok(fetched, "version fetchable by id within org");
    assert.equal(
      fetched!.graphHash,
      canonicalGraphHash(edited),
      "stored hash matches canonical hash",
    );
    const crossTenant = await getWorkflowVersion("org_other", afterEdit.id, db);
    assert.equal(crossTenant, undefined, "tenant guard blocks other org");
    ok("snapshot round-trips; tenant guard enforced");

    // --- 3. Stale-save conflict (optimistic concurrency) --------------
    const noopSave = async () => {}; // don't touch the canonical row in tests
    await assert.rejects(
      saveWorkflowGraphOptimistic({
        orgId,
        id: workflowId,
        graph: edited,
        expectedVersion: 2, // stale: current is 4
        db,
        saveFn: noopSave,
      }),
      WorkflowVersionConflictError,
      "stale expectedVersion is rejected",
    );
    ok("stale save rejected with WorkflowVersionConflictError");

    await saveWorkflowGraphOptimistic({
      orgId,
      id: workflowId,
      graph: edited,
      expectedVersion: 4, // current
      db,
      saveFn: noopSave,
    });
    ok("save at current version succeeds");

    // --- 4. Legacy compatibility (no expectedVersion) -----------------
    await saveWorkflowGraphOptimistic({
      orgId,
      id: workflowId,
      graph: edited,
      db,
      saveFn: noopSave,
    });
    ok("no expectedVersion -> Phase-1 save path (no conflict check)");

    // --- Invariant: max version is monotonic --------------------------
    const maxV = await maxVersionNumber(workflowId, db);
    assert.equal(maxV, 4, "max version number is 4");
    ok("maxVersionNumber reports monotonic max (4)");

    console.log(
      `\nALL M20 VERSIONING INTEGRATION TESTS PASSED! (${passed}/${passed})`,
    );
  } finally {
    // Clean up fixture rows (parameterized, scoped to this test's rows only).
    await pool.query(
      "DELETE FROM workflow_versions WHERE workflow_id = $1",
      [workflowId],
    );
    await pool.query("DELETE FROM workflows WHERE id = $1", [workflowId]);
    await pool.end();
  }
}

main().catch((err) => {
  console.error("M20 versioning test FAILED:", err);
  process.exit(1);
});
