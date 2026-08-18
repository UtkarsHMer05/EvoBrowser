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
import { RunResultsDialog } from "@/features/workflows/components/run-results-dialog";
import {
  useConsoleRuns,
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
// When a run finishes, fails, or is stopped, its readable results popup opens
// automatically and the completion summary opens in the console (Milestone 14).
// Both are dismissible — the canvas above is never replaced, and closing them
// returns the console to its plain logs view so the graph can be edited and run
// again.
export function ConsolePanel({ workflowId }: { workflowId: string }) {
  const [selected, setSelected] = useState<ConsoleSelection | null>(null);
  // The run whose results popup is open (null = closed). Tracked by id so the
  // dialog can show a historical run, not just the latest.
  const [resultsRunId, setResultsRunId] = useState<string | null>(null);

  const runs = useConsoleRuns();
  const latestRun = runs[0]; // useConsoleRuns sorts newest first
  const resultsRun = runs.find((r) => r.id === resultsRunId);

  // Surface the results popup + completion summary the moment the latest run
  // flips from live to finished. A ref tracks the previous live state so a page
  // load that starts with an already-finished run doesn't force either open.
  const { isLive } = useLatestRunSteps();
  const wasLiveRef = useRef(false);

  useEffect(() => {
    if (wasLiveRef.current && !isLive && latestRun) {
      setSelected({ kind: "summary", runId: latestRun.id });
      setResultsRunId(latestRun.id);
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
              onShowResults={(runId) => {
                setSelected({ kind: "summary", runId });
                setResultsRunId(runId);
              }}
            />
          </ResizablePanel>
        </>
      )}
      <RunResultsDialog
        run={resultsRun}
        workflowId={workflowId}
        open={resultsRunId !== null}
        onOpenChange={(open) => {
          if (!open) setResultsRunId(null);
        }}
        onSelect={toggle}
      />
    </ResizablePanelGroup>
  );
}
