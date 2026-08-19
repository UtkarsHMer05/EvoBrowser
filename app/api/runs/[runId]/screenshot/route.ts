import * as Sentry from "@sentry/nextjs";
import { runs } from "@trigger.dev/sdk";
import { NextResponse } from "next/server";

import { readAuthWithRetry, resolveActiveOrgId } from "@/lib/auth";
import { getRunArtifact } from "@/features/workflows/data";
import {
  getEvoRunArtifact,
  getEvoRunOrgId,
} from "@/features/workflows/lib/evo-run-data";
import type { runWorkflowTask } from "@/features/workflows/tasks/run-workflow";

// Serves a run's final screenshot (base64 JPEG captured before the Browserbase
// session closed) for the results popup.
//
// Ownership is engine-aware (Milestone 29):
//   - Legacy runs: retrieved from Trigger.dev; the payload orgId (set
//     server-side by runWorkflowAction after auth) must match the caller's org,
//     then the artifact row's orgId is re-checked.
//   - Evo runs have no Trigger.dev record; their run row and artifact live in
//     the local Phase-2 Postgres. The run row's orgId must match the caller's
//     org, then the artifact is read org-scoped from the same store.
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

  // Try the legacy Trigger.dev path first (Phase-1 behavior unchanged).
  let run;
  try {
    run = await runs.retrieve<typeof runWorkflowTask>(runId);
  } catch {
    run = undefined;
  }

  let screenshotBase64: string | undefined;

  if (run) {
    if (run.payload?.orgId !== orgId) {
      return NextResponse.json({ error: "Forbidden" }, { status: 403 });
    }
    const artifact = await getRunArtifact(orgId, runId);
    screenshotBase64 = artifact?.screenshotBase64 ?? undefined;
  } else {
    // Evo path (M29): resolve ownership from the Phase-2 store.
    const evoOrgId = await getEvoRunOrgId(runId);
    if (!evoOrgId) {
      return NextResponse.json({ error: "Run not found." }, { status: 404 });
    }
    if (evoOrgId !== orgId) {
      return NextResponse.json({ error: "Forbidden" }, { status: 403 });
    }
    const artifact = await getEvoRunArtifact(orgId, runId);
    screenshotBase64 = artifact?.screenshotBase64 ?? undefined;
  }

  if (!screenshotBase64) {
    return NextResponse.json(
      { error: "No screenshot available for this run." },
      { status: 404 },
    );
  }

  return new Response(Buffer.from(screenshotBase64, "base64"), {
    headers: {
      "Content-Type": "image/jpeg",
      // The screenshot is immutable once the run ends — safe to cache.
      "Cache-Control": "private, max-age=3600, immutable",
    },
  });
}
