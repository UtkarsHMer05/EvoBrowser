"use client";

import { useTransition } from "react";
import prettyMilliseconds from "pretty-ms";
import {
  CheckCircle2,
  AlertCircle,
  StopCircle,
  ExternalLink,
  MonitorPlay,
  Clock,
  Layers,
  Play,
  FileText,
} from "lucide-react";
import { toast } from "sonner";

import { Button } from "@/components/ui/button";
import { runWorkflowAction } from "@/features/workflows/actions";
import { NodeIcon } from "@/features/workflows/components/node-icon";
import { SessionReplay } from "@/features/workflows/components/session-replay";
import { StepOutputView } from "@/features/workflows/components/step-output-view";
import {
  useConsoleRuns,
  type ConsoleRun,
} from "@/features/workflows/components/workflow-runs-provider";
import type { ConsoleSelection } from "@/features/workflows/components/logs-panel";
import { validateGraph } from "@/features/workflows/lib/validate-graph";
import type { StepNodeType } from "@/features/workflows/nodes/node-registry";
import { useReactFlow } from "@xyflow/react";

// A short, centered note for when there's nothing concrete to show.
function Note({ children }: { children: React.ReactNode }) {
  return (
    <div className="flex size-full items-center justify-center p-3 text-center text-xs text-muted-foreground">
      {children}
    </div>
  );
}

function RunSummary({
  run,
  workflowId,
  onSelect,
  onShowResults,
}: {
  run: ConsoleRun;
  workflowId: string;
  onSelect?: (selection: ConsoleSelection) => void;
  onShowResults?: (runId: string) => void;
}) {
  const isCompleted = run.status === "COMPLETED" && !run.isLive;
  const isFailed = run.status === "FAILED";
  const isCanceled = run.status === "CANCELED";

  // Run Again re-runs the graph as it exists on the canvas right now — any
  // edits made since the summarized run are what gets executed. Same
  // validate-then-trigger path as the sidebar's Run button; nothing runs
  // automatically.
  const { getNodes, getEdges } = useReactFlow<StepNodeType>();
  const [isRerunning, startRerun] = useTransition();

  const handleRunAgain = () => {
    const graph = { nodes: getNodes(), edges: getEdges() };
    const problems = validateGraph(graph);
    if (problems.length > 0) {
      toast.error(problems[0]);
      return;
    }

    startRerun(async () => {
      try {
        await runWorkflowAction({ id: workflowId, graph });
      } catch (error) {
        const message =
          error instanceof Error ? error.message : "Couldn't start the run.";
        toast.error(message);
      }
    });
  };

  return (
    <div className="flex size-full flex-col overflow-y-auto">
      {/* Header */}
      <div className="flex items-center justify-between border-b border-border px-3 py-2.5 bg-card/50">
        <div className="flex items-center gap-2">
          {isCompleted && (
            <CheckCircle2 className="size-4 text-emerald-500 shrink-0" />
          )}
          {isFailed && (
            <AlertCircle className="size-4 text-destructive shrink-0" />
          )}
          {isCanceled && (
            <StopCircle className="size-4 text-amber-500 shrink-0" />
          )}
          {run.isLive && (
            <Clock className="size-4 text-blue-500 animate-spin shrink-0" />
          )}
          <span className="text-xs font-semibold">
            {run.isLive
              ? "Workflow Executing"
              : isCompleted
                ? "Workflow Completed"
                : isFailed
                  ? "Workflow Failed"
                  : isCanceled
                    ? "Workflow Stopped"
                    : `Workflow ${run.status.toLowerCase()}`}
          </span>
        </div>
        <span className="text-[10px] text-muted-foreground">
          {run.createdAt.toLocaleTimeString()}
        </span>
      </div>

      {/* Metrics & Details */}
      <div className="p-3 space-y-3">
        <div className="grid grid-cols-2 gap-2">
          {/* Duration */}
          <div className="rounded-md border border-border/60 p-2 bg-muted/20">
            <div className="flex items-center gap-1.5 text-[10px] text-muted-foreground mb-0.5">
              <Clock className="size-3" />
              <span>Duration</span>
            </div>
            <div className="text-xs font-semibold tabular-nums">
              {run.durationMs != null
                ? prettyMilliseconds(run.durationMs)
                : run.isLive
                  ? "In progress…"
                  : "—"}
            </div>
          </div>

          {/* Node Count */}
          <div className="rounded-md border border-border/60 p-2 bg-muted/20">
            <div className="flex items-center gap-1.5 text-[10px] text-muted-foreground mb-0.5">
              <Layers className="size-3" />
              <span>Nodes</span>
            </div>
            <div className="text-xs font-semibold tabular-nums">
              {run.completedCount}/{run.totalCount} completed
              {run.failedCount > 0 && (
                <span className="text-destructive font-normal ml-1">
                  ({run.failedCount} failed)
                </span>
              )}
            </div>
          </div>
        </div>

        {/* Final URL if reached */}
        {run.finalUrl && (
          <div className="rounded-md border border-border/60 p-2 bg-muted/20">
            <div className="text-[10px] text-muted-foreground mb-1">
              Final Browser URL
            </div>
            <a
              href={run.finalUrl}
              target="_blank"
              rel="noreferrer"
              className="flex items-center gap-1.5 text-xs text-blue-500 dark:text-blue-400 hover:underline font-mono truncate"
            >
              <span className="truncate">{run.finalUrl}</span>
              <ExternalLink className="size-3 shrink-0" />
            </a>
          </div>
        )}

        {/* Replay + rerun actions */}
        {!run.isLive && (
          <div className="flex flex-col gap-1.5">
            {onShowResults && (
              <Button
                size="sm"
                variant="outline"
                className="w-full justify-center text-xs h-8"
                onClick={() => onShowResults(run.id)}
              >
                <FileText className="size-3.5 mr-1.5" />
                View Full Results
              </Button>
            )}
            {run.browserbaseSessionId && onSelect && (
              <Button
                size="sm"
                variant="outline"
                className="w-full justify-center text-xs h-8"
                onClick={() =>
                  onSelect({ kind: "replay", runId: run.id })
                }
              >
                <MonitorPlay className="size-3.5 mr-1.5" />
                Watch Session Replay
              </Button>
            )}
            <Button
              size="sm"
              variant="secondary"
              className="w-full justify-center text-xs h-8"
              disabled={isRerunning}
              onClick={handleRunAgain}
            >
              <Play className="size-3.5 mr-1.5" />
              Run Again
            </Button>
          </div>
        )}

        {/* Node outputs & step breakdown */}
        <div className="space-y-1.5 pt-1">
          <div className="text-[11px] font-semibold text-muted-foreground">
            Step Execution Details
          </div>
          <div className="divide-y divide-border/40 rounded-md border border-border/60">
            {run.steps.map((s) => (
              <button
                key={s.nodeId}
                type="button"
                onClick={() =>
                  onSelect?.({
                    kind: "step",
                    runId: run.id,
                    nodeId: s.nodeId,
                  })
                }
                className="flex w-full items-center justify-between p-2 text-left text-xs hover:bg-accent/50 transition-colors"
              >
                <div className="flex items-center gap-2 min-w-0">
                  <NodeIcon type={s.type} running={s.status === "running"} />
                  <span className="truncate font-medium">{s.title}</span>
                </div>
                <div className="flex items-center gap-2 shrink-0 text-muted-foreground tabular-nums text-[10px]">
                  {s.durationMs != null && (
                    <span>{prettyMilliseconds(s.durationMs)}</span>
                  )}
                  <span
                    className={
                      s.status === "done"
                        ? "text-emerald-500 font-medium"
                        : s.status === "failed"
                          ? "text-destructive font-medium"
                          : "text-muted-foreground"
                    }
                  >
                    {s.status}
                  </span>
                </div>
              </button>
            ))}
          </div>
        </div>
      </div>
    </div>
  );
}

