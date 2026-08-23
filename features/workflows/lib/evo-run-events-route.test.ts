// Unit tests for buildEvoRunEventsResponse — the Evo run-event SSE builder
// (features/workflows/lib/evo-run-events-route.ts).
//
// The builder takes injectable redis/db/streamKey dependencies, so this suite
// exercises real stream behavior with in-memory doubles:
//   404 contract  - unknown run / other org / legacy engine all deny alike
//   replay        - XRANGE frames filtered to one run, exclusive Last-Event-ID
//   terminal      - run_finished closes the stream without a snapshot
//   fallback      - empty stream serves the durable snapshot (terminal closes,
//                   non-terminal stays open until the client disconnects)
//
// Streams are consumed through a bounded reader that cancels afterwards: a
// non-terminal run tails indefinitely by design, so draining via res.text()
// would either hang or busy-loop against an instant fake redis.
//
// vitest-only; no local infra.

import { describe, expect, it, vi } from "vitest";

import { nodeRuns, workflowRuns } from "@/lib/db/schema";

import { buildEvoRunEventsResponse } from "./evo-run-events-route";

const ORG = "org_sse";
const RUN = "evo_11111111-2222-3333-4444-555555555555";

function eventOf(kind: string, extra: Record<string, unknown> = {}) {
  return { run_id: RUN, kind, wall_ms: 1_724_443_200_000, ...extra };
}

function entry(streamId: string, payload: unknown): [string, string[]] {
  return [streamId, ["payload", JSON.stringify(payload)]];
}

/** Drizzle-shaped double: each where() consumes the next queued result set. */
function fakeDb(queue: Array<{ table: unknown; rows: unknown[] }>) {
  let cursor = 0;
  return {
    select: () => ({
      from: (table: unknown) => ({
        where: async () => {
          const next = queue[cursor++];
          if (!next) return [];
          expect(next.table).toBe(table);
          return next.rows;
        },
      }),
    }),
  };
}

function evoRunRow(overrides: Record<string, unknown> = {}) {
  return {
    id: RUN,
    orgId: ORG,
    workflowId: "wf-uuid",
    engine: "evo",
    status: overrides.status ?? "succeeded",
    createdAt: new Date("2026-08-22T10:00:00Z"),
    startedAt: new Date("2026-08-22T10:00:01Z"),
    finishedAt: new Date("2026-08-22T10:00:09Z"),
    browserbaseSessionId: "sess_from_row",
    ...overrides,
  };
}

function nodeRow(nodeId: string, status: string) {
  return {
    runId: RUN,
    nodeId,
    nodeType: "act",
    status,
    output: null,
    failureReason: null,
    startedAt: new Date("2026-08-22T10:00:02Z"),
    finishedAt:
      status === "succeeded" ? new Date("2026-08-22T10:00:03Z") : null,
  };
}

function fakeRedis(entries: Array<[string, string[]]>) {
  return {
    xrange: vi.fn(async () => entries),
    // Realistic pacing: a blocking XREAD holds ~blockMs before returning
    // nothing. Without this the tail polls as fast as microtasks allow.
    xread: vi.fn(
      async () =>
        await new Promise<null>((resolve) => setTimeout(() => resolve(null), 50)),
    ),
  };
}

/**
 * Read exactly `count` SSE frames off the stream, then cancel. Cancelation
 * trips the builder's abort path, ending any open tail promptly.
 */
async function readFrames(res: Response, count: number): Promise<string[]> {
  const reader = res.body!.getReader();
  const decoder = new TextDecoder();
  const frames: string[] = [];
  try {
    let buffer = "";
    while (frames.length < count) {
      const { value, done } = await reader.read();
      if (done) break;
      buffer += decoder.decode(value, { stream: true });
      let sep = buffer.indexOf("\n\n");
      while (sep >= 0) {
        frames.push(buffer.slice(0, sep));
        buffer = buffer.slice(sep + 2);
        sep = buffer.indexOf("\n\n");
      }
    }
  } finally {
    await reader.cancel().catch(() => undefined);
  }
  return frames;
}

describe("buildEvoRunEventsResponse — authorization", () => {
  it.each([
    ["unknown run", []],
    ["run owned by another org", [evoRunRow({ orgId: "org_someone_else" })]],
    [
      "legacy run routed away from the SSE path",
      [evoRunRow({ engine: "legacy" })],
    ],
  ])("returns a uniform 404 for %s", async (_label, rows) => {
    const res = await buildEvoRunEventsResponse({
      runId: RUN,
      orgId: ORG,
      db: fakeDb(rows.map((r) => ({ table: workflowRuns, rows: [r] }))) as never,
    });

    expect(res.status).toBe(404);
    expect(await res.json()).toEqual({ error: "Run not found." });
  });
});

