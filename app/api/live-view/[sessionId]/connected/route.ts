import * as Sentry from "@sentry/nextjs";
import { auth } from "@clerk/nextjs/server";
import { runs } from "@trigger.dev/sdk";
import { NextResponse } from "next/server";

import { resolveActiveOrgId } from "@/lib/auth";
import {
  getWorkflow,
  markLiveViewConnected,
} from "@/features/workflows/data";
import type { runWorkflowTask } from "@/features/workflows/tasks/run-workflow";

// The Live Browser iframe calls this when it finishes loading. The run task
// polls for the resulting row and holds its first browser step until it
// appears, so the automation never races ahead of the live view.
//
// Same ownership checks as the GET route: the caller must pass the run id that
// produced the session, and we verify the run belongs to the caller's org and
// actually drove this session.
export async function POST(
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
    route: "POST /api/live-view/[sessionId]/connected",
    userId,
    orgId,
    sessionId,
    runId,
  });

  let run;
  try {
    run = await runs.retrieve<typeof runWorkflowTask>(runId);
  } catch {
    return NextResponse.json({ error: "Run not found." }, { status: 404 });
  }

  const runWorkflowId = run.payload?.workflowId;
  if (run.payload?.orgId !== orgId || !runWorkflowId) {
    return NextResponse.json({ error: "Forbidden" }, { status: 403 });
  }

  const workflow = await getWorkflow(orgId, runWorkflowId);
  if (!workflow) {
    return NextResponse.json({ error: "Forbidden" }, { status: 403 });
  }

  const runSessionId =
    run.output?.browserbaseSessionId ??
    (run.metadata?.browserbaseSessionId as string | undefined);
  if (runSessionId !== sessionId) {
    return NextResponse.json({ error: "Forbidden" }, { status: 403 });
  }

  await markLiveViewConnected(sessionId, runId);

  Sentry.logger.info("Live view connected", { sessionId, orgId, runId });

  return NextResponse.json({ connected: true });
}
