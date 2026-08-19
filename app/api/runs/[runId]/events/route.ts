// Phase 2 — authorized SSE route for Evo run events (Milestone 28).
//
// GET /api/runs/[runId]/events
//
// Delivers one Evo run's lifecycle events to the browser as Server-Sent
// Events. This is the ONLY path from the Redis event stream to the browser —
// Redis credentials and the stream itself stay server-side (M28 no-go).
//
// Auth (M28 step 4): Clerk session + org ownership of the run row, enforced
// here before any event is read. The stream construction itself lives in
// buildEvoRunEventsResponse (features/workflows/lib/evo-run-events-route.ts)
// so its authorization predicate and reconnect behavior are unit-testable.
//
// Wire format:
//   event: run-event   data: <EvoRunEvent json>   id: <redis stream id>
//   event: snapshot    data: <NormalizedRunViewModel json>  (durable fallback)
//   : ping             (keep-alive comment every ~15s)

import { NextResponse } from "next/server";

import { readAuthWithRetry, resolveActiveOrgId } from "@/lib/auth";
import { buildEvoRunEventsResponse } from "@/features/workflows/lib/evo-run-events-route";

export const dynamic = "force-dynamic";

export async function GET(
  request: Request,
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

  return buildEvoRunEventsResponse({
    runId,
    orgId,
    lastEventId: request.headers.get("last-event-id") ?? undefined,
    signal: request.signal,
  });
}