// The output pane for whatever the logs have selected: a step's output, a
// whole run's session replay, or run completion summary.
export function InspectorPanel({
  selection,
  workflowId,
  onSelect,
  onShowResults,
}: {
  selection: ConsoleSelection;
  workflowId: string;
  onSelect?: (selection: ConsoleSelection) => void;
  onShowResults?: (runId: string) => void;
}) {
  const runs = useConsoleRuns();
  const run = runs.find((r) => r.id === selection.runId);

  // A run's completion overview summary
  if (selection.kind === "summary") {
    if (!run) return <Note>This run is no longer available.</Note>;
    return (
      <RunSummary
        run={run}
        workflowId={workflowId}
        onSelect={onSelect}
        onShowResults={onShowResults}
      />
    );
  }

  // A run's replay stands for the whole session — play it instead of any step.
  if (selection.kind === "replay") {
    if (!run?.browserbaseSessionId) {
      return <Note>This recording is no longer available.</Note>;
    }
    return <SessionReplay sessionId={run.browserbaseSessionId} />;
  }

  const step = run?.steps.find((s) => s.nodeId === selection.nodeId);

  // The selected step can vanish if its run drops out of the realtime window.
  if (!step) return <Note>This step is no longer available.</Note>;

  return (
    <div className="flex size-full flex-col">
      <div className="flex items-center gap-2 border-b border-border px-3 py-2">
        <NodeIcon type={step.type} />
        <span className="truncate text-xs font-semibold">{step.title}</span>
      </div>
      {step.status === "pending" ? (
        <Note>This step hasn&apos;t run yet.</Note>
      ) : step.status === "running" ? (
        <Note>Waiting for this step to finish…</Note>
      ) : (
        <div className="min-h-0 flex-1 overflow-y-auto p-3">
          <StepOutputView step={step} />
        </div>
      )}
    </div>
  );
}
