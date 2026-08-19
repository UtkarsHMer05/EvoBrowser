// Milestone 28: engine-neutral run view model + Evo realtime transport.
//
// Covers:
//   1. Normalized model: triggerRunToViewModel maps legacy Trigger.dev runs
//      (output steps, metadata fallback, session id, duration) — this is the
//      Phase-1 provider regression for the mapping the shared provider uses.
//   2. reduceEvoEvents: event ordering, duplicate-event idempotency, terminal
//      state, cross-run isolation, unknown-kind tolerance.
//   3. Authorized event route (buildEvoRunEventsResponse): 404 for unknown
//      runs, tenant mismatch, and legacy runs; SSE stream for an owned evo
//      run.
//   4. Reconnect simulation: replay resumes exclusively after Last-Event-ID;
//      durable Postgres snapshot fallback when the stream has no events.
//
// Sections 3–4 run against the local Phase-2 Redis + Postgres and skip
// cleanly when either is down, so the Phase-1 `npm test` gate stays green.

import assert from "node:assert/strict";
import { randomUUID } from "node:crypto";

import { drizzle } from "drizzle-orm/node-postgres";
import { Redis } from "ioredis";
import pg from "pg";

import * as schema from "@/lib/db/schema";

import {
  finalizeViewModel,
  isNormalizedLive,
  isNormalizedTerminal,
  mapLegacyStatus,
  reduceEvoEvents,
  triggerRunToViewModel,
  type EvoRunEvent,
  type LegacyRunLike,
} from "./run-view-model";
import { buildEvoRunEventsResponse } from "./evo-run-events-route";
import type { VersioningDb } from "./workflow-versions";

let passed = 0;
const ok = (label: string) => {
  passed++;
  console.log(`  ok   ${label}`);
};

function ev(
  runId: string,
  kind: string,
  nodeId: string,
  wallMs: number,
  detail = "",
): EvoRunEvent {
  return { run_id: runId, node_id: nodeId, kind, detail, wall_ms: wallMs };
}

// --- 1. Normalized model: legacy Trigger adapter ---------------------------
{
  const createdAt = new Date("2026-08-19T10:00:00Z");
  const run: LegacyRunLike = {
    id: "trig_run_1",
    status: "COMPLETED",
    createdAt,
    output: {
      steps: [
        {
          nodeId: "n1",
          type: "open-url",
          title: "Open",
          status: "done",
          durationMs: 1200,
          output: { url: "https://example.com" },
        },
        {
          nodeId: "n2",
          type: "act",
          title: "Click",
          status: "done",
          durationMs: 300,
        },
      ],
      browserbaseSessionId: "bb_sess_1",
      finalUrl: "https://example.com/after",
      durationMs: 1500,
    },
  };

  const vm = triggerRunToViewModel(run);
  assert.equal(vm.id, "trig_run_1");
  assert.equal(vm.engine, "legacy");
  assert.equal(vm.status, "succeeded");
  assert.equal(vm.isTerminal, true);
  assert.equal(vm.isLive, false);
  assert.equal(vm.steps.length, 2);
  assert.equal(vm.steps[0].title, "Open");
  assert.equal(vm.browserbaseSessionId, "bb_sess_1");
  assert.equal(vm.finalUrl, "https://example.com/after");
  assert.equal(vm.durationMs, 1500);
  assert.equal(vm.completedCount, 2);
  assert.equal(vm.failedCount, 0);
  assert.equal(vm.totalCount, 2);
  assert.deepEqual(vm.createdAt, createdAt);
  ok("legacy completed run maps to normalized model (output steps)");

  // In-flight run: metadata steps fallback + live session id.
  const live: LegacyRunLike = {
    id: "trig_run_2",
    status: "EXECUTING",
    createdAt,
    metadata: {
      steps: [
        { nodeId: "n1", type: "open-url", title: "Open", status: "done" },
        { nodeId: "n2", type: "act", title: "Click", status: "running" },
      ],
      browserbaseSessionId: "bb_live",
    },
  };
  const liveVm = triggerRunToViewModel(live);
  assert.equal(liveVm.status, "running");
  assert.equal(liveVm.isLive, true);
  assert.equal(liveVm.isTerminal, false);
  assert.equal(liveVm.steps.length, 2);
  assert.equal(liveVm.liveBrowserbaseSessionId, "bb_live");
  assert.equal(liveVm.browserbaseSessionId, undefined);
  ok("legacy in-flight run maps metadata steps + live session id");

  // Status mapping matrix.
  assert.equal(mapLegacyStatus("QUEUED"), "queued");
  assert.equal(mapLegacyStatus("EXECUTING"), "running");
  assert.equal(mapLegacyStatus("COMPLETED"), "succeeded");
  assert.equal(mapLegacyStatus("FAILED"), "failed");
  assert.equal(mapLegacyStatus("CANCELED"), "canceled");
  assert.equal(mapLegacyStatus("CRASHED"), "failed");
  assert.equal(mapLegacyStatus("SOMETHING_NEW"), "unknown");
  ok("mapLegacyStatus matrix");

  assert.equal(isNormalizedLive("queued"), true);
  assert.equal(isNormalizedLive("running"), true);
  assert.equal(isNormalizedLive("succeeded"), false);
  assert.equal(isNormalizedTerminal("failed"), true);
  assert.equal(isNormalizedTerminal("running"), false);
  ok("normalized live/terminal predicates");

  // finalizeViewModel derives stats.
  const fin = finalizeViewModel({
    id: "x",
    engine: "evo",
    status: "failed",
    steps: [
      { nodeId: "a", type: "t", title: "a", status: "done" },
      { nodeId: "b", type: "t", title: "b", status: "failed" },
    ],
  });
  assert.equal(fin.completedCount, 1);
  assert.equal(fin.failedCount, 1);
  assert.equal(fin.totalCount, 2);
  assert.equal(fin.isTerminal, true);
  ok("finalizeViewModel derives stats");
}

