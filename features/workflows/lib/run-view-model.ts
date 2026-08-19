// Phase 2 — normalized frontend run view model (Milestone 28).
//
// One engine-neutral shape the UI consumes, regardless of which engine ran
// the workflow. Both engines map INTO this model:
//   - legacy Trigger.dev realtime runs  -> triggerRunToViewModel()
//   - Evo run events (Redis/SSE)        -> reduceEvoEvents()
//
// The UI (console, live-view panel, results dialog) reads this model through
// the shared provider hooks instead of engine-specific shapes, so existing UI
// concepts can consume Evo runs without pretending they are Trigger.dev runs
// (M28 objective). This module is pure — no I/O, no React — so the mapping is
// unit-testable in isolation.
//
// Timestamps in this model are wall-clock UTC (Date), consistent with the
// durable run/event timestamps. Durations are milliseconds.

import type { NodeType } from "@/features/workflows/nodes/node-registry";

/** Coarse engine-neutral run lifecycle status. */
export type NormalizedRunStatus =
  | "queued"
  | "running"
  | "succeeded"
  | "failed"
  | "canceled"
  | "unknown";

/** One step (node) within a normalized run. */
export interface NormalizedRunStep {
  nodeId: string;
  type: NodeType | string;
  title: string;
  status: "pending" | "running" | "done" | "failed" | "canceled";
  /** Wall-clock executor duration once the step leaves "running". */
  durationMs?: number;
  /** Opaque executor output (JSON), for the per-step detail view. */
  output?: unknown;
  /** Error message when status is "failed". */
  error?: string;
}

/** The engine-neutral run view the UI renders. */
export interface NormalizedRunViewModel {
  /** Engine-neutral run id (separate from any provider id). */
  id: string;
  engine: "legacy" | "evo";
  status: NormalizedRunStatus;
  createdAt?: Date;
  /** True while the run is still producing steps (queued/running). */
  isLive: boolean;
  /** True once the run reached a terminal status. */
  isTerminal: boolean;
  steps: NormalizedRunStep[];
  /** Browserbase session id to replay (present once the run used a browser). */
  browserbaseSessionId?: string;
  /** Live Browserbase session id while the run is executing. */
  liveBrowserbaseSessionId?: string;
  /** Final URL the browser navigated to, if a browser was used. */
  finalUrl?: string;
  /** Total run duration in milliseconds. */
  durationMs?: number;
  /** Step completion statistics. */
  completedCount: number;
  failedCount: number;
  totalCount: number;
}

/** The normalized statuses that mean "still producing steps". */
export function isNormalizedLive(status: NormalizedRunStatus): boolean {
  return status === "queued" || status === "running";
}

/** The normalized statuses that mean "no more steps will arrive". */
export function isNormalizedTerminal(status: NormalizedRunStatus): boolean {
  return (
    status === "succeeded" || status === "failed" || status === "canceled"
  );
}

/** Compute step stats + derived fields for a view model under construction. */
export function finalizeViewModel(
  vm: Omit<
    NormalizedRunViewModel,
    "isLive" | "isTerminal" | "completedCount" | "failedCount" | "totalCount"
  >,
): NormalizedRunViewModel {
  const completedCount = vm.steps.filter((s) => s.status === "done").length;
  const failedCount = vm.steps.filter((s) => s.status === "failed").length;
  return {
    ...vm,
    isLive: isNormalizedLive(vm.status),
    isTerminal: isNormalizedTerminal(vm.status),
    completedCount,
    failedCount,
    totalCount: vm.steps.length,
  };
}

// ---------------------------------------------------------------------------
// Legacy Trigger.dev adapter (M28 step 2).
// ---------------------------------------------------------------------------

/** One step as it appears in a Trigger.dev run's output/metadata. */
export interface LegacyRunStepLike {
  nodeId: string;
  type: NodeType;
  title: string;
  status: "pending" | "running" | "done" | "failed";
  durationMs?: number;
  output?: unknown;
  error?: string;
}

