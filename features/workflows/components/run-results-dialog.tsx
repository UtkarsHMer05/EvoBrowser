"use client";

import { useTransition } from "react";
import prettyMilliseconds from "pretty-ms";
import {
  AlertCircle,
  Camera,
  CheckCircle2,
  Clock,
  ExternalLink,
  ImageOff,
  Layers,
  MonitorPlay,
  Play,
  StopCircle,
} from "lucide-react";
import { toast } from "sonner";

import { Button } from "@/components/ui/button";
import {
  Dialog,
  DialogContent,
  DialogHeader,
  DialogTitle,
} from "@/components/ui/dialog";
import { NodeIcon } from "@/features/workflows/components/node-icon";
import { StepOutputView } from "@/features/workflows/components/step-output-view";
import { runWorkflowAction } from "@/features/workflows/actions";
import type { ConsoleSelection } from "@/features/workflows/components/logs-panel";
import type { ConsoleRun } from "@/features/workflows/components/workflow-runs-provider";
import { validateGraph } from "@/features/workflows/lib/validate-graph";
import type { StepNodeType } from "@/features/workflows/nodes/node-registry";
import { useReactFlow } from "@xyflow/react";

// A full-screen readable summary of a finished run: what each step produced,
// the final screenshot of the browser, and quick actions (replay, run again).
// Auto-opens when a run completes, fails, or is stopped; closing it returns the
// user to the fully editable canvas. Nothing here runs automatically.
export function RunResultsDialog({
  run,
  workflowId,
  open,
  onOpenChange,
  onSelect,
}: {
  run: ConsoleRun | undefined;
  workflowId: string;
  open: boolean;
  onOpenChange: (open: boolean) => void;
  onSelect?: (selection: ConsoleSelection) => void;
}) {
  const { getNodes, getEdges } = useReactFlow<StepNodeType>();
  const [isRerunning, startRerun] = useTransition();

  if (!run) return null;

  const isCompleted = run.isCompleted;
  const isFailed = run.isFailed;
  const isCanceled = run.isCanceled;

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
        onOpenChange(false);
      } catch (error) {
        const message =
          error instanceof Error ? error.message : "Couldn't start the run.";
        toast.error(message);
      }
    });
  };

  const handleWatchReplay = () => {
    onOpenChange(false);
    onSelect?.({ kind: "replay", runId: run.id });
  };

  return (
    <Dialog open={open} onOpenChange={onOpenChange}>
      <DialogContent className="flex max-h-[85vh] max-w-2xl flex-col gap-0 overflow-hidden p-0">
        <DialogHeader className="shrink-0 border-b border-border px-5 py-4">
          <DialogTitle className="flex items-center gap-2 text-sm">
            {isCompleted && (
              <CheckCircle2 className="size-4 text-emerald-500" />
            )}
            {isFailed && <AlertCircle className="size-4 text-destructive" />}
            {isCanceled && <StopCircle className="size-4 text-amber-500" />}
            {isCompleted
              ? "Workflow completed"
              : isFailed
                ? "Workflow failed"
                : isCanceled
                  ? "Workflow stopped"
                  : "Workflow results"}
            <span className="ml-auto text-[10px] font-normal text-muted-foreground">
              {run.createdAt.toLocaleTimeString()}
            </span>
          </DialogTitle>
        </DialogHeader>

        <div className="min-h-0 flex-1 space-y-4 overflow-y-auto px-5 py-4">
          {/* Metrics */}
          <div className="grid grid-cols-2 gap-2">
            <div className="rounded-md border border-border/60 bg-muted/20 p-2.5">
              <div className="mb-0.5 flex items-center gap-1.5 text-[10px] text-muted-foreground">
                <Clock className="size-3" />
                Duration
              </div>
              <div className="text-sm font-semibold tabular-nums">
                {run.durationMs != null
                  ? prettyMilliseconds(run.durationMs)
                  : "—"}
              </div>
            </div>
            <div className="rounded-md border border-border/60 bg-muted/20 p-2.5">
              <div className="mb-0.5 flex items-center gap-1.5 text-[10px] text-muted-foreground">
                <Layers className="size-3" />
                Steps
              </div>
              <div className="text-sm font-semibold tabular-nums">
                {run.completedCount}/{run.totalCount} completed
                {run.failedCount > 0 && (
                  <span className="ml-1 font-normal text-destructive">
                    ({run.failedCount} failed)
                  </span>
                )}
              </div>
            </div>
          </div>

          {/* Final screenshot */}
          <div className="space-y-1.5">
            <div className="flex items-center gap-1.5 text-[11px] font-semibold text-muted-foreground">
              <Camera className="size-3.5" />
              Final browser screenshot
            </div>
            {run.browserbaseSessionId ? (
              // A dynamic per-run screenshot served from our own API route. We
              // need a plain <img> (not next/image) for the onError fallback
              // that swaps in a placeholder when no screenshot exists.
              // eslint-disable-next-line @next/next/no-img-element
              <img
                src={`/api/runs/${run.id}/screenshot`}
                alt="Final state of the automated browser"
                className="w-full rounded-md border border-border/60 object-contain"
                onError={(e) => {
                  // No screenshot (e.g. email-only run) — swap in a placeholder.
                  const target = e.currentTarget;
                  const placeholder = target.nextElementSibling;
                  target.style.display = "none";
                  if (placeholder instanceof HTMLElement) {
                    placeholder.style.display = "flex";
                  }
                }}
              />
            ) : null}
            <div
              className="hidden w-full flex-col items-center justify-center gap-1.5 rounded-md border border-dashed border-border/60 bg-muted/20 py-8 text-center"
              style={run.browserbaseSessionId ? undefined : { display: "flex" }}
            >
              <ImageOff className="size-6 text-muted-foreground/40" />
              <p className="text-[11px] text-muted-foreground">
                No browser screenshot for this run.
              </p>
            </div>
          </div>

          {/* Final URL */}
          {run.finalUrl && (
            <div className="space-y-1.5">
              <div className="text-[11px] font-semibold text-muted-foreground">
                Final URL
              </div>
              <a
                href={run.finalUrl}
                target="_blank"
                rel="noreferrer"
                className="flex items-center gap-1.5 text-xs text-blue-500 hover:underline dark:text-blue-400"
              >
                <span className="truncate font-mono">{run.finalUrl}</span>
                <ExternalLink className="size-3 shrink-0" />
              </a>
            </div>
          )}

          {/* Per-step readable outputs */}
          <div className="space-y-1.5">
            <div className="text-[11px] font-semibold text-muted-foreground">
              What each step did
            </div>
            <div className="divide-y divide-border/40 rounded-md border border-border/60">
              {run.steps.map((step) => (
                <div key={step.nodeId} className="p-3">
                  <div className="mb-2 flex items-center gap-2">
                    <NodeIcon
                      type={step.type}
                      running={step.status === "running"}
                    />
                    <span className="truncate text-xs font-medium">
                      {step.title}
                    </span>
                    <span
                      className={
                        "ml-auto shrink-0 text-[10px] font-medium " +
                        (step.status === "done"
                          ? "text-emerald-500"
                          : step.status === "failed"
                            ? "text-destructive"
                            : "text-muted-foreground")
                      }
                    >
                      {step.status}
                      {step.durationMs != null &&
                        ` · ${prettyMilliseconds(step.durationMs)}`}
                    </span>
                  </div>
                  {step.status === "done" || step.status === "failed" ? (
                    <StepOutputView step={step} />
                  ) : (
                    <p className="text-xs text-muted-foreground">
                      {step.status === "pending"
                        ? "Didn't run."
                        : step.status === "canceled"
                          ? "Canceled before it ran."
                          : "Still running…"}
                    </p>
                  )}
                </div>
              ))}
            </div>
          </div>
        </div>

        {/* Footer actions */}
        <div className="flex shrink-0 items-center justify-end gap-2 border-t border-border px-5 py-3">
          {run.browserbaseSessionId && (
            <Button
              size="sm"
              variant="outline"
              className="text-xs"
              onClick={handleWatchReplay}
            >
              <MonitorPlay className="mr-1.5 size-3.5" />
              Watch Replay
            </Button>
          )}
          <Button
            size="sm"
            variant="secondary"
            className="text-xs"
            disabled={isRerunning}
            onClick={handleRunAgain}
          >
            <Play className="mr-1.5 size-3.5" />
            Run Again
          </Button>
        </div>
      </DialogContent>
    </Dialog>
  );
}
