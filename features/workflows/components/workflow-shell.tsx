"use client";

import { useState } from "react";
import { useMutation } from "@liveblocks/react";
import { LiveObject, LiveMap } from "@liveblocks/client";
import type { Edge } from "@xyflow/react";
import { toast } from "sonner";

import {
  ResizableHandle,
  ResizablePanel,
  ResizablePanelGroup,
} from "@/components/ui/resizable";

import { planWorkflowAction } from "@/features/workflows/actions";
import { convertWorkflowPlanToGraph } from "@/features/workflows/lib/convert-plan";
import type { StepNodeType } from "@/features/workflows/nodes/node-registry";
import {
  useLiveBrowserbaseSessionId,
  useLiveRun,
} from "./workflow-runs-provider";
import { Canvas } from "./canvas";
import { ConsolePanel } from "./console-panel";
import { LiveBrowser } from "./live-browser";
import { PlannerStart } from "./planner-start";
import { RightSidebar } from "./right-sidebar";

interface WorkflowShellProps {
  workflowId: string;
  isNew?: boolean;
}

export function WorkflowShell({
  workflowId,
  isNew = false,
}: WorkflowShellProps) {
  const [viewMode, setViewMode] = useState<"planner" | "canvas">(
    isNew ? "planner" : "canvas",
  );
  const [isPreview, setIsPreview] = useState(false);

  // Live Browserbase session from Trigger.dev realtime metadata (Milestone 9).
  const liveBrowserbaseSessionId = useLiveBrowserbaseSessionId();
  const liveRun = useLiveRun();
  const isRunLive = !!liveRun;

  // Show the live browser panel when:
  // 1. A run is currently executing, OR
  // 2. A session ID is/was available (allows "ended" state to display gracefully)
  const [hadSession, setHadSession] = useState(false);
  if (liveBrowserbaseSessionId && !hadSession) {
    setHadSession(true);
  }
  // Reset when there's no live run and session is gone
  if (!isRunLive && !liveBrowserbaseSessionId && hadSession) {
    // Delay reset so the "ended" state renders briefly
    setTimeout(() => setHadSession(false), 5000);
  }
  const showLiveBrowser = isRunLive || hadSession;

  // Mutation to replace the Liveblocks room's flow storage with the generated graph.
  // This automatically synchronizes to all connected clients and React Flow.
  const applyWorkflowGraph = useMutation(
    (
      { storage },
      { nodes, edges }: { nodes: StepNodeType[]; edges: Edge[] },
    ) => {
      // eslint-disable-next-line @typescript-eslint/no-explicit-any
      const root = storage as any;
      let flow = root.get("flow");

      if (!flow) {
        flow = new LiveObject({
          nodes: new LiveMap(),
          edges: new LiveMap(),
        });
        root.set("flow", flow);
      }

      const nodesMap = flow.get("nodes");
      const edgesMap = flow.get("edges");

      // Clear existing nodes and edges
      for (const key of Array.from(nodesMap.keys())) {
        nodesMap.delete(key);
      }
      for (const key of Array.from(edgesMap.keys())) {
        edgesMap.delete(key);
      }

      // Insert new nodes and edges
      for (const node of nodes) {
        // eslint-disable-next-line @typescript-eslint/no-explicit-any
        nodesMap.set(node.id, LiveObject.from(node as any));
      }
      for (const edge of edges) {
        // eslint-disable-next-line @typescript-eslint/no-explicit-any
        edgesMap.set(edge.id, LiveObject.from(edge as any));
      }
    },
    [],
  );

  const handleBuildManually = () => {
    setViewMode("canvas");
    setIsPreview(false);
    if (
      typeof window !== "undefined" &&
      window.location.search.includes("new=")
    ) {
      const url = new URL(window.location.href);
      url.searchParams.delete("new");
      window.history.replaceState({}, "", url.pathname + url.search);
    }
  };

  const handleGenerate = async (goal: string) => {
    const result = await planWorkflowAction({ workflowId, goal });

    if (!result.success || !result.plan) {
      throw new Error(result.error || "Failed to generate workflow plan.");
    }

    if (!result.plan.canBuild) {
      throw new Error(
        result.plan.unsupportedReason ||
          "This goal cannot be automated with the available workflow nodes.",
      );
    }

    // Convert plan nodes & edges with deterministic layered DAG layout + validate
    const { nodes, edges } = convertWorkflowPlanToGraph(result.plan);

    // Apply the graph to Liveblocks storage so it appears on the canvas collaboratively
    applyWorkflowGraph({ nodes, edges });

    // Transition to the workflow canvas and show the preview indicator
    setIsPreview(true);
    setViewMode("canvas");

    // Clean up ?new=true query param
    if (
      typeof window !== "undefined" &&
      window.location.search.includes("new=")
    ) {
      const url = new URL(window.location.href);
      url.searchParams.delete("new");
      window.history.replaceState({}, "", url.pathname + url.search);
    }

    toast.success(
      "Workflow plan generated! Review your steps and click Run when ready.",
    );
  };

  if (viewMode === "planner") {
    return (
      <PlannerStart
        onBuildManually={handleBuildManually}
        onGenerate={handleGenerate}
      />
    );
  }

  return (
    <ResizablePanelGroup orientation="horizontal" className="size-full">
      <ResizablePanel minSize="30rem">
        <ResizablePanelGroup orientation="vertical">
          <ResizablePanel minSize="18rem">
            {showLiveBrowser ? (
              <ResizablePanelGroup orientation="horizontal">
                <ResizablePanel minSize="20rem">
                  <Canvas
                    isPreview={isPreview}
                    onDismissPreview={() => setIsPreview(false)}
                  />
                </ResizablePanel>
                <ResizableHandle />
                <ResizablePanel defaultSize="50%" minSize="16rem">
                  <LiveBrowser
                    sessionId={liveBrowserbaseSessionId}
                    isRunLive={isRunLive}
                  />
                </ResizablePanel>
              </ResizablePanelGroup>
            ) : (
              <Canvas
                isPreview={isPreview}
                onDismissPreview={() => setIsPreview(false)}
              />
            )}
          </ResizablePanel>
          <ResizableHandle />
          <ResizablePanel defaultSize="8rem" minSize="6rem">
            <ConsolePanel />
          </ResizablePanel>
        </ResizablePanelGroup>
      </ResizablePanel>
      <ResizableHandle />
      <ResizablePanel defaultSize="16rem" minSize="14rem" maxSize="36rem">
        <RightSidebar workflowId={workflowId} />
      </ResizablePanel>
    </ResizablePanelGroup>
  );
}

