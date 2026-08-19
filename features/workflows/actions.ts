"use server";

import { randomUUID } from "node:crypto";

import * as Sentry from "@sentry/nextjs";
import { auth } from "@clerk/nextjs/server";
import { revalidatePath } from "next/cache";
import { redirect } from "next/navigation";

import { getLiveblocksClient } from "@/lib/liveblocks";
import { resolveActiveOrgId } from "@/lib/auth";
import { getPhase2Db } from "@/lib/db/phase2";
import {
  createWorkflow,
  deleteWorkflow,
  getWorkflow,
  saveWorkflowGraph,
} from "@/features/workflows/data";
import { createWorkflowVersion } from "@/features/workflows/lib/workflow-versions";
import {
  getExecutionEngine,
  getExecutionEngineAdapter,
} from "@/features/workflows/lib/execution-engine";
import {
  createWorkflowRunRecord,
  ensurePhase2Workflow,
  resolveRunEngine,
} from "@/features/workflows/lib/run-records";
import { listEvoRunsForWorkflow } from "@/features/workflows/lib/evo-runs";
import type { NormalizedRunViewModel } from "@/features/workflows/lib/run-view-model";
import { WorkflowGraph } from "@/lib/db/schema";
import type {
  PlanWorkflowGoalInput,
  PlanWorkflowGoalResult,
} from "@/features/workflows/lib/planner-types";
import { generateWorkflowPlan } from "@/features/workflows/lib/planner-service";

export async function createWorkflowAction(name: string) {
  const orgId = await resolveActiveOrgId();

  Sentry.getIsolationScope().setAttributes({
    action: "createWorkflowAction",
    orgId,
  });

  const workflow = await createWorkflow(orgId, name);

  Sentry.logger.info("Workflow created", { workflowId: workflow.id, orgId });

  // The sidebar's workflow list is rendered by the root (dashboard) layout, so
  // invalidate from "/" — there is no /workflows layout to revalidate.
  revalidatePath("/", "layout");
  redirect(`/workflows/${workflow.id}?new=true`);
}

export async function deleteWorkflowAction(id: string) {
  const orgId = await resolveActiveOrgId();

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

  revalidatePath("/", "layout");
  redirect("/");
}

