// Engine-aware run authorization shared by every proxy route that serves
// per-run browser artifacts (live view, handshake, screenshot, replay).
//
// Every one of these routes used to hand-roll the same ownership block, and the
// replay route grew without it entirely — it checked auth + plan but never
// verified that the requested Browserbase session belonged to a run owned by
// the caller's organization. That meant any Pro member could fetch another
// org's recording by guessing a session id. This helper exists so all four
// routes enforce exactly one definition of "this caller owns this run".
//
// Ownership resolution is engine-aware (Milestone 29):
//   - Legacy (Phase-1) runs resolve from Trigger.dev: the payload orgId was set
//     server-side by runWorkflowAction after auth, so it is a trustworthy
//     ownership signal; defense-in-depth re-checks the workflow row and — when
//     a sessionId is supplied — matches the session the run actually drove.
//   - Evo runs have no Trigger.dev record; they resolve from the local Phase-2
//     Postgres run row (orgId + the Browserbase session id the worker stamped).
//
// The result is a discriminated outcome instead of a thrown error so route
// handlers can map it onto their own response shapes (JSON vs text) while the
// decision itself stays in one audited place.

import * as Sentry from "@sentry/nextjs";
import { runs } from "@trigger.dev/sdk";

import { getWorkflow } from "@/features/workflows/data";
import {
  getEvoRunBrowserbaseSessionId,
  getEvoRunOrgId,
} from "@/features/workflows/lib/evo-run-data";
import type { runWorkflowTask } from "@/features/workflows/tasks/run-workflow";

export type RunEngine = "legacy" | "evo";

export type RunAccessResult =
  | { ok: true; engine: RunEngine }
  | { ok: false; engine?: undefined; status: 403 | 404; reason: string };

function deny(
  status: 403 | 404,
  reason: string,
  scope: { runId: string; orgId: string },
): { ok: false; status: 403 | 404; reason: string } {
  Sentry.logger.warn(`Run access denied — ${reason}`, { ...scope, reason });
  return { ok: false, status, reason };
}

/**
 * Verify the caller's organization owns `runId` and — when `sessionId` is
 * given — that the run actually drove that Browserbase session. Never throws:
 * an unreachable store resolves to the same denials an unknown run gets.
 */
export async function authorizeRunAccess({
  orgId,
  runId,
  sessionId,
}: {
  orgId: string;
  runId: string;
  sessionId?: string;
}): Promise<RunAccessResult> {
  // Legacy path first (Phase-1 behavior): the run lives in Trigger.dev.
  type TriggerRun = Awaited<
    ReturnType<typeof runs.retrieve<typeof runWorkflowTask>>
  >;
  let run: TriggerRun | undefined;
  try {
    run = await runs.retrieve<typeof runWorkflowTask>(runId);
  } catch {
    run = undefined;
  }

  if (run) {
    const runWorkflowId = run.payload?.workflowId;
    if (run.payload?.orgId !== orgId || !runWorkflowId) {
      return deny(403, "run belongs to another org", { runId, orgId });
    }

    // Defense in depth: the workflow itself must still exist in this org.
    const workflow = await getWorkflow(orgId, runWorkflowId);
    if (!workflow) {
      return deny(403, "workflow not found in org", { runId, orgId });
    }

    if (sessionId !== undefined) {
      // The session id must match what this run published (live metadata while
      // executing, final output once done) — never expose unrelated sessions.
      const runSessionId =
        run.output?.browserbaseSessionId ??
        (run.metadata?.browserbaseSessionId as string | undefined);
      if (runSessionId !== sessionId) {
        return deny(403, "session does not belong to run", { runId, orgId });
      }
    }
    return { ok: true, engine: "legacy" };
  }

  // Evo path (M29): resolve ownership from the Phase-2 store.
  const evoOrgId = await getEvoRunOrgId(runId);
  if (!evoOrgId) {
    return deny(404, "run not found", { runId, orgId });
  }
  if (evoOrgId !== orgId) {
    return deny(403, "run belongs to another org", { runId, orgId });
  }

  if (sessionId !== undefined) {
    const runSessionId = await getEvoRunBrowserbaseSessionId(orgId, runId);
    if (runSessionId !== sessionId) {
      return deny(403, "session does not belong to run", { runId, orgId });
    }
  }
  return { ok: true, engine: "evo" };
}
