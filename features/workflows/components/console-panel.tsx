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
import { useConsoleRuns } from "@/features/workflows/components/workflow-runs-provider";

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

  // Surface the results popup + completion summary only once the latest run
  // reaches a TERMINAL status (completed/failed/canceled). Two guards:
  //  - A run that was already terminal when we first saw it (page load with
  //    history) never auto-opens — only runs we watched progress do.
  //  - Non-terminal states like retrying or waiting-for-deploy don't count as
  //    finished, so the popup can't open mid-run.
  // Each run only ever auto-opens the popup once.
  const runTrackingRef = useRef<{
    runId: string | null;
    terminalOnFirstSight: boolean;
    notifiedRunId: string | null;
  }>({ runId: null, terminalOnFirstSight: false, notifiedRunId: null });

  useEffect(() => {
    const tracking = runTrackingRef.current;
    if (!latestRun) return;

    if (tracking.runId !== latestRun.id) {
      // First time seeing this run — remember whether it arrived already done.
      tracking.runId = latestRun.id;
      tracking.terminalOnFirstSight = latestRun.isTerminal;
    }

    if (
      !tracking.terminalOnFirstSight &&
      latestRun.isTerminal &&
      tracking.notifiedRunId !== latestRun.id
    ) {
      tracking.notifiedRunId = latestRun.id;
      setSelected({ kind: "summary", runId: latestRun.id });
      setResultsRunId(latestRun.id);
    }
  }, [latestRun]);

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