describe("buildEvoRunEventsResponse — SSE wire format", () => {
  it("replays only the requested run's events and closes on its terminal event", async () => {
    const redis = fakeRedis([
      entry("1-1", eventOf("node_started", { node_id: "n1" })),
      entry("1-2", { run_id: "evo_other", kind: "run_finished", wall_ms: 1 }),
      entry("1-3", eventOf("node_finished", { node_id: "n1" })),
      entry("1-4", eventOf("run_finished")),
    ]);

    const res = await buildEvoRunEventsResponse({
      runId: RUN,
      orgId: ORG,
      redis: redis as never,
      streamKey: "evo:test:events",
      db: fakeDb([{ table: workflowRuns, rows: [evoRunRow()] }]) as never,
    });

    expect(res.headers.get("Content-Type")).toBe("text/event-stream");

    const frames = await readFrames(res, 3);
    expect(frames).toHaveLength(3); // other run's event skipped entirely

    for (const frame of frames) {
      expect(frame).toMatch(/^event: run-event\nid: \d+-\d+\ndata: \{.*\}$/);
    }
    expect(frames.join("\n\n")).not.toContain("evo_other");

    const last = JSON.parse(
      frames[2]!.split("\n").find((l) => l.startsWith("data: "))!.slice(6),
    );
    expect(last.kind).toBe("run_finished");
  });

  it("resumes exclusively after Last-Event-ID using the (id form", async () => {
    const redis = fakeRedis([
      entry("9-9", eventOf("node_started", { node_id: "n2" })),
    ]);

    await readFrames(
      await buildEvoRunEventsResponse({
        runId: RUN,
        orgId: ORG,
        lastEventId: "8-1",
        redis: redis as never,
        streamKey: "evo:test:events",
        db: fakeDb([{ table: workflowRuns, rows: [evoRunRow()] }]) as never,
      }),
      1,
    );

    expect(redis.xrange).toHaveBeenCalledWith("evo:test:events", "(8-1", "+");
  });
});

describe("buildEvoRunEventsResponse — durable snapshot fallback", () => {
  it("serves a terminal durable snapshot when the stream has no events", async () => {
    const run = evoRunRow();
    const db = fakeDb([
      { table: workflowRuns, rows: [run] }, // authorization lookup
      { table: workflowRuns, rows: [run] }, // snapshot run row
      { table: nodeRuns, rows: [nodeRow("n1", "succeeded")] },
    ]);

    const res = await buildEvoRunEventsResponse({
      runId: RUN,
      orgId: ORG,
      redis: fakeRedis([]) as never,
      streamKey: "evo:test:events",
      db: db as never,
    });

    // Terminal snapshot closes the stream by itself.
    const body = await res.text();
    expect(body).toContain("event: snapshot\n");

    const snapshot = JSON.parse(
      body
        .split("\n\n")
        .find((f) => f.startsWith("event: snapshot"))!
        .split("\n")
        .find((l) => l.startsWith("data: "))!
        .slice(6),
    );
    expect(snapshot.id).toBe(RUN);
    expect(snapshot.engine).toBe("evo");
    expect(snapshot.status).toBe("succeeded");
    expect(snapshot.isTerminal).toBe(true);
    expect(snapshot.totalCount).toBe(1);
  });

  it("keeps a non-terminal run's stream open until the client disconnects", async () => {
    const run = evoRunRow({ status: "running", finishedAt: null });
    const db = fakeDb([
      { table: workflowRuns, rows: [run] },
      { table: workflowRuns, rows: [run] },
      { table: nodeRuns, rows: [nodeRow("n1", "running")] },
    ]);

    const controller = new AbortController();
    const res = await buildEvoRunEventsResponse({
      runId: RUN,
      orgId: ORG,
      signal: controller.signal,
      redis: fakeRedis([]) as never,
      streamKey: "evo:test:events",
      db: db as never,
    });

    // First chunk is the snapshot frame; the stream must still be open after it.
    const reader = res.body!.getReader();
    const decoder = new TextDecoder();
    const first = await reader.read();
    expect(first.done).toBe(false);
    expect(decoder.decode(first.value)).toContain("event: snapshot");

    // Client disconnect: abort trips the tail, then the stream closes cleanly.
    controller.abort();
    const next = await reader.read();
    expect(next.done).toBe(true);
    reader.releaseLock();
  });
});