// --- 2. reduceEvoEvents: ordering, duplicates, terminal, isolation ---------
{
  const runId = "evo_run_A";
  const events: EvoRunEvent[] = [
    ev(runId, "run_started", "", 1000),
    ev(runId, "node_dispatched", "n1", 1100, "open-url"),
    ev(runId, "node_succeeded", "n1", 1600, '{"ok":true}'),
    ev(runId, "node_dispatched", "n2", 1700, "click"),
    ev(runId, "node_succeeded", "n2", 1900, '{"clicked":true}'),
    ev(runId, "run_finished", "", 2000, "succeeded"),
  ];

  const vm = reduceEvoEvents(runId, events);
  assert.equal(vm.status, "succeeded");
  assert.equal(vm.isTerminal, true);
  assert.equal(vm.steps.length, 2);
  assert.equal(vm.steps[0].type, "open-url"); // type from dispatch detail
  assert.equal(vm.steps[0].status, "done");
  assert.deepEqual(vm.steps[0].output, { ok: true }); // output from detail
  assert.equal(vm.steps[0].durationMs, 500);
  assert.equal(vm.durationMs, 1000); // finished - started
  assert.equal(vm.completedCount, 2);
  assert.equal(vm.createdAt?.getTime(), 1000);
  ok("ordered event fold produces terminal view with durations");

  // Duplicate deliveries are idempotent.
  const dupVm = reduceEvoEvents(runId, [...events, ...events]);
  assert.deepEqual(dupVm, vm);
  ok("duplicate events are idempotent");

  // Cross-run isolation: another run's events are ignored.
  const mixed = [
    ...events,
    ev("evo_run_B", "run_started", "", 500),
    ev("evo_run_B", "node_failed", "x", 600, "boom"),
  ];
  const isoVm = reduceEvoEvents(runId, mixed);
  assert.deepEqual(isoVm, vm);
  ok("events from other runs are ignored");

  // Unknown kinds are tolerated (forward compatibility).
  const unknownVm = reduceEvoEvents(runId, [
    ...events,
    ev(runId, "some_future_kind", "n9", 3000, "whatever"),
  ]);
  assert.equal(unknownVm.status, "succeeded");
  assert.equal(unknownVm.steps.length, 2);
  ok("unknown event kinds are ignored");

  // Failure path: node_failed then run_finished(failed).
  const failVm = reduceEvoEvents("evo_run_F", [
    ev("evo_run_F", "run_started", "", 100),
    ev("evo_run_F", "node_dispatched", "n1", 200, "open-url"),
    ev("evo_run_F", "node_failed", "n1", 300, "timeout"),
    ev("evo_run_F", "run_finished", "", 400, "failed"),
  ]);
  assert.equal(failVm.status, "failed");
  assert.equal(failVm.steps[0].status, "failed");
  assert.equal(failVm.steps[0].error, "timeout");
  assert.equal(failVm.failedCount, 1);
  ok("failure path folds to failed view");

  // Cancel path: canceled node + run_finished(canceled).
  const cancelVm = reduceEvoEvents("evo_run_C", [
    ev("evo_run_C", "run_started", "", 100),
    ev("evo_run_C", "node_dispatched", "n1", 200, "open-url"),
    ev("evo_run_C", "node_canceled", "n1", 300),
    ev("evo_run_C", "run_finished", "", 400, "canceled"),
  ]);
  assert.equal(cancelVm.status, "canceled");
  assert.equal(cancelVm.steps[0].status, "canceled");
  ok("cancel path folds to canceled view");

  // A canceled event must not overwrite a completed node.
  const raceVm = reduceEvoEvents("evo_run_R", [
    ev("evo_run_R", "run_started", "", 100),
    ev("evo_run_R", "node_dispatched", "n1", 200, "open-url"),
    ev("evo_run_R", "node_succeeded", "n1", 300, "{}"),
    ev("evo_run_R", "node_canceled", "n1", 400),
    ev("evo_run_R", "run_finished", "", 500, "canceled"),
  ]);
  assert.equal(raceVm.steps[0].status, "done");
  ok("cancel does not overwrite a completed node");

  // Empty event list -> queued, no steps.
  const emptyVm = reduceEvoEvents("evo_run_E", []);
  assert.equal(emptyVm.status, "queued");
  assert.equal(emptyVm.isLive, true);
  assert.equal(emptyVm.steps.length, 0);
  ok("empty event list yields queued view");
}

