import * as Sentry from "@sentry/nextjs";
import { NextResponse } from "next/server";

import { readAuthWithRetry, resolveActiveOrgId } from "@/lib/auth";
import { getRunArtifact } from "@/features/workflows/data";
import { getEvoRunArtifact } from "@/features/workflows/lib/evo-run-data";
import { authorizeRunAccess } from "@/features/workflows/lib/run-authorization";

// Serves a run's final screenshot (base64 JPEG captured before the Browserbase
// session closed) for the results popup.
//
// Ownership is engine-aware and shared with every other per-run proxy route
// via authorizeRunAccess: legacy runs resolve from Trigger.dev, Evo runs from
// the Phase-2 run row. The artifact itself is then re-read org-scoped from the
// store that owns the run.
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

  // Confirm the run belongs to this org before touching any artifact data.
  const access = await authorizeRunAccess({ orgId, runId });
  if (!access.ok) {
    return access.status === 404
      ? NextResponse.json({ error: "Run not found." }, { status: 404 })
      : NextResponse.json({ error: "Forbidden" }, { status: 403 });
  }

  let screenshotBase64: string | undefined;
  if (access.engine === "legacy") {
    const artifact = await getRunArtifact(orgId, runId);
    screenshotBase64 = artifact?.screenshotBase64 ?? undefined;
  } else {
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
