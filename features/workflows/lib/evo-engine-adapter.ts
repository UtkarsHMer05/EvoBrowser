// Phase 2 — Evo engine adapter (Milestone 27).
//
// Talks to the C++ scheduler service (engine/app/grpc_service.cpp) over gRPC
// using the shared evo.execution.v1 contract. This is the opt-in Evo path;
// the legacy Trigger.dev engine remains the default and is untouched.
//
// Separation of concerns:
//   - graphToCanonicalDagJson(): pure conversion from the React Flow
//     WorkflowGraph to the canonical DAG JSON the C++ Dag::from_json expects.
//     No gRPC, no I/O — unit-testable in isolation.
//   - EvoSchedulerClient: the gRPC plumbing (submit/cancel/get/health),
//     injectable so tests can fake it without a live scheduler.
//   - createEvoEngineAdapter(): maps the client onto the engine-neutral
//     adapter interface.
//
// The engine-neutral run id for Evo is the client-generated run id we submit
// (idempotent on the C++ side). It is deliberately separate from any provider
// id; there is no Trigger.dev run in this path.

import { randomUUID } from "node:crypto";

import type { WorkflowGraph } from "@/lib/db/schema";

import type {
  EngineRunHandle,
  EngineRunStatus,
  ExecutionEngineAdapter,
  StartRunArgs,
} from "./execution-engine";

/**
 * Convert a React Flow WorkflowGraph to the canonical DAG JSON the C++
 * scheduler's Dag::from_json parses:
 *   {"nodes":[{"id","kind":"trigger"|"action","type"}],
 *    "edges":[{"from","to"}]}
 *
 * Only scheduler-relevant fields cross the boundary — no React Flow UI state
 * (coordinates, selection) and no secrets. Deterministic: nodes sorted by id,
 * edges sorted by (from, to), matching the C++ canonical serialization.
 */
export function graphToCanonicalDagJson(graph: WorkflowGraph): string {
  const nodes = [...graph.nodes]
    .sort((a, b) => a.id.localeCompare(b.id))
    .map((n) => ({
      id: n.id,
      kind: n.data.kind === "trigger" ? "trigger" : "action",
      type: n.data.type,
    }));
  const edges = [...graph.edges]
    .map((e) => ({ from: e.source, to: e.target }))
    .sort((a, b) =>
      a.from !== b.from ? a.from.localeCompare(b.from) : a.to.localeCompare(b.to),
    );
  return JSON.stringify({ nodes, edges });
}

/** Minimal view of the C++ ControlService the adapter needs. */
export interface EvoSchedulerClient {
  submitRun(args: {
    orgId: string;
    workflowVersionId: string;
    runId: string;
    dagJson: string;
    traceId: string;
  }): Promise<{ runId: string; accepted: boolean; message: string }>;
  cancelRun(args: {
    runId: string;
    reason: string;
    traceId: string;
  }): Promise<{ ok: boolean }>;
  getRun(runId: string): Promise<{
    runId: string;
    status: string; // RunStatus enum name, e.g. "RUN_RUNNING"
    outcome: string; // RunOutcome enum name
  }>;
  health(): Promise<{ ok: boolean; detail: string }>;
}

/** Map the C++ RunStatus enum name to the engine-neutral status string. */
export function mapRunStatus(status: string): EngineRunStatus["status"] {
  switch (status) {
    case "RUN_QUEUED":
      return "queued";
    case "RUN_RUNNING":
      return "running";
    case "RUN_SUCCEEDED":
      return "succeeded";
    case "RUN_FAILED":
      return "failed";
    case "RUN_CANCELED":
      return "canceled";
    default:
      return "unknown";
  }
}

export interface EvoEngineAdapterOptions {
  /** Injectable client (tests). Defaults to a live gRPC client. */
  client?: EvoSchedulerClient;
  /** Scheduler address; defaults to EVO_SCHEDULER_ADDR or 127.0.0.1:50051. */
  schedulerAddr?: string;
  /** Injectable run-id generator (tests). */
  generateRunId?: () => string;
}

export function createEvoEngineAdapter(
  options: EvoEngineAdapterOptions = {},
): ExecutionEngineAdapter {
  // Lazily load the real gRPC client only when one is not injected, so
  // importing this module (and running unit tests) never requires a live
  // scheduler or pulls the gRPC stack into the legacy path. The dynamic
  // import is cached after first resolution.
  let clientPromise: Promise<EvoSchedulerClient> | null = options.client
    ? Promise.resolve(options.client)
    : null;
  const getClient = (): Promise<EvoSchedulerClient> => {
    if (!clientPromise) {
      clientPromise = import("./evo-scheduler-client").then((mod) =>
        mod.createGrpcEvoSchedulerClient(
          options.schedulerAddr ??
            process.env.EVO_SCHEDULER_ADDR ??
            "127.0.0.1:50051",
        ),
      );
    }
    return clientPromise;
  };
  const generateRunId = options.generateRunId ?? (() => `evo_${randomUUID()}`);

  return {
    engine: "evo",

    async startRun(args: StartRunArgs): Promise<EngineRunHandle> {
      const runId = args.runId ?? generateRunId();
      const dagJson = graphToCanonicalDagJson(args.graph);
      const resp = await (await getClient()).submitRun({
        orgId: args.orgId,
        workflowVersionId: args.workflowVersionId ?? "",
        runId,
        dagJson,
        traceId: runId,
      });
      if (!resp.accepted) {
        throw new Error(`Evo scheduler rejected run: ${resp.message}`);
      }
      return { engine: "evo", runId: resp.runId || runId };
    },

    async cancelRun(runId: string): Promise<void> {
      await (await getClient()).cancelRun({
        runId,
        reason: "user requested stop",
        traceId: runId,
      });
    },

    async getRunStatus(runId: string): Promise<EngineRunStatus> {
      const resp = await (await getClient()).getRun(runId);
      return { runId, engine: "evo", status: mapRunStatus(resp.status) };
    },
  };
}