// --- 3+4. Authorized route + reconnect (local Redis + Postgres) ------------
{
  const redisHost = process.env.EVO_PHASE2_REDIS_HOST ?? "127.0.0.1";
  const redisPort = Number(process.env.EVO_PHASE2_REDIS_PORT ?? 6390);
  const pgUser = process.env.EVO_PHASE2_PG_USER ?? "evo";
  const pgPassword = process.env.EVO_PHASE2_PG_PASSWORD ?? "evo_dev_password";
  const pgPort = process.env.EVO_PHASE2_PG_PORT ?? "5433";
  const pgDb = process.env.EVO_PHASE2_PG_DB ?? "evo_phase2";

  const redis = new Redis({
    host: redisHost,
    port: redisPort,
    connectTimeout: 2000,
    maxRetriesPerRequest: 1,
    lazyConnect: true,
    enableOfflineQueue: false,
  });
  const pool = new pg.Pool({
    connectionString: `postgresql://${pgUser}:${pgPassword}@127.0.0.1:${pgPort}/${pgDb}`,
    connectionTimeoutMillis: 3000,
  });

  let redisUp = true;
  let pgUp = true;
  try {
    await redis.connect();
    await redis.ping();
  } catch {
    redisUp = false;
  }
  try {
    const client = await pool.connect();
    client.release();
  } catch {
    pgUp = false;
  }

  if (!redisUp || !pgUp) {
    console.log(
      "SKIP: M28 transport section (needs local Phase-2 Redis at " +
        `${redisHost}:${redisPort} and Postgres at 127.0.0.1:${pgPort}; ` +
        "run scripts/phase2/up.sh + migrate-local.sh)",
    );
    redis.disconnect();
    await pool.end();
  } else {
    const db = drizzle(pool, { schema, casing: "snake_case" }) as unknown as VersioningDb;
    const orgId = `org_m28_${randomUUID().slice(0, 8)}`;
    const otherOrgId = `org_m28_other_${randomUUID().slice(0, 8)}`;
    const workflowId = randomUUID();
    const streamKey = `evo:m28test:${randomUUID().slice(0, 8)}:events`;

    await db.insert(schema.workflows).values({
      id: workflowId,
      orgId,
      name: "m28 test workflow",
    });

    const seedRun = async (runId: string, engine: string, runOrgId: string) => {
      await db.insert(schema.workflowRuns).values({
        id: runId,
        orgId: runOrgId,
        workflowId,
        engine,
        status: "queued",
      });
    };

    const publish = async (event: EvoRunEvent): Promise<string> => {
      const id = await redis.xadd(streamKey, "*", "payload", JSON.stringify(event));
      return id as string;
    };

    // Parse an SSE body into frames.
    const parseSse = (text: string) => {
      const frames: { event?: string; id?: string; data?: string }[] = [];
      for (const block of text.split("\n\n")) {
        if (!block.trim()) continue;
        const frame: { event?: string; id?: string; data?: string } = {};
        for (const line of block.split("\n")) {
          if (line.startsWith("event: ")) frame.event = line.slice(7);
          else if (line.startsWith("id: ")) frame.id = line.slice(4);
          else if (line.startsWith("data: ")) frame.data = line.slice(6);
        }
        frames.push(frame);
      }
      return frames;
    };

    const readAll = async (res: Response, timeoutMs = 10_000): Promise<string> => {
      const reader = res.body!.getReader();
      const decoder = new TextDecoder();
      let out = "";
      const deadline = Date.now() + timeoutMs;
      while (Date.now() < deadline) {
        const { done, value } = await Promise.race([
          reader.read(),
          new Promise<never>((_, rej) =>
            setTimeout(() => rej(new Error("SSE read timeout")), deadline - Date.now()),
          ),
        ]);
        if (done) return out;
        out += decoder.decode(value, { stream: true });
      }
      throw new Error("SSE read timeout");
    };

    try {
      // --- Authorization: unknown run, tenant mismatch, legacy run -> 404 ---
      const evoRunId = `evo_${randomUUID()}`;
      await seedRun(evoRunId, "evo", orgId);

      const unknown = await buildEvoRunEventsResponse({
        runId: `evo_${randomUUID()}`,
        orgId,
        redis,
        streamKey,
        db,
      });
      assert.equal(unknown.status, 404);
      ok("unknown run -> 404");

      const tenant = await buildEvoRunEventsResponse({
        runId: evoRunId,
        orgId: otherOrgId,
        redis,
        streamKey,
        db,
      });
      assert.equal(tenant.status, 404);
      ok("tenant mismatch -> 404 (existence not leaked)");

      const legacyRunId = `trig_${randomUUID()}`;
      await seedRun(legacyRunId, "legacy", orgId);
      const legacy = await buildEvoRunEventsResponse({
        runId: legacyRunId,
        orgId,
        redis,
        streamKey,
        db,
      });
      assert.equal(legacy.status, 404);
      ok("legacy run -> 404 (stays on Trigger provider)");

      // --- Terminal run: full replay then close ----------------------------
      const runA = `evo_${randomUUID()}`;
      await seedRun(runA, "evo", orgId);
      const idA1 = await publish(ev(runA, "run_started", "", 1000));
      await publish(ev(runA, "node_dispatched", "n1", 1100, "open-url"));
      await publish(ev(runA, "node_succeeded", "n1", 1500, '{"ok":true}'));
      await publish(ev(runA, "run_finished", "", 1600, "succeeded"));
      // Noise from another run in the same stream must be filtered out.
      await publish(ev(`evo_${randomUUID()}`, "run_started", "", 1700));

      const resA = await buildEvoRunEventsResponse({
        runId: runA,
        orgId,
        redis,
        streamKey,
        db,
      });
      assert.equal(resA.status, 200);
      assert.equal(resA.headers.get("content-type"), "text/event-stream");
      const bodyA = await readAll(resA);
      const framesA = parseSse(bodyA).filter((f) => f.event === "run-event");
      assert.equal(framesA.length, 4);
      const kindsA = framesA.map((f) => JSON.parse(f.data!).kind);
      assert.deepEqual(kindsA, [
        "run_started",
        "node_dispatched",
        "node_succeeded",
        "run_finished",
      ]);
      assert.equal(framesA[0].id, idA1); // stream id carried as SSE id
      ok("owned evo run replays its events in order and closes at terminal");

      // --- Reconnect: resume exclusively after Last-Event-ID --------------
      const idA2 = framesA[1].id!;
      const resB = await buildEvoRunEventsResponse({
        runId: runA,
        orgId,
        lastEventId: idA2,
        redis,
        streamKey,
        db,
      });
      const bodyB = await readAll(resB);
      const framesB = parseSse(bodyB).filter((f) => f.event === "run-event");
      assert.equal(framesB.length, 2); // node_succeeded + run_finished only
      assert.deepEqual(
        framesB.map((f) => JSON.parse(f.data!).kind),
        ["node_succeeded", "run_finished"],
      );
      ok("reconnect resumes exclusively after Last-Event-ID");

      // --- Durable snapshot fallback when the stream has no events --------
      const runC = `evo_${randomUUID()}`;
      await db.insert(schema.workflowRuns).values({
        id: runC,
        orgId,
        workflowId,
        engine: "evo",
        status: "succeeded",
        startedAt: new Date("2026-08-19T10:00:00Z"),
        finishedAt: new Date("2026-08-19T10:00:02Z"),
      });
      await db.insert(schema.nodeRuns).values({
        runId: runC,
        nodeId: "n1",
        nodeType: "open-url",
        status: "succeeded",
        output: { ok: true },
        startedAt: new Date("2026-08-19T10:00:00Z"),
        finishedAt: new Date("2026-08-19T10:00:01Z"),
      });

      const resC = await buildEvoRunEventsResponse({
        runId: runC,
        orgId,
        redis,
        streamKey,
        db,
      });
      const bodyC = await readAll(resC);
      const framesC = parseSse(bodyC);
      const snapshotFrame = framesC.find((f) => f.event === "snapshot");
      assert.ok(snapshotFrame, "snapshot frame present");
      const snapshot = JSON.parse(snapshotFrame!.data!);
      assert.equal(snapshot.id, runC);
      assert.equal(snapshot.status, "succeeded");
      assert.equal(snapshot.isTerminal, true);
      assert.equal(snapshot.steps.length, 1);
      assert.equal(snapshot.steps[0].status, "done");
      assert.equal(snapshot.durationMs, 2000);
      ok("durable snapshot served when stream has no events (reconnect fallback)");

      // --- Live tail: connect on a running run, then publish terminal -----
      const runD = `evo_${randomUUID()}`;
      await seedRun(runD, "evo", orgId);
      await publish(ev(runD, "run_started", "", 100));
      await publish(ev(runD, "node_dispatched", "n1", 200, "bench:sleep"));

      const resD = await buildEvoRunEventsResponse({
        runId: runD,
        orgId,
        redis,
        streamKey,
        db,
      });
      // Give the replay a moment, then publish the remaining events live.
      await new Promise((r) => setTimeout(r, 300));
      await publish(ev(runD, "node_succeeded", "n1", 700, '{"slept":true}'));
      await publish(ev(runD, "run_finished", "", 800, "succeeded"));

      const bodyD = await readAll(resD);
      const framesD = parseSse(bodyD).filter((f) => f.event === "run-event");
      assert.equal(framesD.length, 4);
      assert.equal(JSON.parse(framesD[3].data!).kind, "run_finished");
      ok("live tail delivers late events and closes at terminal");
    } finally {
      await redis.del(streamKey);
      redis.disconnect();
      await pool.end();
    }
  }
}

console.log(`\nM28 run-view-model + transport tests: ${passed} passed`);
