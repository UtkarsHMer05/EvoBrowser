"use client";

import { useState } from "react";

import {
  ResizableHandle,
  ResizablePanel,
  ResizablePanelGroup,
} from "@/components/ui/resizable";

import { planWorkflowAction } from "@/features/workflows/actions";
import { Canvas } from "./canvas";
import { ConsolePanel } from "./console-panel";
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

  const handleBuildManually = () => {
    setViewMode("canvas");
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
    if (!result.success && result.error) {
      throw new Error(result.error);
    }
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
            <Canvas />
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

