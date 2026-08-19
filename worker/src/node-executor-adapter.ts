// Phase 2 — worker-side execution adapter (Milestone 24).
//
// Wraps the EXISTING Phase-1 node registry/executors and interpolation behind
// the worker's TaskExecutor interface, so distributed workers run product
// nodes without reimplementing them (master prompt: "Do not rewrite Stagehand
// in C++ / do not duplicate executors").
//
// Per-task flow (mirrors run-workflow.ts semantics for a single node):
//   1. Load the immutable workflow version snapshot (by workflowVersionId).
//   2. Find the node in that snapshot by nodeId -> its type + values template.
//   3. Load predecessor outputs from durable node-run state.
//   4. Interpolate `{{ nodeId.path }}` placeholders using the existing
//      interpolate() implementation.
//   5. Call the existing executor for the node type.
//   6. Return the executor's result as opaque JSON through the result envelope.
//
// Secrets stay server/worker-only: RESEND_API_KEY / BROWSERBASE_API_KEY are
// read from the worker process environment by the executors themselves. They
// are NEVER carried in the task envelope (M22 payload rule).
//
// Testability: the version snapshot and predecessor outputs come from
// injectable loaders. M24 tests use in-memory fakes; M26 plugs in the durable
// Postgres-backed loaders. Side-effecting executors (email) can be swapped via
// `executorOverrides` — the safe test sink for automated distributed tests.

import type { Stagehand } from "@browserbasehq/stagehand";

import {
  interpolate,
  type NodeOutputs,
} from "@/features/workflows/lib/interpolate";
import {
  nodeExecutors,
  type NodeExecutor,
} from "@/features/workflows/nodes/node-executors";
import type { NodeType } from "@/features/workflows/nodes/node-registry";
import type { WorkflowGraph } from "@/lib/db/schema";

import { ErrorClass } from "./envelope-codec";
import type { TaskEnvelopeView } from "./envelope-codec";
import type { ExecutorResult, TaskExecutor } from "./worker";

/** The immutable snapshot a run executes (loaded by workflowVersionId). */
export interface WorkflowVersionSnapshot {
  workflowVersionId: string;
  graph: WorkflowGraph;
}

/** Loads the immutable version snapshot for a task. */
export type WorkflowVersionLoader = (args: {
  orgId: string;
  workflowVersionId: string;
}) => Promise<WorkflowVersionSnapshot | undefined>;

/** Loads the outputs of a node's predecessors, keyed by node id. */
export type PredecessorOutputsLoader = (args: {
  runId: string;
  nodeId: string;
}) => Promise<NodeOutputs>;

export interface NodeExecutorAdapterOptions {
  loadVersion: WorkflowVersionLoader;
  loadPredecessorOutputs: PredecessorOutputsLoader;
  /** Provides the run's owned Stagehand session (wired in M25). Optional so
   *  non-browser nodes run without a browser. */
  getStagehand?: () => Promise<Stagehand>;
  /** Test sink / override map. When set for a node type, it replaces the
   *  registered executor (used to mock side-effecting email in tests). */
  executorOverrides?: Partial<Record<NodeType, NodeExecutor>>;
}

export function createNodeExecutorAdapter(
  options: NodeExecutorAdapterOptions,
): TaskExecutor {
  return async (task: TaskEnvelopeView, signal: AbortSignal) => {
    // 1. Load the immutable version snapshot.
    const version = await options.loadVersion({
      orgId: task.orgId,
      workflowVersionId: task.workflowVersionId,
    });
    if (!version) {
      return permanentFailure(
        `workflow version not found: ${task.workflowVersionId}`,
      );
    }

    // 2. Locate the node in the approved snapshot.
    const node = version.graph.nodes.find((n) => n.id === task.nodeId);
    if (!node) {
      return permanentFailure(`node not found in version: ${task.nodeId}`);
    }
    const nodeType = node.data.type;

    // Trigger nodes (e.g. "start") do no work and produce no output — same as
    // the legacy run loop, which marks them done without an executor.
    const executor =
      options.executorOverrides?.[nodeType] ?? nodeExecutors[nodeType];
    if (!executor) {
      return { completed: true, output: JSON.stringify({ skipped: true }) };
    }

    if (signal.aborted) {
      return {
        completed: false,
        error: "canceled before execution",
        errorClass: ErrorClass.ERROR_CANCELED,
        retryable: false,
      };
    }

    // 3. Load predecessor outputs for interpolation.
    const outputs = await options.loadPredecessorOutputs({
      runId: task.runId,
      nodeId: task.nodeId,
    });

    // 4. Interpolate the node's values template (existing implementation).
    const values = Object.fromEntries(
      Object.entries(node.data.values).map(([key, text]) => [
        key,
        interpolate({ text, outputs }),
      ]),
    );

    // 5. Call the existing executor.
    const getStagehand =
      options.getStagehand ??
      (async () => {
        throw new Error(
          `node ${task.nodeId} (${nodeType}) requires a browser session, but none is available on this worker`,
        );
      });

    try {
      const output = await executor({ values, getStagehand });
      // 6. Opaque JSON output through the result envelope.
      return { completed: true, output: JSON.stringify(output ?? null) };
    } catch (err) {
      return {
        completed: false,
        error: err instanceof Error ? err.message : String(err),
        errorClass: ErrorClass.ERROR_TRANSIENT,
        retryable: true,
      };
    }
  };
}

function permanentFailure(error: string): ExecutorResult {
  return {
    completed: false,
    error,
    errorClass: ErrorClass.ERROR_PERMANENT,
    retryable: false,
  };
}
