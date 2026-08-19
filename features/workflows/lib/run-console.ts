// Phase 2 — engine-neutral console run mapping (Milestone 29).
//
// Pure functions (no React, no I/O, no SDK) that flatten a run from EITHER
// engine into the one shape the console/canvas/results UI renders: ConsoleRun.
// Keeping this logic in a plain module (not the React provider) means the
// legacy-vs-Evo parity regression suite can exercise the EXACT mapping the UI
// uses, and the provider stays a thin React wrapper.
//
// The parity guarantee (M29 objective): for equivalent lifecycle states, a
// legacy Trigger.dev run and an Evo run produce ConsoleRuns with identical
// derived semantics (isLive/isTerminal/isCompleted/isFailed/isCanceled), the
// same human-readable status label, and the same step-status vocabulary — so
// the UI renders both engines identically.

import type {
  NormalizedRunStatus,
  NormalizedRunStep,
  NormalizedRunViewModel,
} from "./run-view-model";
import type { NodeType } from "@/features/workflows/nodes/node-registry";
import type { RunStep } from "@/features/workflows/tasks/run-workflow";

// The coarse status a console run reports. Legacy runs keep their Trigger.dev
// status strings; Evo runs carry their normalized status. Consumers should rely
// on the derived booleans rather than comparing raw strings, so both engines
// render identically.
export type ConsoleRunStatus = string | NormalizedRunStatus;

// One run flattened for the console: its identity and status, whether it's still
// live, and its steps with everything each one produced. Engine-neutral (M29).
export interface ConsoleRun {
  id: string;
  /** Which engine produced this run. */
  engine: "legacy" | "evo";
  status: ConsoleRunStatus;
  createdAt: Date;
  isLive: boolean;
  // True once the run has reached a terminal status (completed/failed/canceled)
  // and will produce no more steps.
  isTerminal: boolean;
  // Engine-neutral terminal flags — prefer these over raw status comparisons.
  isCompleted: boolean;
  isFailed: boolean;
  isCanceled: boolean;
  steps: RunStep[];
  // The Browserbase session id to replay, present only once the run has finished.
  browserbaseSessionId?: string;
  // The live Browserbase session id available while the run is executing.
  liveBrowserbaseSessionId?: string;
  // The final URL the browser navigated to, if a browser was used.
  finalUrl?: string;
  // Total duration of the workflow execution in milliseconds.
  durationMs?: number;
  // Step completion statistics.
  completedCount: number;
  failedCount: number;
  totalCount: number;
}

// ---------------------------------------------------------------------------
// Legacy Trigger.dev adapter.
// ---------------------------------------------------------------------------

// The subset of a Trigger.dev realtime run the mapper needs. Structural so the
// mapper is testable without the SDK's exact type (and without importing it).
export interface LegacyConsoleRunLike {
  id: string;
  status: string; // QUEUED | EXECUTING | COMPLETED | FAILED | CANCELED | ...
  createdAt: Date;
  output?: {
    steps?: RunStep[];
    browserbaseSessionId?: string;
    finalUrl?: string;
    durationMs?: number;
  };
  metadata?: Record<string, unknown>;
}

// A legacy run is still producing steps while it's queued or executing.
function isLegacyRunLive(run: LegacyConsoleRunLike): boolean {
  return run.status === "QUEUED" || run.status === "EXECUTING";
}

// Terminal statuses — the run will produce no more steps.
const LEGACY_TERMINAL_STATUSES = new Set(["COMPLETED", "FAILED", "CANCELED"]);

function isLegacyRunTerminal(run: LegacyConsoleRunLike): boolean {
  return LEGACY_TERMINAL_STATUSES.has(run.status);
}

// The steps of a legacy run, wherever they live. Prefer the run's final output
// steps (guaranteed once it succeeds) and fall back to the live metadata steps
// the task publishes while it runs — a failed or in-flight run only has the
// latter.
function legacyStepsForRun(run: LegacyConsoleRunLike): RunStep[] {
  const metadataSteps = run.metadata?.steps as RunStep[] | undefined;
  return run.output?.steps ?? metadataSteps ?? [];
}

function legacySessionIdForRun(run: LegacyConsoleRunLike): string | undefined {
  return run.output?.browserbaseSessionId;
}

function legacyLiveSessionIdForRun(
  run: LegacyConsoleRunLike,
): string | undefined {
  const metadataSessionId = run.metadata?.browserbaseSessionId as
    | string
    | undefined;
  return metadataSessionId ?? run.output?.browserbaseSessionId;
}

