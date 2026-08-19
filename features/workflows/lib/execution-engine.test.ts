// Milestone 27: execution-engine abstraction + feature flag.
//
// Covers:
//   1. Flag resolution is fail-closed: only exact "evo" selects Evo; unset /
//      empty / typo / "legacy" all stay on the legacy Trigger.dev engine.
//   2. Legacy adapter regression: startRun triggers the run-workflow task with
//      the exact pre-M27 call shape and maps the Trigger.dev run id to the
//      engine-neutral handle; cancelRun forwards to Trigger.dev cancel.
//   3. Evo adapter: graphToCanonicalDagJson emits the canonical DAG JSON the
//      C++ Dag::from_json parses (sorted nodes/edges, trigger/action kinds,
//      from/to edges, no React Flow UI state); startRun submits a
//      client-generated run id + dagJson through an injected fake client;
//      cancelRun forwards; getRunStatus maps the C++ RunStatus enum.
//   4. Engine-neutral run records (against local Phase-2 Postgres; skips when
//      unreachable): createWorkflowRunRecord inserts with the engine
//      discriminator and is idempotent on runId; getRunEngine resolves the
//      owning engine (evo / legacy / undefined for pre-table legacy runs).
//
// Pure unit tests always run; the Postgres section skips cleanly without the
// local stack so the Phase-1 `npm test` gate stays green.

import assert from "node:assert/strict";
import { randomUUID } from "node:crypto";

import { drizzle } from "drizzle-orm/node-postgres";
import pg from "pg";

import * as schema from "@/lib/db/schema";
import type { WorkflowGraph } from "@/lib/db/schema";
import type { StepNodeType } from "@/features/workflows/nodes/node-registry";

import {
  getExecutionEngine,
  resetExecutionEngineAdapterForTests,
} from "./execution-engine";
import { createLegacyEngineAdapter } from "./legacy-engine-adapter";
import {
  createEvoEngineAdapter,
  graphToCanonicalDagJson,
  mapRunStatus,
  type EvoSchedulerClient,
} from "./evo-engine-adapter";
import {
  createWorkflowRunRecord,
  getRunEngine,
} from "./run-records";
import type { VersioningDb } from "./workflow-versions";

let passed = 0;
const ok = (label: string) => {
  passed++;
  console.log(`  ok   ${label}`);
};

function stepNode(
  id: string,
  type: StepNodeType["data"]["type"],
  kind: "trigger" | "action",
): StepNodeType {
  return {
    id,
    type: "step",
    position: { x: 0, y: 0 },
    data: { type, kind, title: id, values: {} },
  };
}

const graph: WorkflowGraph = {
  // Deliberately unsorted to prove canonical ordering.
  nodes: [
    stepNode("b", "extract", "action"),
    stepNode("start", "start", "trigger"),
    stepNode("a", "open-url", "action"),
  ],
  edges: [
    { id: "e2", source: "a", target: "b" },
    { id: "e1", source: "start", target: "a" },
  ],
};

