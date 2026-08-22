import * as Sentry from "@sentry/nextjs";
import { NextResponse } from "next/server";

import { getBrowserbaseClient } from "@/lib/browserbase";
import { readAuthWithRetry, resolveActiveOrgId } from "@/lib/auth";
import { authorizeRunAccess } from "@/features/workflows/lib/run-authorization";

// Returns the Browserbase live-view debug URLs for an active session. The
// `sessions.debug()` call requires the secret API key, so it can only run
// server-side — the client gets back the `debuggerFullscreenUrl` it needs to
// render an iframe, nothing more.
//
// The caller must also prove it owns the session: it passes the run id that
// produced the session, and we verify that run belongs to the caller's org and
// actually drove this session. Without that, any signed-in user could view
// another org's live browser session by guessing a session id. The shared
// engine-aware check lives in authorizeRunAccess; this route only maps the
// outcome onto HTTP responses.
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
  const access = await authorizeRunAccess({ orgId, runId, sessionId });
  if (!access.ok) {
    return access.status === 404
      ? NextResponse.json({ error: "Run not found." }, { status: 404 })
      : NextResponse.json({ error: "Forbidden" }, { status: 403 });
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
