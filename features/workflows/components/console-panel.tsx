"use client";

import { useEffect, useRef, useState } from "react";

import {
  ResizableHandle,
  ResizablePanel,
  ResizablePanelGroup,
} from "@/components/ui/resizable";

import { InspectorPanel } from "@/features/workflows/components/inspector-panel";
import {
  LogsPanel,
  type ConsoleSelection,
} from "@/features/workflows/components/logs-panel";
import {
  useLatestRun,
  useLatestRunSteps,
} from "@/features/workflows/components/workflow-runs-provider";

// True when two selections point at the same thing — same kind, same run, and
// for a step the same node. Clicking the active selection again clears it.
function isSameSelection(a: ConsoleSelection, b: ConsoleSelection) {
  if (a.kind !== b.kind) return false;
  if (a.runId !== b.runId) return false;
  return a.kind === "step" && b.kind === "step" ? a.nodeId === b.nodeId : true;
}

// The run console below the canvas. It owns what's selected: the logs on the
// left drive the selection, and the InspectorPanel on the right shows either the
// selected step's output or the selected run's replay. Clicking the active
// selection again clears it.
//
// When a run finishes, fails, or is stopped, its completion summary opens here
// automatically (Milestone 14). It stays a dismissible console selection — the
// canvas above is never replaced, and closing the selection returns the console
// to its plain logs view so the graph can be edited and run again.
export function ConsolePanel({ workflowId }: { workflowId: string }) {
  const [selected, setSelected] = useState<ConsoleSelection | null>(null);

  // Surface the completion summary the moment the latest run flips from live
  // to finished. A ref tracks the previous live state so a page load that
  // starts with an already-finished run doesn't force the panel open.
  const latestRun = useLatestRun();
  const { isLive } = useLatestRunSteps();
  const wasLiveRef = useRef(false);

  useEffect(() => {
    if (wasLiveRef.current && !isLive && latestRun) {
      setSelected({ kind: "summary", runId: latestRun.id });
    }
    wasLiveRef.current = isLive;
  }, [isLive, latestRun]);

  const toggle = (selection: ConsoleSelection) => {
    setSelected((prev) =>
      prev && isSameSelection(prev, selection) ? null : selection,
    );
  };

  return (
    <ResizablePanelGroup orientation="horizontal" className="size-full">
      <ResizablePanel minSize="12rem">
        <LogsPanel selected={selected} onSelect={toggle} />
      </ResizablePanel>
      {selected && (
        <>
          <ResizableHandle withHandle />
          <ResizablePanel defaultSize="20rem" minSize="12rem">
            <InspectorPanel
              selection={selected}
              workflowId={workflowId}
              onSelect={toggle}
            />
          </ResizablePanel>
        </>
      )}
    </ResizablePanelGroup>
  );
}
