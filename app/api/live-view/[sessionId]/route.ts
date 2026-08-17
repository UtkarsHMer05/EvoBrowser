import * as Sentry from "@sentry/nextjs";
import { auth } from "@clerk/nextjs/server";
import { NextResponse } from "next/server";

import { getBrowserbaseClient } from "@/lib/browserbase";

// Returns the Browserbase live-view debug URLs for an active session. The
// `sessions.debug()` call requires the secret API key, so it can only run
// server-side — the client gets back the `debuggerFullscreenUrl` it needs to
// render an iframe, nothing more.
export async function GET(
  _request: Request,
  { params }: { params: Promise<{ sessionId: string }> },
) {
  const { userId, orgId } = await auth();
  if (!userId || !orgId) {
    return NextResponse.json({ error: "Unauthorized" }, { status: 401 });
  }

  const { sessionId } = await params;

  Sentry.getIsolationScope().setAttributes({
    route: "GET /api/live-view/[sessionId]",
    userId,
    orgId,
    sessionId,
  });

  try {
    const liveUrls =
      await getBrowserbaseClient().sessions.debug(sessionId);

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
