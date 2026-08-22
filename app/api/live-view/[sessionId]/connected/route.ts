import * as Sentry from "@sentry/nextjs";
import { NextResponse } from "next/server";

import { readAuthWithRetry, resolveActiveOrgId } from "@/lib/auth";
import { markLiveViewConnected } from "@/features/workflows/data";
import {
  authorizeRunAccess,
  type RunEngine,
} from "@/features/workflows/lib/run-authorization";
import { markEvoLiveViewConnected } from "@/features/workflows/lib/evo-run-data";

// The Live Browser iframe calls this when it finishes loading. The run task
// polls for the resulting row and holds its first browser step until it
// appears, so the automation never races ahead of the live view.
//
// Same ownership bar as the GET route: the caller must pass the run id that
// produced the session, and we verify the run belongs to the caller's org and
// actually drove this session. The shared engine-aware check lives in
// authorizeRunAccess; only the handshake write below is engine-specific.
export async function POST(
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
    route: "POST /api/live-view/[sessionId]/connected",
    userId,
    orgId,
    sessionId,
    runId,
  });

  const access: { ok: true; engine: RunEngine } | {
    ok: false;
    status: 403 | 404;
  } = await authorizeRunAccess({ orgId, runId, sessionId });
  if (!access.ok) {
    return access.status === 404
      ? NextResponse.json({ error: "Run not found." }, { status: 404 })
      : NextResponse.json({ error: "Forbidden" }, { status: 403 });
  }

  // Write the handshake where the engine that owns the run reads it: legacy
  // runs poll Neon, Evo runs poll the Phase-2 store.
  if (access.engine === "legacy") {
    await markLiveViewConnected(sessionId, runId);
  } else {
    await markEvoLiveViewConnected(sessionId, runId);
  }

  Sentry.logger.info("Live view connected", { sessionId, orgId, runId });

  return NextResponse.json({ connected: true });
}
