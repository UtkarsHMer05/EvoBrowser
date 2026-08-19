// Phase 2 — Evo run events SSE response builder (Milestone 28).
//
// The authorized-route core, split out of the route handler so the
// authorization predicate (org ownership + engine check) and the SSE stream
// behavior are testable without a Clerk session. The route handler
// (app/api/runs/[runId]/events) resolves the Clerk user + org and delegates
// here.
//
// Authorization: the run row must exist, belong to the caller's org, and be
// an Evo run. Unknown runs, tenant mismatches, and legacy runs all return the
// same 404 so cross-tenant existence is not leaked; legacy runs stay on the
// Trigger realtime provider (Phase-1 preservation).
//
// Reconnect: replay from the start of the stream, or exclusively after
// Last-Event-ID when a client reconnects; fall back to the durable Postgres
// snapshot when the stream has no events for the run.

import { NextResponse } from "next/server";
import { sql } from "drizzle-orm";
import type { Redis } from "ioredis";

import { getPhase2Db } from "@/lib/db/phase2";
import { workflowRuns } from "@/lib/db/schema";
import {
  durableRunSnapshot,
  parseStreamEntry,
  tailEvoRunEvents,
} from "@/features/workflows/lib/evo-run-events";
import type { EvoRunEvent } from "@/features/workflows/lib/run-view-model";
import {
  eventStreamKey,
  getEvoEnvPrefix,
  getEvoRedis,
} from "@/lib/evo-redis";

import type { VersioningDb } from "./workflow-versions";

const KEEPALIVE_MS = 15_000;
// Hard cap so a stuck terminal event can't pin a connection forever; the
// client reconnects and gets the durable snapshot.
const MAX_STREAM_MS = 10 * 60_000;

export interface BuildEvoRunEventsResponseArgs {
  runId: string;
  /** The authenticated caller's org (already resolved by the route). */
  orgId: string;
  /** Last-Event-ID from a reconnecting EventSource, if any. */
  lastEventId?: string;
  /** The incoming request's signal — aborts the tail on client disconnect. */
  signal?: AbortSignal;
  /** Injectable for tests. */
  redis?: Redis;
  streamKey?: string;
  db?: VersioningDb;
}

function sseFrame(event: string, data: unknown, id?: string): string {
  let frame = `event: ${event}\n`;
  if (id) frame += `id: ${id}\n`;
  frame += `data: ${JSON.stringify(data)}\n\n`;
  return frame;
}

export async function buildEvoRunEventsResponse({
  runId,
  orgId,
  lastEventId,
  signal,
  redis = getEvoRedis(),
  streamKey = eventStreamKey(getEvoEnvPrefix()),
  db = getPhase2Db(),
}: BuildEvoRunEventsResponseArgs): Promise<Response> {
  // Ownership + engine check against the durable run row.
  const [run] = await db
    .select()
    .from(workflowRuns)
    .where(sql`${workflowRuns.id} = ${runId}`);

  if (!run || run.orgId !== orgId || run.engine !== "evo") {
    return NextResponse.json({ error: "Run not found." }, { status: 404 });
  }

  const encoder = new TextEncoder();
  const abort = new AbortController();
  // Client disconnect ends the tail.
  signal?.addEventListener("abort", () => abort.abort(), { once: true });

  const stream = new ReadableStream({
    async start(controller) {
      let closed = false;
      const enqueue = (chunk: string) => {
        if (!closed) controller.enqueue(encoder.encode(chunk));
      };
      const close = () => {
        if (closed) return;
        closed = true;
        try {
          controller.close();
        } catch {
          // already closed
        }
      };

      try {
        // 1) Replay. Resume after Last-Event-ID when the client reconnects;
        //    otherwise replay the run's whole history.
        const replayFrom = lastEventId ? `(${lastEventId}` : "-";
        const { events, lastStreamId } = await replayRunEvents(
          redis,
          streamKey,
          runId,
          replayFrom,
        );

        let sawTerminal = false;
        for (const { streamId, event } of events) {
          enqueue(sseFrame("run-event", event, streamId));
          if (event.kind === "run_finished") sawTerminal = true;
        }

        // 2) No events in the stream for this run -> serve the durable
        //    snapshot so a late client still sees current state.
        if (events.length === 0) {
          const snapshot = await durableRunSnapshot(runId, db);
          if (snapshot) enqueue(sseFrame("snapshot", snapshot));
          if (snapshot?.isTerminal) {
            close();
            return;
          }
        }

        if (sawTerminal) {
          close();
          return;
        }

        // 3) Tail until terminal / disconnect / cap.
        let lastPing = Date.now();
        await tailEvoRunEvents(
          redis,
          streamKey,
          runId,
          lastStreamId,
          (event: EvoRunEvent) => {
            enqueue(sseFrame("run-event", event));
            if (event.kind === "run_finished") abort.abort();
          },
          {
            signal: abort.signal,
            timeoutMs: MAX_STREAM_MS,
            blockMs: 500,
            onIdle: () => {
              const now = Date.now();
              if (now - lastPing >= KEEPALIVE_MS) {
                lastPing = now;
                enqueue(": ping\n\n");
              }
            },
          },
        );
      } catch {
        // Any failure ends the stream; the client reconnects and replays.
      } finally {
        close();
      }
    },
    cancel() {
      abort.abort();
    },
  });

  return new Response(stream, {
    headers: {
      "Content-Type": "text/event-stream",
      "Cache-Control": "no-cache, no-transform",
      Connection: "keep-alive",
      "X-Accel-Buffering": "no",
    },
  });
}

// XRANGE from `start` (supports the exclusive "(id" form), filtered to one
// run. Split out so the route and the tests share one implementation.
async function replayRunEvents(
  redis: Redis,
  streamKey: string,
  runId: string,
  start: string,
) {
  const entries = (await redis.xrange(streamKey, start, "+")) as [
    string,
    string[],
  ][];
  const events: { streamId: string; event: EvoRunEvent }[] = [];
  let lastStreamId = start.startsWith("(") ? start.slice(1) : "0-0";
  for (const entry of entries) {
    lastStreamId = entry[0];
    const parsed = parseStreamEntry(entry);
    if (parsed && parsed.event.run_id === runId) events.push(parsed);
  }
  return { events, lastStreamId };
}
