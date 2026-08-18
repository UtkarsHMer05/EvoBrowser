"use client";

import { createContext, useContext, useMemo } from "react";
import { useRealtimeRunsWithTag } from "@trigger.dev/react-hooks";

import type {
  RunStep,
  runWorkflowTask,
} from "@/features/workflows/tasks/run-workflow";

type WorkflowRun = ReturnType<
  typeof useRealtimeRunsWithTag<typeof runWorkflowTask>
>["runs"][number];

interface WorkflowRunsContextValue {
  runs: WorkflowRun[];
  error?: Error;
}

const WorkflowRunsContext = createContext<WorkflowRunsContextValue | null>(
  null,
);

interface WorkflowRunsProviderProps {
  workflowId: string;
  // A Public Access Token scoped to read this workflow's runs, minted on the
  // server (auth.createPublicToken) and handed down as a prop.
  accessToken: string;
  children: React.ReactNode;
}

// One shared realtime subscription to every run tagged workflow:<id>. Any
// component on the canvas reads it through the hooks below instead of opening
// its own socket.
export function WorkflowRunsProvider({
  workflowId,
  accessToken,
  children,
}: WorkflowRunsProviderProps) {
  const { runs, error } = useRealtimeRunsWithTag<typeof runWorkflowTask>(
    `workflow:${workflowId}`,
    { accessToken },
  );

  const value = useMemo<WorkflowRunsContextValue>(
    () => ({ runs, error }),
    [runs, error],
  );

  return (
    <WorkflowRunsContext.Provider value={value}>
      {children}
    </WorkflowRunsContext.Provider>
  );
}

function useWorkflowRuns() {
  const ctx = useContext(WorkflowRunsContext);
  if (!ctx) {
    throw new Error(
      "useWorkflowRuns must be used within a WorkflowRunsProvider",
    );
  }
  return ctx;
}

// A run is still producing steps while it's queued or executing.
function isRunLive(run: WorkflowRun): boolean {
  return run.status === "QUEUED" || run.status === "EXECUTING";
}

// Terminal statuses — the run will produce no more steps. Anything else is
// still in flight (queued, executing, retrying, waiting to resume, …).
const TERMINAL_STATUSES = new Set(["COMPLETED", "FAILED", "CANCELED"]);

function isRunTerminal(run: WorkflowRun): boolean {
  return TERMINAL_STATUSES.has(run.status);
}

// The steps of a run, wherever they live. Prefer the run's final output steps
// (guaranteed once it succeeds) and fall back to the live metadata steps the
// task publishes while it runs — a failed or in-flight run only has the latter.
function stepsForRun(run: WorkflowRun): RunStep[] {
  const metadataSteps = run.metadata?.steps as RunStep[] | undefined;
  return run.output?.steps ?? metadataSteps ?? [];
}

interface LatestRunSteps {
  steps: RunStep[];
  // True while the latest run is queued or executing — i.e. still producing steps.
  isLive: boolean;
}

// The most recent run regardless of status — used to notice when a run flips
// from live to finished so the console can surface its completion summary.
export function useLatestRun(): WorkflowRun | undefined {
  const { runs } = useWorkflowRuns();

  return useMemo(
    () =>
      runs.reduce<WorkflowRun | undefined>((newest, run) => {
        if (!newest || run.createdAt > newest.createdAt) return run;
        return newest;
      }, undefined),
    [runs],
  );
}

// The steps of the most recent run, plus whether it's still going.
export function useLatestRunSteps(): LatestRunSteps {
  const latest = useLatestRun();

  return useMemo<LatestRunSteps>(() => {
    if (!latest) return { steps: [], isLive: false };

    return { steps: stepsForRun(latest), isLive: isRunLive(latest) };
  }, [latest]);
}

// The run currently in flight, if any — at most one is live at a time. A Stop
// button reads this to know whether there's a run to cancel and, if so, its id.
export function useLiveRun(): WorkflowRun | undefined {
  const { runs } = useWorkflowRuns();

  return useMemo(() => runs.find(isRunLive), [runs]);
}

// The Browserbase session id a finished run drove, read from its final output so
// a panel can fetch the replay.
function sessionIdForRun(run: WorkflowRun): string | undefined {
  return run.output?.browserbaseSessionId;
}

// Live Browserbase session id published in realtime metadata while the run is executing.
function liveSessionIdForRun(run: WorkflowRun): string | undefined {
  const metadataSessionId = run.metadata?.browserbaseSessionId as
    | string
    | undefined;
  return metadataSessionId ?? run.output?.browserbaseSessionId;
}

// Hook that returns the live Browserbase session ID for the currently executing run.
// Returns undefined before any browser node initializes Stagehand or when no run is active.
export function useLiveBrowserbaseSessionId(): string | undefined {
  const liveRun = useLiveRun();

  return useMemo(() => {
    if (!liveRun) return undefined;
    return liveSessionIdForRun(liveRun);
  }, [liveRun]);
}

// One run flattened for the console: its identity and status, whether it's still
// live, and its steps with everything each one produced.
export interface ConsoleRun {
  id: string;
  status: WorkflowRun["status"];
  createdAt: Date;
  isLive: boolean;
  // True once the run has reached a terminal status (completed/failed/canceled)
  // and will produce no more steps.
  isTerminal: boolean;
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

// Every run, newest first, with its steps resolved — the full history a console
// panel below the canvas renders as a list of runs to drill into.
export function useConsoleRuns(): ConsoleRun[] {
  const { runs } = useWorkflowRuns();

  return useMemo<ConsoleRun[]>(
    () =>
      [...runs]
        .sort((a, b) => b.createdAt.getTime() - a.createdAt.getTime())
        .map((run) => {
          const steps = stepsForRun(run);
          const completedCount = steps.filter((s) => s.status === "done").length;
          const failedCount = steps.filter((s) => s.status === "failed").length;
          const totalCount = steps.length;

          // Measured run duration: prefer task output duration, fallback to metadata or sum of step durations
          const durationMs =
            (run.output?.durationMs as number | undefined) ??
            (run.metadata?.durationMs as number | undefined) ??
            (steps.length > 0
              ? steps.reduce((sum, s) => sum + (s.durationMs ?? 0), 0)
              : undefined);

          const finalUrl =
            (run.output?.finalUrl as string | undefined) ??
            (run.metadata?.finalUrl as string | undefined);

          return {
            id: run.id,
            status: run.status,
            createdAt: run.createdAt,
            isLive: isRunLive(run),
            isTerminal: isRunTerminal(run),
            steps,
            browserbaseSessionId: sessionIdForRun(run),
            liveBrowserbaseSessionId: liveSessionIdForRun(run),
            finalUrl,
            durationMs,
            completedCount,
            failedCount,
            totalCount,
          };
        }),
    [runs],
  );
}
