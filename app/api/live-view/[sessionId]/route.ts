import * as Sentry from "@sentry/nextjs";
import { runs } from "@trigger.dev/sdk";
import { NextResponse } from "next/server";

import { getBrowserbaseClient } from "@/lib/browserbase";
import { readAuthWithRetry, resolveActiveOrgId } from "@/lib/auth";
import { getWorkflow } from "@/features/workflows/data";
import {
  getEvoRunBrowserbaseSessionId,
  getEvoRunOrgId,
} from "@/features/workflows/lib/evo-run-data";
import type { runWorkflowTask } from "@/features/workflows/tasks/run-workflow";

// Returns the Browserbase live-view debug URLs for an active session. The
// `sessions.debug()` call requires the secret API key, so it can only run
// server-side — the client gets back the `debuggerFullscreenUrl` it needs to
// render an iframe, nothing more.
//
// The caller must also prove it owns the session: it passes the run id that
// produced the session, and we verify that run belongs to the caller's org and
// actually drove this session. Without that, any signed-in user could view
// another org's live browser session by guessing a session id.
//
// Ownership resolution is engine-aware (Milestone 29): legacy runs are resolved
// from Trigger.dev (payload orgId + published session id); Evo runs have no
// Trigger.dev record, so they are resolved from the local Phase-2 Postgres run
// row (orgId + the Browserbase session id the worker stamped).
export async function GET(
  request: Request,
  { params }: { params: Promise<{ sessionId: string }> },
) {
  const { userId } = await readAuthWithRetry();
  if (!userId) {
    return NextResponse.json({ error: "Unauthorized" }, { status: 401 });
  }

  let orgId: string;
  try {
    orgId = await resolveActiveOrgId();
  } catch {
    return NextResponse.json({ error: "Unauthorized" }, { status: 401 });
  }

  const { sessionId } = await params;
  const runId = new URL(request.url).searchParams.get("runId");
  if (!runId) {
    return NextResponse.json(
      { error: "Missing runId query parameter." },
      { status: 400 },
    );
  }

  Sentry.getIsolationScope().setAttributes({
    route: "GET /api/live-view/[sessionId]",
    userId,
    orgId,
    sessionId,
    runId,
  });

  // Resolve the run and confirm it belongs to this org and drove this session.
  // Try the legacy Trigger.dev path first (Phase-1 behavior unchanged).
  let run;
  try {
    run = await runs.retrieve<typeof runWorkflowTask>(runId);
  } catch {
    run = undefined;
  }

  if (run) {
    // Legacy path: the payload's orgId was set server-side by runWorkflowAction
    // after auth, so it is a trustworthy ownership signal.
    const runWorkflowId = run.payload?.workflowId;
    if (run.payload?.orgId !== orgId || !runWorkflowId) {
      Sentry.logger.warn("Live view denied — run belongs to another org", {
        runId,
        orgId,
      });
      return NextResponse.json({ error: "Forbidden" }, { status: 403 });
    }

    // Defense in depth: the workflow itself must still exist in this org.
    const workflow = await getWorkflow(orgId, runWorkflowId);
    if (!workflow) {
      return NextResponse.json({ error: "Forbidden" }, { status: 403 });
    }

    // The session id must match what this run published (live metadata while
    // executing, final output once done) — never expose unrelated sessions.
    const runSessionId =
      run.output?.browserbaseSessionId ??
      (run.metadata?.browserbaseSessionId as string | undefined);
    if (runSessionId !== sessionId) {
      Sentry.logger.warn("Live view denied — session does not belong to run", {
        runId,
        orgId,
      });
      return NextResponse.json({ error: "Forbidden" }, { status: 403 });
    }
  } else {
    // Evo path (M29): resolve ownership from the Phase-2 store.
    const evoOrgId = await getEvoRunOrgId(runId);
    if (!evoOrgId) {
      Sentry.logger.warn("Live view denied — run not found", {
        runId,
        orgId,
      });
      return NextResponse.json({ error: "Run not found." }, { status: 404 });
    }
    if (evoOrgId !== orgId) {
      Sentry.logger.warn("Live view denied — run belongs to another org", {
        runId,
        orgId,
      });
      return NextResponse.json({ error: "Forbidden" }, { status: 403 });
    }

    // The session id must match the one the worker stamped on the run row.
    const runSessionId = await getEvoRunBrowserbaseSessionId(orgId, runId);
    if (runSessionId !== sessionId) {
      Sentry.logger.warn("Live view denied — session does not belong to run", {
        runId,
        orgId,
      });
      return NextResponse.json({ error: "Forbidden" }, { status: 403 });
    }
  }

  try {
    const liveUrls = await getBrowserbaseClient().sessions.debug(sessionId);

    Sentry.logger.info("Live view URLs served", {
      sessionId,
      orgId,
      pageCount: liveUrls.pages.length,
    });

    return NextResponse.json({
      debuggerFullscreenUrl: liveUrls.debuggerFullscreenUrl,
      debuggerUrl: liveUrls.debuggerUrl,
    });
  } catch (error) {
    Sentry.logger.warn("Live view unavailable", {
      sessionId,
      orgId,
      error: error instanceof Error ? error.message : String(error),
    });

    return NextResponse.json(
      { error: "Live view unavailable for this session." },
      { status: 404 },
    );
  }
}
