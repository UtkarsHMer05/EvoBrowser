"use server";

import * as Sentry from "@sentry/nextjs";
import { auth } from "@clerk/nextjs/server";
import { runs, tasks } from "@trigger.dev/sdk";
import { revalidatePath } from "next/cache";
import { redirect } from "next/navigation";

import type { runWorkflowTask } from "@/features/workflows/tasks/run-workflow";

import { getLiveblocksClient } from "@/lib/liveblocks";
import {
  createWorkflow,
  deleteWorkflow,
  saveWorkflowGraph,
} from "@/features/workflows/data";
import { WorkflowGraph } from "@/lib/db/schema";
import type {
  PlanWorkflowGoalInput,
  PlanWorkflowGoalResult,
} from "@/features/workflows/lib/planner-types";
import { generateWorkflowPlan } from "@/features/workflows/lib/planner-service";

export async function createWorkflowAction(name: string) {
  const { orgId } = await auth();

  if (!orgId) {
    throw new Error("No active organization");
  }

  Sentry.getIsolationScope().setAttributes({
    action: "createWorkflowAction",
    orgId,
  });

  const workflow = await createWorkflow(orgId, name);

  Sentry.logger.info("Workflow created", { workflowId: workflow.id, orgId });

  revalidatePath("/workflows", "layout");
  redirect(`/workflows/${workflow.id}?new=true`);
}

export async function deleteWorkflowAction(id: string) {
  const { orgId } = await auth();

  if (!orgId) {
    throw new Error("No active organization");
  }

  Sentry.getIsolationScope().setAttributes({
    action: "deleteWorkflowAction",
    orgId,
    workflowId: id,
  });

  const workflow = await deleteWorkflow(orgId, id);

  if (!workflow) {
    Sentry.logger.warn("Workflow delete skipped — not found", {
      workflowId: id,
      orgId,
    });
    throw new Error("Workflow not found");
  }

  // The workflow id doubles as its Liveblocks room id — clean it up too.
  await getLiveblocksClient().deleteRoom(id);

  Sentry.logger.info("Workflow deleted", { workflowId: id, orgId });

  revalidatePath("/workflows", "layout");
  redirect("/");
}

export async function runWorkflowAction({
  id,
  graph,
}: {
  id: string;
  graph: WorkflowGraph;
}) {
  const { orgId, has } = await auth();

  if (!orgId) {
    throw new Error("No active organization");
  }

  // The Agent node is Pro-only. Enforce it here rather than in the run task: the
  // action holds the Clerk session (and has()), while the Trigger.dev task runs
  // with no auth context. has() evaluates the active org, confirmed above.
  Sentry.getIsolationScope().setAttributes({
    action: "runWorkflowAction",
    orgId,
    workflowId: id,
  });

  const hasAgentNode = graph.nodes.some((node) => node.data.type === "agent");
  if (hasAgentNode && !has({ plan: "pro" })) {
    Sentry.logger.warn("Workflow run denied — Agent node requires Pro plan", {
      workflowId: id,
      orgId,
    });
    throw new Error("The Agent node requires the Pro plan.");
  }

  try {
    await saveWorkflowGraph({ orgId, id, graph });
  } catch (error) {
    Sentry.logger.warn("Workflow run blocked — graph validation failed", {
      workflowId: id,
      orgId,
    });
    throw error;
  }

  const handle = await tasks.trigger<typeof runWorkflowTask>(
    "run-workflow",
    { workflowId: id, orgId },
    { tags: [`workflow:${id}`] },
  );

  Sentry.logger.info("Workflow run triggered", {
    workflowId: id,
    orgId,
    runId: handle.id,
    nodeCount: graph.nodes.length,
    hasAgentNode,
  });

  return handle;
}

export async function cancelWorkflowRunAction(runId: string) {
  const { orgId } = await auth();
  if (!orgId) throw new Error("No active organization");

  Sentry.getIsolationScope().setAttributes({
    action: "cancelWorkflowRunAction",
    orgId,
    runId,
  });

  await runs.cancel(runId);

  Sentry.logger.info("Workflow run cancelled", { runId, orgId });
}

export async function planWorkflowAction({
  workflowId,
  goal,
}: PlanWorkflowGoalInput): Promise<PlanWorkflowGoalResult> {
  const { orgId } = await auth();

  if (!orgId) {
    throw new Error("No active organization");
  }

  const trimmedGoal = goal.trim();
  if (!trimmedGoal) {
    throw new Error("Goal prompt cannot be empty.");
  }

  if (trimmedGoal.length > 2000) {
    throw new Error("Goal prompt exceeds maximum length of 2000 characters.");
  }

  Sentry.getIsolationScope().setAttributes({
    action: "planWorkflowAction",
    orgId,
    workflowId,
    goalLength: trimmedGoal.length,
  });

  try {
    const plan = await generateWorkflowPlan({ goal: trimmedGoal });

    Sentry.logger.info("Workflow plan generated", {
      workflowId,
      orgId,
      canBuild: plan.canBuild,
      nodeCount: plan.nodes.length,
      edgeCount: plan.edges.length,
    });

    return {
      success: true,
      message: plan.canBuild
        ? "Workflow plan generated successfully."
        : plan.unsupportedReason,
      plan,
    };
  } catch (error) {
    const errorMessage =
      error instanceof Error ? error.message : "Failed to plan workflow.";

    Sentry.logger.error("Workflow planning failed", {
      workflowId,
      orgId,
      error: errorMessage,
    });

    return {
      success: false,
      error: errorMessage,
    };
  }
}


