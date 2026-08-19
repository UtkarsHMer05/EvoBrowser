// Phase 2 — server-side Evo run listing for the UI (Milestone 29).
//
// The UI's run console/history needs the workflow's Evo runs. Those live in the
// LOCAL Phase-2 Postgres (where the C++ engine + workers persist run/node
// state), NOT in Neon. This module reads them and maps each to the normalized
// view model the UI renders.
//
// Org scoping: every read filters by orgId (tenant guard). A run the caller's
// org does not own is never returned.
//
// Timestamps are wall-clock UTC (database now()), consistent with M19/M26.

import { and, desc, eq } from "drizzle-orm";

import { getPhase2Db } from "@/lib/db/phase2";
import { nodeRuns, workflowRuns } from "@/lib/db/schema";

import {
  finalizeViewModel,
  type NormalizedRunStatus,
  type NormalizedRunStep,
  type NormalizedRunViewModel,
} from "./run-view-model";
import type { VersioningDb } from "./workflow-versions";

function mapRunStatus(status: string): NormalizedRunStatus {
  switch (status) {
    case "queued":
      return "queued";
    case "running":
      return "running";
    case "succeeded":
      return "succeeded";
    case "failed":
      return "failed";
    case "canceled":
      return "canceled";
    default:
      return "unknown";
  }
}

function mapNodeStatus(status: string): NormalizedRunStep["status"] {
  switch (status) {
    case "succeeded":
      return "done";
    case "failed":
    case "dead_lettered":
      return "failed";
    case "canceled":
      return "canceled";
    case "running":
    case "dispatched":
    case "ready":
      return "running";
    default:
      return "pending"; // blocked / queued
  }
}

/**
 * List a workflow's Evo runs, newest first, each mapped to the normalized
 * view model with its node steps. Org-scoped. Returns [] when the workflow has
 * no Evo runs (or the Phase-2 store is unreachable — fail-open so a Phase-2
 * outage never breaks the legacy UI).
 */
export async function listEvoRunsForWorkflow(
  orgId: string,
  workflowId: string,
  db?: VersioningDb,
): Promise<NormalizedRunViewModel[]> {
  let store: VersioningDb;
  try {
    store = db ?? getPhase2Db();
  } catch {
    return [];
  }

  let runs;
  try {
    runs = await store
      .select()
      .from(workflowRuns)
      .where(
        and(
          eq(workflowRuns.orgId, orgId),
          eq(workflowRuns.workflowId, workflowId),
          eq(workflowRuns.engine, "evo"),
        ),
      )
      .orderBy(desc(workflowRuns.createdAt));
  } catch {
    return [];
  }

  const result: NormalizedRunViewModel[] = [];
  for (const run of runs) {
    let nodes: Array<typeof nodeRuns.$inferSelect> = [];
    try {
      nodes = await store
        .select()
        .from(nodeRuns)
        .where(eq(nodeRuns.runId, run.id));
    } catch {
      nodes = [];
    }

    const steps: NormalizedRunStep[] = nodes.map((n) => ({
      nodeId: n.nodeId,
      type: n.nodeType,
      title: n.nodeId,
      status: mapNodeStatus(n.status),
      output: n.output ?? undefined,
      error: n.failureReason ?? undefined,
      durationMs:
        n.startedAt && n.finishedAt
          ? n.finishedAt.getTime() - n.startedAt.getTime()
          : undefined,
    }));

    const durationMs =
      run.startedAt && run.finishedAt
        ? run.finishedAt.getTime() - run.startedAt.getTime()
        : undefined;

    result.push(
      finalizeViewModel({
        id: run.id,
        engine: "evo",
        status: mapRunStatus(run.status),
        createdAt: run.createdAt,
        steps,
        // M29: the worker stamps the Browserbase session id on the run row as
        // soon as the session opens (durable source of truth for replay /
        // live-view / screenshot).
        browserbaseSessionId: run.browserbaseSessionId ?? undefined,
        liveBrowserbaseSessionId: run.browserbaseSessionId ?? undefined,
        durationMs,
      }),
    );
  }
  return result;
}
