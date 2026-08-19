// Phase 2 — engine-neutral run data readers for API routes (Milestone 29).
//
// Evo runs have no Trigger.dev run record. Their durable state — the run row,
// the Browserbase session id the worker stamped, and the final screenshot —
// lives in the LOCAL Phase-2 Postgres. These helpers let the screenshot and
// live-view routes resolve ownership and read that state for Evo runs with the
// same org-scoped guarantees the legacy Trigger.dev path has.
//
// Every read is org-scoped and fail-open: an unreachable Phase-2 store yields
// undefined (never throws), so a Phase-2 outage can only make an Evo artifact
// temporarily unavailable — it can never leak another org's data or break the
// legacy path.

import { and, eq } from "drizzle-orm";

import { getPhase2Db } from "@/lib/db/phase2";
import {
  liveViewConnections,
  runArtifacts,
  workflowRuns,
} from "@/lib/db/schema";

/**
 * Return the orgId that owns an Evo run, or undefined when the run does not
 * exist in the Phase-2 store (or the store is unreachable). Callers compare
 * this against the caller's org to authorize the request.
 */
export async function getEvoRunOrgId(
  runId: string,
): Promise<string | undefined> {
  try {
    const db = getPhase2Db();
    const [row] = await db
      .select({ orgId: workflowRuns.orgId })
      .from(workflowRuns)
      .where(eq(workflowRuns.id, runId));
    return row?.orgId;
  } catch {
    return undefined;
  }
}

/**
 * Read an Evo run's artifact (final screenshot) from the Phase-2 store,
 * org-scoped. Returns undefined when the run is not owned by the org or has no
 * artifact yet.
 */
export async function getEvoRunArtifact(orgId: string, runId: string) {
  try {
    const db = getPhase2Db();
    const [row] = await db
      .select()
      .from(runArtifacts)
      .where(and(eq(runArtifacts.runId, runId), eq(runArtifacts.orgId, orgId)));
    return row;
  } catch {
    return undefined;
  }
}

/**
 * Return the Browserbase session id an Evo run drove (stamped by the worker as
 * soon as the session opened), org-scoped. The live-view route compares this to
 * the requested session id so a caller can only watch a session its own run
 * actually drove.
 */
export async function getEvoRunBrowserbaseSessionId(
  orgId: string,
  runId: string,
): Promise<string | undefined> {
  try {
    const db = getPhase2Db();
    const [row] = await db
      .select({ browserbaseSessionId: workflowRuns.browserbaseSessionId })
      .from(workflowRuns)
      .where(and(eq(workflowRuns.id, runId), eq(workflowRuns.orgId, orgId)));
    return row?.browserbaseSessionId ?? undefined;
  } catch {
    return undefined;
  }
}

// --- Live-view handshake (Phase-2 store) ------------------------------------
// The Phase-1 handshake row lives in Neon (data.ts markLiveViewConnected), but
// the distributed worker reads the Phase-2 store. These helpers give the Evo
// path the same "hold the first browser step until the live view connects"
// behavior, writing/reading the handshake in the Phase-2 store.

/** Record that the watching browser's live view connected (Phase-2 store). */
export async function markEvoLiveViewConnected(
  sessionId: string,
  runId?: string,
): Promise<void> {
  try {
    const db = getPhase2Db();
    await db
      .insert(liveViewConnections)
      .values({ sessionId, runId })
      .onConflictDoNothing();
  } catch {
    // Best-effort: a missed handshake must never fail the run.
  }
}

/** Whether the live view connected for a session (Phase-2 store). */
export async function isEvoLiveViewConnected(
  sessionId: string,
): Promise<boolean> {
  try {
    const db = getPhase2Db();
    const [row] = await db
      .select()
      .from(liveViewConnections)
      .where(eq(liveViewConnections.sessionId, sessionId));
    return Boolean(row);
  } catch {
    // Fail-open: treat as connected so an unwatched run never hangs.
    return true;
  }
}
