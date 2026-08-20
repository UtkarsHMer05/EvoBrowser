// Phase 2 — server-side Evo run event reader (Milestone 28).
//
// The C++ distributed run loop publishes one JSON event per lifecycle
// transition to the Redis Stream `<envPrefix>:events` (XADD * payload <json>).
// This module reads that stream for ONE run:
//
//   readEvoRunEvents   — XRANGE replay from the start of the stream, filtered
//                        to one run id. Used on (re)connect so a client never
//                        loses run progress (M28 step 5).
//   tailEvoRunEvents   — blocking XREAD tail from a given stream id, delivering
//                        new events until the run reaches a terminal state,
//                        the caller aborts, or a timeout elapses.
//   durableRunSnapshot — fallback that reconstructs the view model from the
//                        durable Postgres state (workflow_runs + node_runs)
//                        when the event stream is unavailable or empty.
//
// Ownership/lifetime: the reader owns no mutable shared state; each call gets
// its own cursor. The Redis client is the shared lazily-created server client
// from lib/evo-redis (never exposed to the browser). Duplicate deliveries are
// harmless because reduceEvoEvents is idempotent per (kind, node_id).
//
// Timestamps: event wall_ms is wall-clock UTC milliseconds (C++ steady_clock is
// never published). Postgres timestamps are wall-clock UTC.

import { sql } from "drizzle-orm";
import type { Redis } from "ioredis";

import { getPhase2Db } from "@/lib/db/phase2";
import { nodeRuns, workflowRuns } from "@/lib/db/schema";

import {
  reduceEvoEvents,
  type EvoRunEvent,
  type NormalizedRunStatus,
  type NormalizedRunStep,
  type NormalizedRunViewModel,
  finalizeViewModel,
} from "./run-view-model";
import type { VersioningDb } from "./workflow-versions";

/** One event plus its Redis stream id (the tail cursor). */
export interface StreamedEvoEvent {
  streamId: string;
  event: EvoRunEvent;
}

/** Parse one stream entry ([id, [field, value, ...]]) into an event. */
export function parseStreamEntry(
  entry: [string, string[]],
): StreamedEvoEvent | undefined {
  const [streamId, fields] = entry;
  const idx = fields.findIndex((f) => f === "payload");
  if (idx < 0 || idx + 1 >= fields.length) return undefined;
  try {
    const event = JSON.parse(fields[idx + 1]) as EvoRunEvent;
    if (typeof event?.run_id !== "string" || typeof event?.kind !== "string") {
      return undefined; // malformed envelope — skip, never throw
    }
    return { streamId, event };
  } catch {
    return undefined;
  }
}

/**
 * Replay every event for `runId` from the start of the stream, in stream
 * order. Returns events for this run only, plus the last stream id seen (for
 * tailing). Events for other runs are skipped but still advance the cursor.
 */
export async function readEvoRunEvents(
  redis: Redis,
  streamKey: string,
  runId: string,
): Promise<{ events: StreamedEvoEvent[]; lastStreamId: string }> {
  const entries = (await redis.xrange(streamKey, "-", "+")) as [
    string,
    string[],
  ][];
  const events: StreamedEvoEvent[] = [];
  let lastStreamId = "0-0";
  for (const entry of entries) {
    lastStreamId = entry[0];
    const parsed = parseStreamEntry(entry);
    if (parsed && parsed.event.run_id === runId) events.push(parsed);
  }
  return { events, lastStreamId };
}

export interface TailOptions {
  /** Abort tailing (client disconnect / route teardown). */
  signal?: AbortSignal;
  /** Stop once the run reaches a terminal event. Default true. */
  stopOnTerminal?: boolean;
  /** Max ms to block per XREAD poll. */
  blockMs?: number;
  /** Overall deadline; the route enforces its own too. */
  timeoutMs?: number;
  /** Called after every poll (including empty ones) — keep-alive hook. */
  onIdle?: () => void;
}

/**
 * Tail the event stream from `afterStreamId`, invoking `onEvent` for each
 * event belonging to `runId`. Resolves when the run is terminal (default),
 * the signal aborts, or the timeout elapses. Never throws on Redis hiccups —
 * it returns and lets the route close the stream; the client reconnects and
 * replays (readEvoRunEvents), so no progress is lost.
 */