export async function runWorkflowAction({
  id,
  graph,
}: {
  id: string;
  graph: WorkflowGraph;
}) {
  // Resolve the org robustly — the active-org claim can be missing from a
  // session token snapshot even for a signed-in user with an active org, so
  // fall back to the verified membership list rather than failing the run.
  const orgId = await resolveActiveOrgId();
  const { has } = await auth();

  // The Agent node is Pro-only. Enforce it here rather than in the run task: the
  // action holds the Clerk session (and has()), while the Trigger.dev task runs
  // with no auth context.
  Sentry.getIsolationScope().setAttributes({
    action: "runWorkflowAction",
    orgId,
    workflowId: id,
  });

  const hasAgentNode = graph.nodes.some((node) => node.data.type === "agent");
  if (hasAgentNode && !has?.({ plan: "pro" })) {
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

  // Phase 2 (M20): snapshot the approved graph into an immutable workflow
  // version before any engine executes. Fail-open for the legacy path: the
  // Phase-2 tables may not exist on Neon yet (migrations require explicit
  // human approval), so a snapshot failure must never block a Phase-1 run.
  // For the Evo engine the snapshot is REQUIRED (it executes the immutable
  // version), so a snapshot failure aborts an Evo submission.
  //
  // M29: the Evo engine and its workers read the version snapshot from the
  // LOCAL Phase-2 Postgres (where the C++ engine persists run state), not from
  // Neon. So the Evo snapshot is written to the Phase-2 DB; the legacy path
  // keeps the Neon default.
  const engine = getExecutionEngine();
  let workflowVersionId: string | undefined;
  if (engine === "evo") {
    const phase2Db = getPhase2Db();
    // The Phase-2 workflows table is separate from Neon's; ensure the FK row
    // exists before the version/run rows reference it (mirrors the C++
    // ensure_workflow).
    const wf = await getWorkflow(orgId, id);
    await ensurePhase2Workflow(phase2Db, {
      id,
      orgId,
      name: wf?.name ?? "workflow",
    });
    try {
      const version = await createWorkflowVersion({
        orgId,
        workflowId: id,
        graph,
        db: phase2Db,
      });
      workflowVersionId = version.id;
    } catch (error) {
      // Evo runs execute an immutable version snapshot; without one there is
      // nothing safe to submit. Fail closed.
      Sentry.logger.error(
        "Evo run blocked — workflow version snapshot failed",
        {
          workflowId: id,
          orgId,
          error: error instanceof Error ? error.message : String(error),
        },
      );
      throw new Error("Could not snapshot the workflow for the Evo engine.");
    }
  } else {
    try {
      const version = await createWorkflowVersion({
        orgId,
        workflowId: id,
        graph,
      });
      workflowVersionId = version.id;
    } catch (error) {
      Sentry.logger.warn(
        "Workflow version snapshot skipped (Phase-2 tables unavailable) — legacy run continues",
        {
          workflowId: id,
          orgId,
          error: error instanceof Error ? error.message : String(error),
        },
      );
    }
  }

  // Phase 2 (M27): route through the engine-neutral adapter. The flag
  // (EXECUTION_ENGINE) is fail-closed to legacy; auth + plan gating above ran
  // before either engine was selected.
  const adapter = getExecutionEngineAdapter(engine);

  if (engine === "evo") {
    // Evo: create the engine-neutral run row BEFORE submission (M27 step 6),
    // then submit a client-generated run id to the C++ scheduler. The run row
    // lives in the LOCAL Phase-2 Postgres (same store the engine persists to),
    // so the UI and the engine read consistent state.
    const runId = `evo_${randomUUID()}`;
    await createWorkflowRunRecord({
      runId,
      orgId,
      workflowId: id,
      workflowVersionId,
      engine: "evo",
      db: getPhase2Db(),
    });
    const handle = await adapter.startRun({
      orgId,
      workflowId: id,
      workflowVersionId,
      graph,
      runId,
    });
    Sentry.logger.info("Evo workflow run submitted", {
      workflowId: id,
      orgId,
      runId: handle.runId,
      workflowVersionId,
      nodeCount: graph.nodes.length,
      hasAgentNode,
    });
    return handle;
  }

  // Legacy Trigger.dev path (unchanged behavior). The run row is best-effort
  // (fail-open) so Phase-1 runs never depend on the Phase-2 tables.
  const handle = await adapter.startRun({
    orgId,
    workflowId: id,
    workflowVersionId,
    graph,
  });
  try {
    await createWorkflowRunRecord({
      runId: handle.runId,
      orgId,
      workflowId: id,
      workflowVersionId,
      engine: "legacy",
    });
  } catch (error) {
    Sentry.logger.warn(
      "Legacy run record skipped (Phase-2 tables unavailable)",
      {
        workflowId: id,
        orgId,
        runId: handle.runId,
        error: error instanceof Error ? error.message : String(error),
      },
    );
  }

  Sentry.logger.info("Workflow run triggered", {
    workflowId: id,
    orgId,
    runId: handle.runId,
    workflowVersionId,
    nodeCount: graph.nodes.length,
    hasAgentNode,
  });

  return handle;
}

export async function cancelWorkflowRunAction(runId: string) {
  const orgId = await resolveActiveOrgId();

  Sentry.getIsolationScope().setAttributes({
    action: "cancelWorkflowRunAction",
    orgId,
    runId,
  });

  // Phase 2 (M27/M29): route the cancel to the engine that owns the run. Evo
  // run rows live in the local Phase-2 Postgres; legacy rows in Neon. Runs
  // without a workflow_runs row in either store (legacy Trigger.dev runs
  // predating the run table) resolve to legacy.
  const engine = (await resolveRunEngine(runId)) ?? "legacy";
  const adapter = getExecutionEngineAdapter(engine);
  await adapter.cancelRun(runId);

  Sentry.logger.info("Workflow run cancelled", { runId, orgId, engine });
}

// Phase 2 (M29): list a workflow's Evo runs for the UI console/history. Reads
// from the local Phase-2 Postgres (where the engine persists run state),
// org-scoped. Returns a serializable shape (Dates -> ISO strings) so it can
// cross the server-action boundary. The client revives dates on receipt.
export async function listEvoRunsAction(workflowId: string): Promise<
  Array<
    Omit<NormalizedRunViewModel, "createdAt"> & { createdAt?: string }
  >
> {
  const orgId = await resolveActiveOrgId();

  Sentry.getIsolationScope().setAttributes({
    action: "listEvoRunsAction",
    orgId,
    workflowId,
  });

  const runs = await listEvoRunsForWorkflow(orgId, workflowId);
  return runs.map((r) => ({
    ...r,
    createdAt: r.createdAt ? r.createdAt.toISOString() : undefined,
  }));
}

export async function planWorkflowAction({
  workflowId,
  goal,
}: PlanWorkflowGoalInput): Promise<PlanWorkflowGoalResult> {  const orgId = await resolveActiveOrgId();

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