/** The subset of a Trigger.dev realtime run the mapper needs. Structural so
 *  the mapper is testable without the SDK's exact type. */
export interface LegacyRunLike {
  id: string;
  status: string; // QUEUED | EXECUTING | COMPLETED | FAILED | CANCELED | ...
  createdAt: Date;
  output?: {
    steps?: LegacyRunStepLike[];
    browserbaseSessionId?: string;
    finalUrl?: string;
    durationMs?: number;
  };
  metadata?: Record<string, unknown>;
}

/** Map a legacy Trigger.dev realtime run into the normalized model. */
export function triggerRunToViewModel(run: LegacyRunLike): NormalizedRunViewModel {
  const status = mapLegacyStatus(run.status);
  // Prefer final output steps; fall back to live metadata steps.
  const metadataSteps = run.metadata?.steps as LegacyRunStepLike[] | undefined;
  const steps: LegacyRunStepLike[] = run.output?.steps ?? metadataSteps ?? [];

  const metadataSessionId = run.metadata?.browserbaseSessionId as
    | string
    | undefined;
  const metadataFinalUrl = run.metadata?.finalUrl as string | undefined;
  const metadataDurationMs = run.metadata?.durationMs as number | undefined;

  const normalizedSteps: NormalizedRunStep[] = steps.map((s) => ({
    nodeId: s.nodeId,
    type: s.type,
    title: s.title,
    status: s.status,
    durationMs: s.durationMs,
    output: s.output,
    error: s.error,
  }));

  const durationMs =
    run.output?.durationMs ??
    metadataDurationMs ??
    (normalizedSteps.length > 0
      ? normalizedSteps.reduce((sum, s) => sum + (s.durationMs ?? 0), 0)
      : undefined);

  return finalizeViewModel({
    id: run.id,
    engine: "legacy",
    status,
    createdAt: run.createdAt,
    steps: normalizedSteps,
    browserbaseSessionId: run.output?.browserbaseSessionId,
    liveBrowserbaseSessionId:
      metadataSessionId ?? run.output?.browserbaseSessionId,
    finalUrl: run.output?.finalUrl ?? metadataFinalUrl,
    durationMs,
  });
}

/** Map a Trigger.dev run status string to the normalized status. */
export function mapLegacyStatus(status: string): NormalizedRunStatus {
  switch (status) {
    case "QUEUED":
    case "WAITING_FOR_DEPLOY":
    case "DELAYED":
      return "queued";
    case "EXECUTING":
    case "REATTEMPTING":
    case "FROZEN":
      return "running";
    case "COMPLETED":
      return "succeeded";
    case "FAILED":
    case "CRASHED":
    case "SYSTEM_FAILURE":
    case "TIMED_OUT":
      return "failed";
    case "CANCELED":
      return "canceled";
    default:
      return "unknown";
  }
}

// ---------------------------------------------------------------------------
// Evo gRPC run-detail adapter (Milestone 29).
// ---------------------------------------------------------------------------

/** One node as reported by the C++ scheduler's GetRun RPC (M29). Mirrors
 *  evo-engine-adapter's EvoNodeStatus structurally so this module stays free
 *  of gRPC imports. */
export interface EvoNodeStatusLike {
  nodeId: string;
  nodeType: string;
  /** NodeState enum name, e.g. "NODE_STATE_RUNNING". */
  state: string;
  attemptNumber: number;
  output: string; // opaque JSON owned by the node executor
  error: string;
  startedAtMs?: number;
  finishedAtMs?: number;
}

/** Full GetRun detail (run + per-node states) for the normalized model. */
export interface EvoRunDetailLike {
  runId: string;
  /** RunStatus enum name, e.g. "RUN_RUNNING". */
  status: string;
  outcome: string;
  createdAtMs?: number;
  finishedAtMs?: number;
  nodes: EvoNodeStatusLike[];
}