export async function tailEvoRunEvents(
  redis: Redis,
  streamKey: string,
  runId: string,
  afterStreamId: string,
  onEvent: (event: EvoRunEvent) => void,
  options: TailOptions = {},
): Promise<void> {
  const blockMs = options.blockMs ?? 1000;
  const stopOnTerminal = options.stopOnTerminal ?? true;
  const deadline =
    options.timeoutMs !== undefined ? Date.now() + options.timeoutMs : Infinity;
  let cursor = afterStreamId;

  while (!options.signal?.aborted && Date.now() < deadline) {
    let reply: [string, [string, string[]][]][] | null;
    try {
      reply = (await redis.xread(
        "BLOCK",
        blockMs,
        "STREAMS",
        streamKey,
        cursor,
      )) as [string, [string, string[]][]][] | null;
    } catch {
      return; // Redis hiccup — client reconnects and replays.
    }
    if (!reply) {
      options.onIdle?.();
      continue; // block timeout, poll again
    }

    for (const [, entries] of reply) {
      for (const entry of entries) {
        cursor = entry[0];
        const parsed = parseStreamEntry(entry);
        if (!parsed || parsed.event.run_id !== runId) continue;
        onEvent(parsed.event);
        if (stopOnTerminal && parsed.event.kind === "run_finished") return;
      }
    }
    options.onIdle?.();
  }
}

// ---------------------------------------------------------------------------
// Durable-state snapshot (M28 step 5 fallback).
// ---------------------------------------------------------------------------

/** Map a durable node_runs status to a normalized step status. */
function mapNodeStatus(status: string): NormalizedRunStep["status"] {
  switch (status) {
    case "succeeded":
      return "done";
    case "failed":
    case "dead_lettered":
      return "failed";
    case "canceled":
      return "canceled";
    case "running":
    case "dispatched":
    case "ready":
    // M32: a node parked in retry_wait is still in progress (waiting out its
    // backoff before a new attempt) — read it as running, not failed, so the
    // normal UI does not show a transient failure as terminal.
    case "retry_wait":
      return "running";
    default:
      return "pending"; // blocked / queued
  }
}

/** Map a durable workflow_runs status to a normalized run status. */
function mapRunStatus(status: string): NormalizedRunStatus {
  switch (status) {
    case "queued":
      return "queued";
    case "running":
      return "running";
    case "succeeded":
      return "succeeded";
    case "failed":
      return "failed";
    case "canceled":
      return "canceled";
    default:
      return "unknown";
  }
}

/**
 * Reconstruct the normalized view model from durable Postgres state. This is
 * the reconnect fallback when the event stream is empty/unavailable: the run
 * row plus its node rows already capture every terminal transition (the C++
 * loop persists terminal state BEFORE publishing events), so the snapshot is
 * always at least as fresh as the last durable write.
 */
export async function durableRunSnapshot(
  runId: string,
  db: VersioningDb = getPhase2Db(),
): Promise<NormalizedRunViewModel | undefined> {
  const [run] = await db
    .select()
    .from(workflowRuns)
    .where(sql`${workflowRuns.id} = ${runId}`);
  if (!run) return undefined;

  const nodes = await db
    .select()
    .from(nodeRuns)
    .where(sql`${nodeRuns.runId} = ${runId}`);

  const steps: NormalizedRunStep[] = nodes.map((n) => ({
    nodeId: n.nodeId,
    type: n.nodeType,
    title: n.nodeId,
    status: mapNodeStatus(n.status),
    output: n.output ?? undefined,
    error: n.failureReason ?? undefined,
    durationMs:
      n.startedAt && n.finishedAt
        ? n.finishedAt.getTime() - n.startedAt.getTime()
        : undefined,
  }));

  const durationMs =
    run.startedAt && run.finishedAt
      ? run.finishedAt.getTime() - run.startedAt.getTime()
      : undefined;

  return finalizeViewModel({
    id: run.id,
    engine: "evo",
    status: mapRunStatus(run.status),
    createdAt: run.createdAt,
    steps,
    // M29: the worker stamps the Browserbase session id on the run row as soon
    // as the session opens; the snapshot carries it so replay/live-view work
    // even when the event stream is unavailable.
    browserbaseSessionId: run.browserbaseSessionId ?? undefined,
    liveBrowserbaseSessionId: run.browserbaseSessionId ?? undefined,
    durationMs,
  });
}

/**
 * Full server-side fetch of a run's current view: replay the event stream and
 * fold it; if the stream has no events for this run, fall back to the durable
 * snapshot. Returns undefined only when neither source knows the run.
 */
export async function loadEvoRunView(
  redis: Redis,
  streamKey: string,
  runId: string,
  db?: VersioningDb,
): Promise<NormalizedRunViewModel | undefined> {
  const { events } = await readEvoRunEvents(redis, streamKey, runId);
  if (events.length > 0) {
    return reduceEvoEvents(
      runId,
      events.map((e) => e.event),
    );
  }
  return durableRunSnapshot(runId, db);
}