// Flatten one Trigger.dev realtime run into a ConsoleRun. This is the EXACT
// Phase-1 mapping (preserved verbatim from the pre-M29 provider) so legacy
// behavior is unchanged.
export function toConsoleRunFromLegacy(
  run: LegacyConsoleRunLike,
): ConsoleRun {
  const steps = legacyStepsForRun(run);
  const completedCount = steps.filter((s) => s.status === "done").length;
  const failedCount = steps.filter((s) => s.status === "failed").length;
  const totalCount = steps.length;

  // Measured run duration: prefer task output duration, fallback to metadata or
  // sum of step durations.
  const durationMs =
    (run.output?.durationMs as number | undefined) ??
    (run.metadata?.durationMs as number | undefined) ??
    (steps.length > 0
      ? steps.reduce((sum, s) => sum + (s.durationMs ?? 0), 0)
      : undefined);

  const finalUrl =
    (run.output?.finalUrl as string | undefined) ??
    (run.metadata?.finalUrl as string | undefined);

  const isLive = isLegacyRunLive(run);
  return {
    id: run.id,
    engine: "legacy",
    status: run.status,
    createdAt: run.createdAt,
    isLive,
    isTerminal: isLegacyRunTerminal(run),
    // Legacy terminal flags mirror the exact Phase-1 string comparisons the
    // inspector/results dialog used before M29.
    isCompleted: run.status === "COMPLETED" && !isLive,
    isFailed: run.status === "FAILED",
    isCanceled: run.status === "CANCELED",
    steps,
    browserbaseSessionId: legacySessionIdForRun(run),
    liveBrowserbaseSessionId: legacyLiveSessionIdForRun(run),
    finalUrl,
    durationMs,
    completedCount,
    failedCount,
    totalCount,
  };
}

// ---------------------------------------------------------------------------
// Evo adapter.
// ---------------------------------------------------------------------------

// Map one normalized Evo step to the console's RunStep shape. The Evo node type
// is a plain string; it may not be a registry key, so keep it as-is and let
// NodeIcon fall back gracefully for unknown types.
function evoStepToRunStep(step: NormalizedRunStep): RunStep {
  return {
    nodeId: step.nodeId,
    type: step.type as NodeType,
    title: step.title,
    status: step.status,
    durationMs: step.durationMs,
    output: step.output,
    error: step.error,
  };
}

// Flatten one normalized Evo view model into a ConsoleRun (Milestone 29). The
// derived booleans make Evo runs render identically to legacy runs.
export function toConsoleRunFromEvo(vm: NormalizedRunViewModel): ConsoleRun {
  const steps = vm.steps.map(evoStepToRunStep);
  return {
    id: vm.id,
    engine: "evo",
    status: vm.status,
    createdAt: vm.createdAt ?? new Date(0),
    isLive: vm.isLive,
    isTerminal: vm.isTerminal,
    isCompleted: vm.status === "succeeded",
    isFailed: vm.status === "failed",
    isCanceled: vm.status === "canceled",
    steps,
    browserbaseSessionId: vm.browserbaseSessionId,
    liveBrowserbaseSessionId: vm.liveBrowserbaseSessionId,
    finalUrl: vm.finalUrl,
    durationMs: vm.durationMs,
    completedCount: vm.completedCount,
    failedCount: vm.failedCount,
    totalCount: vm.totalCount,
  };
}

// ---------------------------------------------------------------------------
// Shared, engine-neutral helpers.
// ---------------------------------------------------------------------------

// The human-readable status line for a console run, engine-neutral. Matches the
// exact labels the logs panel used for legacy runs before M29.
export function consoleRunStatusLabel(run: ConsoleRun): string {
  if (run.isLive) return "Running…";
  if (run.isCompleted) return "Completed";
  if (run.isFailed) return "Failed";
  if (run.isCanceled) return "Stopped";
  return String(run.status).toLowerCase();
}

// Merge runs from both engines into one newest-first, de-duplicated list. The
// two sources never overlap in practice, but this guards against a run appearing
// in both during an engine transition.
export function mergeConsoleRuns(
  legacyRuns: ConsoleRun[],
  evoRuns: ConsoleRun[],
): ConsoleRun[] {
  const merged = [...legacyRuns, ...evoRuns];
  merged.sort((a, b) => b.createdAt.getTime() - a.createdAt.getTime());
  const seen = new Set<string>();
  return merged.filter((r) => {
    if (seen.has(r.id)) return false;
    seen.add(r.id);
    return true;
  });
}
