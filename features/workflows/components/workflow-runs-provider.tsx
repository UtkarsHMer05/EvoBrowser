"use client";

import {
  createContext,
  useContext,
  useEffect,
  useMemo,
  useRef,
  useState,
} from "react";
import { useRealtimeRunsWithTag } from "@trigger.dev/react-hooks";

import { listEvoRunsAction } from "@/features/workflows/actions";
import type { NormalizedRunViewModel } from "@/features/workflows/lib/run-view-model";
import {
  consoleRunStatusLabel,
  mergeConsoleRuns,
  toConsoleRunFromEvo,
  toConsoleRunFromLegacy,
  type ConsoleRun,
  type ConsoleRunStatus,
  type LegacyConsoleRunLike,
} from "@/features/workflows/lib/run-console";
import type { RunStep, runWorkflowTask } from "@/features/workflows/tasks/run-workflow";

// Re-export the engine-neutral console types + helpers so existing consumers
// keep importing them from this provider (their import paths are unchanged).
export type { ConsoleRun, ConsoleRunStatus };
export { consoleRunStatusLabel };

interface WorkflowRunsContextValue {
  /** Every run from both engines, newest first. */
  consoleRuns: ConsoleRun[];
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

// One shared realtime subscription to every legacy (Trigger.dev) run tagged
// workflow:<id>, PLUS a single durable poll of the workflow's Evo runs (Phase 2,
// M29). Both are merged into one engine-neutral, newest-first list that every
// component on the canvas reads through the hooks below — so the polling and the
// socket each run exactly once per canvas, no matter how many panels mount.
export function WorkflowRunsProvider({
  workflowId,
  accessToken,
  children,
}: WorkflowRunsProviderProps) {
  const { runs, error } = useRealtimeRunsWithTag<typeof runWorkflowTask>(
    `workflow:${workflowId}`,
    { accessToken },
  );
  const evoRuns = useEvoConsoleRuns(workflowId);

  const consoleRuns = useMemo<ConsoleRun[]>(() => {
    const legacyRuns = runs.map((run) =>
      toConsoleRunFromLegacy(run as LegacyConsoleRunLike),
    );
    return mergeConsoleRuns(legacyRuns, evoRuns);
  }, [runs, evoRuns]);

  const value = useMemo<WorkflowRunsContextValue>(
    () => ({ consoleRuns, error }),
    [consoleRuns, error],
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

// ---------------------------------------------------------------------------
// Evo run polling (Milestone 29).
// ---------------------------------------------------------------------------

// How often to re-read the workflow's Evo runs from the durable Phase-2 store.
// The C++ engine persists every node status transition as it happens, so a short
// poll gives the console/canvas a live view of Evo runs.
const EVO_POLL_MS = 2000;

// The server action returns a serializable DTO (createdAt as an ISO string);
// revive it back into a NormalizedRunViewModel with a real Date.
type EvoRunDto = Omit<NormalizedRunViewModel, "createdAt"> & {
  createdAt?: string;
};

function reviveEvoRun(dto: EvoRunDto): NormalizedRunViewModel {
  return {
    ...dto,
    createdAt: dto.createdAt ? new Date(dto.createdAt) : undefined,
  };
}

// Poll the workflow's Evo runs from the durable store. Runs once per provider
// (not per consumer). Fail-open: a transient error keeps the last known list so
// the console never blanks.
function useEvoConsoleRuns(workflowId: string): ConsoleRun[] {
  const [vms, setVms] = useState<NormalizedRunViewModel[]>([]);
  const cancelledRef = useRef(false);

  useEffect(() => {
    cancelledRef.current = false;
    let timer: ReturnType<typeof setTimeout> | undefined;

    const tick = async () => {
      try {
        const dtos = await listEvoRunsAction(workflowId);
        if (!cancelledRef.current) {
          setVms(dtos.map(reviveEvoRun));
        }
      } catch {
        // Fail-open: keep the previous list.
      }
      if (!cancelledRef.current) {
        timer = setTimeout(tick, EVO_POLL_MS);
      }
    };

    void tick();

    return () => {
      cancelledRef.current = true;
      if (timer) clearTimeout(timer);
    };
  }, [workflowId]);

  return useMemo(() => vms.map(toConsoleRunFromEvo), [vms]);
}

// ---------------------------------------------------------------------------
// Merged, engine-neutral hooks.
// ---------------------------------------------------------------------------

// Every run from both engines, newest first. This is the full history a console
// panel below the canvas renders as a list of runs to drill into.
export function useConsoleRuns(): ConsoleRun[] {
  const { consoleRuns } = useWorkflowRuns();
  return consoleRuns;
}

interface LatestRunSteps {
  steps: RunStep[];
  // True while the latest run is queued or executing — i.e. still producing steps.
  isLive: boolean;
}

// The most recent run regardless of status or engine — used to notice when a run
// flips from live to finished so the console can surface its completion summary.
export function useLatestRun(): ConsoleRun | undefined {
  const runs = useConsoleRuns();
  return runs[0]; // useConsoleRuns sorts newest first
}

// The steps of the most recent run, plus whether it's still going.
export function useLatestRunSteps(): LatestRunSteps {
  const latest = useLatestRun();

  return useMemo<LatestRunSteps>(() => {
    if (!latest) return { steps: [], isLive: false };
    return { steps: latest.steps, isLive: latest.isLive };
  }, [latest]);
}

// The run currently in flight, if any — at most one is live at a time. A Stop
// button reads this to know whether there's a run to cancel and, if so, its id.
export function useLiveRun(): ConsoleRun | undefined {
  const runs = useConsoleRuns();
  return useMemo(() => runs.find((r) => r.isLive), [runs]);
}

// Hook that returns the live Browserbase session ID for the currently executing
// run (either engine). Returns undefined before any browser node initializes a
// session or when no run is active.
export function useLiveBrowserbaseSessionId(): string | undefined {
  const liveRun = useLiveRun();
  return liveRun?.liveBrowserbaseSessionId;
}
