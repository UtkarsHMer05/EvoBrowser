import * as Sentry from "@sentry/nextjs";
import { runs } from "@trigger.dev/sdk";
import { NextResponse } from "next/server";

import { readAuthWithRetry, resolveActiveOrgId } from "@/lib/auth";
import { getRunArtifact } from "@/features/workflows/data";
import type { runWorkflowTask } from "@/features/workflows/tasks/run-workflow";

// Serves a run's final screenshot (base64 JPEG captured by the task before the
// Browserbase session closed) for the results popup.
//
// Ownership: the caller must own the run — we retrieve it from Trigger.dev and
// check its payload orgId (set server-side by runWorkflowAction after auth)
// against the caller's org, then re-check the artifact row's orgId.
export async function GET(
  _request: Request,
  { params }: { params: Promise<{ runId: string }> },
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

  const { runId } = await params;

  Sentry.getIsolationScope().setAttributes({
    route: "GET /api/runs/[runId]/screenshot",
    userId,
    orgId,
    runId,
  });

  let run;
  try {
    run = await runs.retrieve<typeof runWorkflowTask>(runId);
  } catch {
    return NextResponse.json({ error: "Run not found." }, { status: 404 });
  }

  if (run.payload?.orgId !== orgId) {
    return NextResponse.json({ error: "Forbidden" }, { status: 403 });
  }

  const artifact = await getRunArtifact(orgId, runId);
  if (!artifact?.screenshotBase64) {
    return NextResponse.json(
      { error: "No screenshot available for this run." },
      { status: 404 },
    );
  }

  return new Response(
    Buffer.from(artifact.screenshotBase64, "base64"),
    {
      headers: {
        "Content-Type": "image/jpeg",
        // The screenshot is immutable once the run ends — safe to cache.
        "Cache-Control": "private, max-age=3600, immutable",
      },
    },
  );
}
