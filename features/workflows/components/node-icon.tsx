import { CircleHelp } from "lucide-react";

import { Spinner } from "@/components/ui/spinner";
import { cn } from "@/lib/utils";

import {
  nodeRegistry,
  type NodeType,
} from "@/features/workflows/nodes/node-registry";

// The accent-colored icon chip, mirroring the node on the canvas. Pass `running`
// to swap the node's icon for a spinner inside the same colored chip.
//
// `type` is typed as NodeType, but engine-neutral run steps (Phase 2, M29) carry
// the node type as a plain string that may not be in the registry (e.g. a future
// or synthetic node). Fall back to a neutral chip instead of crashing on an
// unknown key.
export function NodeIcon({
  type,
  running,
  className,
}: {
  type: NodeType | string;
  running?: boolean;
  className?: string;
}) {
  const def = nodeRegistry[type as NodeType];
  if (!def) {
    return (
      <span
        className={cn(
          "flex size-6 shrink-0 items-center justify-center rounded-md bg-muted text-muted-foreground",
          className,
        )}
      >
        {running ? (
          <Spinner className="size-3.5" />
        ) : (
          <CircleHelp className="size-3.5" />
        )}
      </span>
    );
  }
  const Icon = def.icon;
  return (
    <span
      className={cn(
        "flex size-6 shrink-0 items-center justify-center rounded-md",
        def.accent,
        className,
      )}
    >
      {running ? (
        <Spinner className="size-3.5" />
      ) : (
        <Icon className="size-3.5" />
      )}
    </span>
  );
}
