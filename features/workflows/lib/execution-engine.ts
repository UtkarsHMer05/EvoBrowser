// Phase 2 — execution-engine abstraction + feature flag (Milestone 27).
//
// Connects Phase 2 to the product WITHOUT replacing Phase 1. A server-only
// feature flag selects which engine executes a workflow run:
//
//   EXECUTION_ENGINE=legacy|evo     (default: legacy)
//
// The flag is read server-side only (this module is imported exclusively by
// "use server" actions), is fail-closed — any value other than the exact
// string "evo" resolves to the legacy Trigger.dev engine — and never changes
// Phase-1 default behavior. The planner still never auto-runs; the Run action
// remains the only explicit trigger for either engine.
//
// Both engines sit behind one engine-neutral adapter interface
// (start/cancel/query). Clerk authorization and the Pro-plan gate happen in
// the action BEFORE an adapter is selected, so neither engine can be reached
// without passing auth + plan gating.

import type { WorkflowGraph } from "@/lib/db/schema";

import { createLegacyEngineAdapter } from "./legacy-engine-adapter";
import { createEvoEngineAdapter } from "./evo-engine-adapter";

/** Which engine executes workflow runs. */
export type ExecutionEngine = "legacy" | "evo";

/**
 * Engine-neutral run handle returned to the UI boundary. `runId` is the
 * engine-neutral run id (deliberately separate from any provider id); for the
 * legacy engine it equals the Trigger.dev run id so existing live-view /
 * cancel / replay paths keep working unchanged.
 */
export interface EngineRunHandle {
  engine: ExecutionEngine;
  runId: string;
  /** Provider-specific id when it differs from runId (legacy: Trigger.dev). */
  providerRunId?: string;
}

/** Inputs an engine needs to start a run. Auth/plan gating already happened. */
export interface StartRunArgs {
  orgId: string;
  workflowId: string;
  workflowVersionId?: string;
  graph: WorkflowGraph;
  /**
   * Engine-neutral run id chosen by the caller (used by Evo, which submits a
   * client-generated id). The legacy engine ignores it — its run id is
   * assigned by Trigger.dev and doubles as the engine-neutral id.
   */
  runId?: string;
}

/** Engine-neutral run status for query semantics. */
export interface EngineRunStatus {
  runId: string;
  engine: ExecutionEngine;
  /** Coarse lifecycle: queued | running | succeeded | failed | canceled. */
  status: "queued" | "running" | "succeeded" | "failed" | "canceled" | "unknown";
}

/**
 * The contract every execution engine satisfies. M27 ships two adapters:
 * the legacy Trigger.dev path (behavior-preserving) and the Evo path (gRPC to
 * the C++ scheduler service).
 */
export interface ExecutionEngineAdapter {
  readonly engine: ExecutionEngine;
  /** Start a run. Returns the engine-neutral handle. */
  startRun(args: StartRunArgs): Promise<EngineRunHandle>;
  /** Cancel a run by its engine-neutral run id. */
  cancelRun(runId: string): Promise<void>;
  /** Best-effort status query (M28 wires live UI fan-out). */
  getRunStatus?(runId: string): Promise<EngineRunStatus>;
}

/**
 * Resolve the configured engine. Fail-closed: only the exact string "evo"
 * (case-insensitive, trimmed) selects Evo; everything else — including unset,
 * empty, or a typo — stays on the legacy Trigger.dev engine.
 */
export function getExecutionEngine(
  env: Record<string, string | undefined> = process.env,
): ExecutionEngine {
  const raw = (env.EXECUTION_ENGINE ?? "").trim().toLowerCase();
  return raw === "evo" ? "evo" : "legacy";
}

let cachedAdapter: ExecutionEngineAdapter | null = null;

/**
 * Return the adapter for the configured engine. Cached per process; the flag
 * is read once at first use (server actions are long-lived in the Node
 * runtime). Tests pass an explicit engine to bypass the cache.
 */
export function getExecutionEngineAdapter(
  engine: ExecutionEngine = getExecutionEngine(),
): ExecutionEngineAdapter {
  if (cachedAdapter && cachedAdapter.engine === engine) return cachedAdapter;
  cachedAdapter =
    engine === "evo" ? createEvoEngineAdapter() : createLegacyEngineAdapter();
  return cachedAdapter;
}

/** Test helper: drop the cached adapter (used between flag flips in tests). */
export function resetExecutionEngineAdapterForTests(): void {
  cachedAdapter = null;
}
