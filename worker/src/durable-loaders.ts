// Phase 2 — durable Postgres-backed loaders for the worker executor adapter
// (Milestone 29).
//
// The M24 node-executor adapter takes injectable loaders for the immutable
// version snapshot and the predecessor outputs. M24 tests used in-memory
// fakes; this module plugs in the REAL durable readers against the local
// Phase-2 Postgres — the same store the C++ run loop persists node outputs to.
//
//   loadVersion              — workflow_versions by id (immutable snapshot).
//   loadPredecessorOutputs   — for a node, the outputs of its predecessor
//                              nodes, read from node_runs.output.
//
// Secrets stay server/worker-only: this module reads only graph + output data;
// it never touches credentials. Connection settings mirror the worker / C++
// PgRunStore defaults (EVO_PHASE2_PG_* env vars).

import { and, eq, inArray } from "drizzle-orm";

import { getPhase2Db } from "@/lib/db/phase2";
import {
  nodeRuns,
  workflowRuns,
  workflowVersions,
  type WorkflowGraph,
} from "@/lib/db/schema";
import type { NodeOutputs } from "@/features/workflows/lib/interpolate";

import type {
  PredecessorOutputsLoader,
  WorkflowVersionLoader,
} from "./node-executor-adapter";

/** Load the immutable version snapshot for a task (workflow_versions by id). */
export const loadVersion: WorkflowVersionLoader = async ({
  workflowVersionId,
}) => {
  if (!workflowVersionId) return undefined;
  const db = getPhase2Db();
  const [row] = await db
    .select()
    .from(workflowVersions)
    .where(eq(workflowVersions.id, workflowVersionId));
  if (!row) return undefined;
  return {
    workflowVersionId: row.id,
    graph: row.graph as WorkflowGraph,
  };
};

/**
 * Load the outputs of a node's predecessors, keyed by predecessor node id.
 * Determines predecessors from the run's version snapshot edges (P -> nodeId),
 * then reads each predecessor's durable output from node_runs.output. A
 * predecessor with no output yet (or a failed/canceled one) is omitted,
 * matching the legacy interpolation behavior (missing => "" placeholder).
 */
export const loadPredecessorOutputs: PredecessorOutputsLoader = async ({
  runId,
  nodeId,
}) => {
  const db = getPhase2Db();
  const outputs: NodeOutputs = {};

  // Resolve the run's version id.
  const [run] = await db
    .select({ workflowVersionId: workflowRuns.workflowVersionId })
    .from(workflowRuns)
    .where(eq(workflowRuns.id, runId));
  if (!run?.workflowVersionId) return outputs;

  const [version] = await db
    .select()
    .from(workflowVersions)
    .where(eq(workflowVersions.id, run.workflowVersionId));
  if (!version) return outputs;

  const graph = version.graph as WorkflowGraph;
  const predecessors = graph.edges
    .filter((e) => e.target === nodeId)
    .map((e) => e.source);
  if (predecessors.length === 0) return outputs;

  const rows = await db
    .select({ nodeId: nodeRuns.nodeId, output: nodeRuns.output })
    .from(nodeRuns)
    .where(
      and(
        eq(nodeRuns.runId, runId),
        inArray(nodeRuns.nodeId, predecessors),
      ),
    );

  for (const row of rows) {
    if (row.output !== null && row.output !== undefined) {
      outputs[row.nodeId] = row.output;
    }
  }
  return outputs;
};
