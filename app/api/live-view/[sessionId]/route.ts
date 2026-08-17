import * as Sentry from "@sentry/nextjs";
import { auth } from "@clerk/nextjs/server";
import { runs } from "@trigger.dev/sdk";
import { NextResponse } from "next/server";

import { getBrowserbaseClient } from "@/lib/browserbase";
import { resolveActiveOrgId } from "@/lib/auth";
import { getWorkflow } from "@/features/workflows/data";
import type { runWorkflowTask } from "@/features/workflows/tasks/run-workflow";

// Returns the Browserbase live-view debug URLs for an active session. The
// `sessions.debug()` call requires the secret API key, so it can only run
// server-side — the client gets back the `debuggerFullscreenUrl` it needs to
// render an iframe, nothing more.
//
// The caller must also prove it owns the session: it passes the Trigger.dev run
// id that produced the session, and we verify that run belongs to the caller's
// org and actually drove this session. Without that, any signed-in user could
// view another org's live browser session by guessing a session id.
export async function GET(
  request: Request,
  { params }: { params: Promise<{ sessionId: string }> },
) {
  const { userId } = await auth();
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
  // The payload's orgId was set server-side by runWorkflowAction after auth, so
  // it is a trustworthy ownership signal.
  let run;
  try {
    run = await runs.retrieve<typeof runWorkflowTask>(runId);
  } catch (error) {
    Sentry.logger.warn("Live view denied — run not found", {
      runId,
      orgId,
      error: error instanceof Error ? error.message : String(error),
    });
    return NextResponse.json({ error: "Run not found." }, { status: 404 });
  }

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
