import { memo } from "react";
import { Handle, Position, type NodeProps } from "@xyflow/react";
import { Check, AlertCircle, Ban } from "lucide-react";

import {
  nodeRegistry,
  type StepNodeType,
} from "@/features/workflows/nodes/node-registry";
import { useLatestRunSteps } from "@/features/workflows/components/workflow-runs-provider";
import { Spinner } from "@/components/ui/spinner";
import { cn } from "@/lib/utils";

function StepNodeComponent({ id, data, selected }: NodeProps<StepNodeType>) {
  const { type, kind, title, values } = data;
  const def = nodeRegistry[type];
  const Icon = def.icon;
  const fields = def.fields.filter((field) => values[field.key]);

  // Reflect this node's state in the latest run. A node is only "running" while
  // the run is actually live — once it ends, a node left marked running stops
  // spinning rather than hanging forever.
  const { steps, isLive } = useLatestRunSteps();
  const step = steps.find((s) => s.nodeId === id);
  const status = step?.status;
  const isRunning = status === "running" && isLive;
  const isDone = status === "done";
  const isFailed = status === "failed";
  // Evo (M29) marks nodes that never ran when a run is stopped as "canceled".
  const isCanceled = status === "canceled";
  const isPending = status === "pending" && isLive;

  // A trigger starts the flow and takes no input, so it has no target handle.
  const hasTarget = kind !== "trigger";

  return (
    <div
      className={cn(
        "min-w-50 max-w-80 rounded-(--radius) border-2 border-border bg-card text-card-foreground transition-all duration-200",
        isRunning &&
          "border-blue-500 shadow-md shadow-blue-500/20 ring-2 ring-blue-500/30",
        isDone && "border-emerald-500/50",
        isFailed &&
          "border-destructive shadow-md shadow-destructive/20 ring-2 ring-destructive/30",
        isCanceled && "border-amber-500/50 opacity-70",
        isPending && "opacity-60",
        selected && "ring-2 ring-ring ring-offset-2 ring-offset-background",
      )}
    >
      {hasTarget && (
        <Handle
          type="target"
          position={Position.Left}
          style={{ transform: "translate(-100%, -50%)" }}
          className="h-3.5! w-1.5! min-w-0! rounded-l-xs! rounded-r-none! border-0! bg-border!"
        />
      )}

      <div className="flex items-center justify-between gap-2.5 px-3 py-2.5">
        <div className="flex items-center gap-2.5 min-w-0">
          <div
            className={cn(
              "flex size-7 shrink-0 items-center justify-center rounded-md transition-colors",
              def.accent,
              isDone &&
                "bg-emerald-500/15 text-emerald-600 dark:text-emerald-400",
            )}
          >
            {isRunning ? (
              <Spinner className="size-4 text-blue-500" />
            ) : isDone ? (
              <Check className="size-4 text-emerald-500" />
            ) : (
              <Icon className="size-4" />
            )}
          </div>
          <span className="text-sm font-semibold truncate">{title}</span>
        </div>

        {isRunning && (
          <span className="relative flex size-2 shrink-0">
            <span className="absolute inline-flex size-full animate-ping rounded-full bg-blue-400 opacity-75" />
            <span className="relative inline-flex size-2 rounded-full bg-blue-500" />
          </span>
        )}
        {isDone && (
          <Check className="size-3.5 shrink-0 text-emerald-500" />
        )}
        {isFailed && (
          <AlertCircle className="size-3.5 shrink-0 text-destructive" />
        )}
        {isCanceled && (
          <Ban className="size-3.5 shrink-0 text-amber-500" />
        )}
      </div>

      {fields.length > 0 && (
        <>
          <div className="border-t border-border" />
          <div className="flex flex-col gap-1.5 px-3 py-2.5">
            {fields.map((field) => (
              <div
                key={field.key}
                className="flex items-center justify-between gap-4 text-xs"
              >
                <span className="shrink-0 text-muted-foreground">
                  {field.label}
                </span>
                <span className="truncate font-medium">
                  {values[field.key]}
                </span>
              </div>
            ))}
          </div>
        </>
      )}

      <Handle
        type="source"
        position={Position.Right}
        style={{ transform: "translate(100%, -50%)" }}
        className="h-3.5! w-1.5! min-w-0! rounded-l-none! rounded-r-xs! border-0! bg-border!"
      />
    </div>
  );
}

export const StepNode = memo(StepNodeComponent);