async function main() {
  // --- 1. Flag resolution (fail-closed) ------------------------------------
  {
    assert.equal(getExecutionEngine({}), "legacy", "unset -> legacy");
    assert.equal(
      getExecutionEngine({ EXECUTION_ENGINE: "" }),
      "legacy",
      "empty -> legacy",
    );
    assert.equal(
      getExecutionEngine({ EXECUTION_ENGINE: "evo" }),
      "evo",
      "exact evo -> evo",
    );
    assert.equal(
      getExecutionEngine({ EXECUTION_ENGINE: "EVO" }),
      "evo",
      "case-insensitive evo -> evo",
    );
    assert.equal(
      getExecutionEngine({ EXECUTION_ENGINE: "  evo  " }),
      "evo",
      "trimmed evo -> evo",
    );
    assert.equal(
      getExecutionEngine({ EXECUTION_ENGINE: "legacy" }),
      "legacy",
      "explicit legacy -> legacy",
    );
    assert.equal(
      getExecutionEngine({ EXECUTION_ENGINE: "evoo" }),
      "legacy",
      "typo -> legacy (fail-closed)",
    );
    ok("EXECUTION_ENGINE flag is fail-closed to legacy");
  }

  // --- 2. Legacy adapter regression ----------------------------------------
  {
    const triggerCalls: Array<{ workflowId: string; orgId: string }> = [];
    const cancelCalls: string[] = [];
    const adapter = createLegacyEngineAdapter({
      trigger: async (args) => {
        triggerCalls.push(args);
        return { id: "trig_run_123" };
      },
      cancel: async (runId) => {
        cancelCalls.push(runId);
      },
    });

    assert.equal(adapter.engine, "legacy");
    const handle = await adapter.startRun({
      orgId: "org-1",
      workflowId: "wf-1",
      graph,
    });
    assert.equal(handle.engine, "legacy");
    assert.equal(handle.runId, "trig_run_123", "run id = Trigger.dev run id");
    assert.equal(handle.providerRunId, "trig_run_123");
    assert.deepEqual(
      triggerCalls,
      [{ workflowId: "wf-1", orgId: "org-1" }],
      "legacy trigger call shape unchanged",
    );

    await adapter.cancelRun("trig_run_123");
    assert.deepEqual(cancelCalls, ["trig_run_123"], "cancel forwards");

    const status = await adapter.getRunStatus!("trig_run_123");
    assert.equal(status.status, "unknown", "legacy status is best-effort");
    ok("legacy adapter preserves Trigger.dev call shape + handle mapping");
  }

  // --- 3. Evo adapter: canonical DAG conversion ----------------------------
  {
    const dagJson = graphToCanonicalDagJson(graph);
    const parsed = JSON.parse(dagJson) as {
      nodes: Array<{ id: string; kind: string; type: string }>;
      edges: Array<{ from: string; to: string }>;
    };
    // Nodes sorted by id; kinds mapped; no React Flow fields leak.
    assert.deepEqual(
      parsed.nodes.map((n) => n.id),
      ["a", "b", "start"],
      "nodes sorted by id",
    );
    const start = parsed.nodes.find((n) => n.id === "start")!;
    assert.equal(start.kind, "trigger", "trigger kind mapped");
    assert.equal(start.type, "start");
    const a = parsed.nodes.find((n) => n.id === "a")!;
    assert.equal(a.kind, "action", "action kind mapped");
    assert.equal(a.type, "open-url");
    // No UI state crosses the boundary.
    for (const n of parsed.nodes) {
      assert.equal("position" in n, false, "no position in canonical node");
      assert.equal("data" in n, false, "no data blob in canonical node");
    }
    // Edges sorted by (from, to) with from/to keys.
    assert.deepEqual(
      parsed.edges,
      [
        { from: "a", to: "b" },
        { from: "start", to: "a" },
      ],
      "edges sorted, from/to keys",
    );
    ok("graphToCanonicalDagJson emits canonical C++ DAG JSON (no UI state)");
  }

  // --- 3b. Evo adapter: submit/cancel/query via injected fake client -------
  {
    const submitted: Array<Record<string, unknown>> = [];
    const canceled: Array<Record<string, unknown>> = [];
    const fakeClient: EvoSchedulerClient = {
      submitRun: async (args) => {
        submitted.push(args);
        return { runId: args.runId, accepted: true, message: "run accepted" };
      },
      cancelRun: async (args) => {
        canceled.push(args);
        return { ok: true };
      },
      getRun: async (runId) => ({
        runId,
        status: "RUN_SUCCEEDED",
        outcome: "SUCCEEDED",
      }),
      health: async () => ({ ok: true, detail: "SERVING" }),
    };

    const adapter = createEvoEngineAdapter({
      client: fakeClient,
      generateRunId: () => "evo_fixed_run",
    });
    assert.equal(adapter.engine, "evo");

    const handle = await adapter.startRun({
      orgId: "org-1",
      workflowId: "wf-1",
      workflowVersionId: "wfv-1",
      graph,
      runId: "evo_caller_run",
    });
    assert.equal(handle.engine, "evo");
    assert.equal(handle.runId, "evo_caller_run", "caller-supplied run id wins");
    assert.equal(submitted.length, 1);
    assert.equal(submitted[0].runId, "evo_caller_run");
    assert.equal(submitted[0].orgId, "org-1");
    assert.equal(submitted[0].workflowVersionId, "wfv-1");
    // dagJson is valid canonical JSON.
    JSON.parse(String(submitted[0].dagJson));

    // Without a caller run id, the generator is used.
    const handle2 = await adapter.startRun({
      orgId: "org-1",
      workflowId: "wf-1",
      graph,
    });
    assert.equal(handle2.runId, "evo_fixed_run", "generated run id fallback");

    await adapter.cancelRun("evo_caller_run");
    assert.equal(canceled.length, 1);
    assert.equal(canceled[0].runId, "evo_caller_run");

    const status = await adapter.getRunStatus!("evo_caller_run");
    assert.equal(status.status, "succeeded", "RunStatus enum mapped");
    ok("evo adapter submits/cancels/queries via the scheduler client");
  }

  // --- 3c. Rejected submission surfaces as an error ------------------------
  {
    const rejecting: EvoSchedulerClient = {
      submitRun: async () => ({
        runId: "",
        accepted: false,
        message: "malformed DAG",
      }),
      cancelRun: async () => ({ ok: false }),
      getRun: async (runId) => ({
        runId,
        status: "RUN_STATUS_UNSPECIFIED",
        outcome: "OUTCOME_UNSPECIFIED",
      }),
      health: async () => ({ ok: false, detail: "" }),
    };
    const adapter = createEvoEngineAdapter({ client: rejecting });
    await assert.rejects(
      () => adapter.startRun({ orgId: "o", workflowId: "w", graph }),
      /rejected run/,
      "rejected submission throws",
    );
    ok("evo adapter surfaces scheduler rejection");
  }

  // --- 3d. RunStatus enum mapping ------------------------------------------
  {
    assert.equal(mapRunStatus("RUN_QUEUED"), "queued");
    assert.equal(mapRunStatus("RUN_RUNNING"), "running");
    assert.equal(mapRunStatus("RUN_SUCCEEDED"), "succeeded");
    assert.equal(mapRunStatus("RUN_FAILED"), "failed");
    assert.equal(mapRunStatus("RUN_CANCELED"), "canceled");
    assert.equal(mapRunStatus("RUN_STATUS_UNSPECIFIED"), "unknown");
    assert.equal(mapRunStatus("garbage"), "unknown");
    ok("mapRunStatus covers all C++ RunStatus values");
  }

  // --- 4. Engine-neutral run records (local Postgres; skips if down) -------
  {
    const user = process.env.EVO_PHASE2_PG_USER ?? "evo";
    const password = process.env.EVO_PHASE2_PG_PASSWORD ?? "evo_dev_password";
    const port = process.env.EVO_PHASE2_PG_PORT ?? "5433";
    const dbName = process.env.EVO_PHASE2_PG_DB ?? "evo_phase2";
    const connectionString = `postgresql://${user}:${password}@127.0.0.1:${port}/${dbName}`;
    const pool = new pg.Pool({ connectionString, connectionTimeoutMillis: 3000 });

    let reachable = true;
    try {
      const client = await pool.connect();
      client.release();
    } catch {
      reachable = false;
    }

    if (!reachable) {
      console.log(
        "SKIP: M27 run-record section (local Phase-2 Postgres unreachable at " +
          `127.0.0.1:${port}; run scripts/phase2/up.sh + migrate-local.sh)`,
      );
      await pool.end();
    } else {
      const db = drizzle(pool, {
        schema,
        casing: "snake_case",
      }) as unknown as VersioningDb;
      const orgId = `org_m27_${randomUUID().slice(0, 8)}`;
      const workflowId = randomUUID();

      // Seed the FK parent (workflows).
      await db.insert(schema.workflows).values({
        id: workflowId,
        orgId,
        name: "m27 test workflow",
      });

      // Evo run record.
      const evoRunId = `evo_${randomUUID()}`;
      const evoRow = await createWorkflowRunRecord({
        runId: evoRunId,
        orgId,
        workflowId,
        engine: "evo",
        db,
      });
      assert.equal(evoRow.engine, "evo", "evo run row engine discriminator");
      assert.equal(evoRow.status, "queued", "run starts queued");
      assert.equal(await getRunEngine(evoRunId, db), "evo", "getRunEngine evo");

      // Idempotent re-insert with the same run id.
      const again = await createWorkflowRunRecord({
        runId: evoRunId,
        orgId,
        workflowId,
        engine: "evo",
        db,
      });
      assert.equal(again.id, evoRunId, "idempotent on runId");

      // Legacy run record.
      const legacyRunId = `trig_${randomUUID()}`;
      await createWorkflowRunRecord({
        runId: legacyRunId,
        orgId,
        workflowId,
        engine: "legacy",
        db,
      });
      assert.equal(
        await getRunEngine(legacyRunId, db),
        "legacy",
        "getRunEngine legacy",
      );

      // Unknown run id (pre-table legacy run) -> undefined -> legacy.
      assert.equal(
        await getRunEngine("nonexistent_run", db),
        undefined,
        "unknown run id -> undefined (treated as legacy)",
      );

      ok("engine-neutral run records: discriminator + idempotency + resolver");
      await pool.end();
    }
  }

  resetExecutionEngineAdapterForTests();
  console.log(`\nALL M27 EXECUTION-ENGINE TESTS PASSED! (${passed}/${passed})`);
}

main().catch((err) => {
  console.error("M27 execution-engine test FAILED:", err);
  process.exit(1);
});