/** Map a C++ NodeState enum name to a normalized step status. */
export function mapEvoNodeState(state: string): NormalizedRunStep["status"] {
  switch (state) {
    case "NODE_STATE_SUCCEEDED":
      return "done";
    case "NODE_STATE_FAILED":
    case "NODE_STATE_DEAD_LETTER":
      return "failed";
    case "NODE_STATE_CANCELED":
      return "canceled";
    case "NODE_STATE_RUNNING":
      return "running";
    default:
      return "pending"; // BLOCKED / READY / UNSPECIFIED
  }
}

/** Map a C++ RunStatus enum name to a normalized run status. */
export function mapEvoRunStatus(status: string): NormalizedRunStatus {
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

/**
 * Map a C++ GetRun detail into the normalized view model (M29). This is the
 * engine-neutral path the UI uses for app-submitted Evo runs: the scheduler's
 * GetRun RPC is the authoritative source of per-node state for those runs.
 * Node output is parsed from the opaque JSON string; a parse failure keeps the
 * raw string so nothing is lost.
 */
export function evoRunDetailToViewModel(
  detail: EvoRunDetailLike,
): NormalizedRunViewModel {
  const steps: NormalizedRunStep[] = detail.nodes.map((n) => {
    let output: unknown;
    if (n.output) {
      try {
        output = JSON.parse(n.output);
      } catch {
        output = n.output;
      }
    }
    return {
      nodeId: n.nodeId,
      type: n.nodeType,
      title: n.nodeId,
      status: mapEvoNodeState(n.state),
      output,
      error: n.error || undefined,
      durationMs:
        n.startedAtMs !== undefined && n.finishedAtMs !== undefined
          ? n.finishedAtMs - n.startedAtMs
          : undefined,
    };
  });

  const durationMs =
    detail.createdAtMs !== undefined && detail.finishedAtMs !== undefined
      ? detail.finishedAtMs - detail.createdAtMs
      : undefined;

  return finalizeViewModel({
    id: detail.runId,
    engine: "evo",
    status: mapEvoRunStatus(detail.status),
    createdAt: detail.createdAtMs !== undefined ? new Date(detail.createdAtMs) : undefined,
    steps,
    durationMs,
  });
}

// ---------------------------------------------------------------------------
// Evo event adapter (M28 step 3 consumer side).
// ---------------------------------------------------------------------------

/** A normalized Evo run event as delivered by the SSE layer. Mirrors the C++
 *  RunEvent JSON (engine/core distributed_run_loop): run_id, node_id, kind,
 *  detail, wall_ms. The C++ loop puts the node type in `detail` on
 *  node_dispatched and the (truncated) output in `detail` on node_succeeded.
 *  The optional rich fields let a future durable-snapshot enrichment path
 *  override `detail` without changing the reducer. */
export interface EvoRunEvent {
  run_id: string;
  node_id: string; // empty for run-level events
  kind:
    | "run_started"
    | "node_dispatched"
    | "node_succeeded"
    | "node_failed"
    | "node_canceled"
    | "run_finished"
    | "browser_session_opened"
    | string;
  detail: string;
  wall_ms: number;
  /** Optional overrides from a durable-snapshot enrichment path. */
  node_type?: string;
  node_title?: string;
  output_json?: string;
  browserbase_session_id?: string;
  final_url?: string;
}

/**
 * Fold an ordered list of Evo run events into a normalized view model.
 * Idempotent per (kind, node_id): applying the same event twice does not
 * change the result, so duplicate deliveries are safe (M28 step 7). Events
 * MUST be in delivery order; the reducer is order-sensitive for step status
 * transitions but duplicate-safe.
 */
export function reduceEvoEvents(
  runId: string,
  events: EvoRunEvent[],
): NormalizedRunViewModel {
  let status: NormalizedRunStatus = "queued";
  let createdAt: Date | undefined;
  let finishedAtMs: number | undefined;
  let startedAtMs: number | undefined;
  let browserbaseSessionId: string | undefined;
  let finalUrl: string | undefined;
  const steps = new Map<
    string,
    NormalizedRunStep & { startedAtMs?: number; finishedAtMs?: number }
  >();

  const ensureStep = (nodeId: string): NormalizedRunStep & {
    startedAtMs?: number;
    finishedAtMs?: number;
  } => {
    let s = steps.get(nodeId);
    if (!s) {
      s = { nodeId, type: "", title: nodeId, status: "pending" };
      steps.set(nodeId, s);
    }
    return s;
  };

  for (const ev of events) {
    if (ev.run_id !== runId) continue; // never fold another run's events
    switch (ev.kind) {
      case "run_started":
        status = "running";
        startedAtMs = ev.wall_ms;
        createdAt = createdAt ?? new Date(ev.wall_ms);
        if (ev.browserbase_session_id) browserbaseSessionId = ev.browserbase_session_id;
        break;
      case "node_dispatched": {
        const s = ensureStep(ev.node_id);
        // The C++ loop puts the node type in `detail` on dispatch.
        const type = ev.node_type ?? ev.detail;
        if (type) s.type = type;
        if (ev.node_title) s.title = ev.node_title;
        if (s.status === "pending") s.status = "running";
        s.startedAtMs = ev.wall_ms;
        break;
      }
      case "node_succeeded": {
        const s = ensureStep(ev.node_id);
        s.status = "done";
        s.finishedAtMs = ev.wall_ms;
        // The C++ loop puts the (truncated) output in `detail` on success.
        const outputJson = ev.output_json ?? ev.detail;
        if (outputJson) {
          try {
            s.output = JSON.parse(outputJson);
          } catch {
            s.output = outputJson;
          }
        }
        if (s.startedAtMs !== undefined) {
          s.durationMs = ev.wall_ms - s.startedAtMs;
        }
        break;
      }
      case "node_failed": {
        const s = ensureStep(ev.node_id);
        if (ev.node_type) s.type = ev.node_type;
        s.status = "failed";
        s.error = ev.detail || undefined;
        s.finishedAtMs = ev.wall_ms;
        if (s.startedAtMs !== undefined) {
          s.durationMs = ev.wall_ms - s.startedAtMs;
        }
        break;
      }
      case "node_canceled": {
        const s = ensureStep(ev.node_id);
        if (s.status !== "done" && s.status !== "failed") {
          s.status = "canceled";
        }
        s.finishedAtMs = ev.wall_ms;
        break;
      }
      case "run_finished":
        finishedAtMs = ev.wall_ms;
        status =
          ev.detail === "succeeded"
            ? "succeeded"
            : ev.detail === "canceled" || ev.detail === "stopped" || ev.detail === "timeout"
              ? "canceled"
              : "failed";
        if (ev.browserbase_session_id) browserbaseSessionId = ev.browserbase_session_id;
        if (ev.final_url) finalUrl = ev.final_url;
        break;
      case "browser_session_opened":
        // The worker publishes the Browserbase session id as soon as it opens
        // (M29 live-view parity). The id rides in detail or the rich field.
        if (ev.browserbase_session_id) {
          browserbaseSessionId = ev.browserbase_session_id;
        } else if (ev.detail) {
          browserbaseSessionId = ev.detail;
        }
        break;
      default:
        // Unknown event kinds are ignored (forward compatibility).
        break;
    }
  }

  const stepList: NormalizedRunStep[] = [...steps.values()].map((s) => ({
    nodeId: s.nodeId,
    type: s.type,
    title: s.title,
    status: s.status,
    durationMs: s.durationMs,
    output: s.output,
    error: s.error,
  }));

  const durationMs =
    startedAtMs !== undefined && finishedAtMs !== undefined
      ? finishedAtMs - startedAtMs
      : undefined;

  return finalizeViewModel({
    id: runId,
    engine: "evo",
    status,
    createdAt,
    steps: stepList,
    browserbaseSessionId,
    liveBrowserbaseSessionId: browserbaseSessionId,
    finalUrl,
    durationMs,
  });
}
