// Phase 2 — distributed worker entry point (Milestone 23, browser wiring M25,
// real-executor wiring M29).
//
// Runs one worker process against the local Phase-2 Redis. Separate from
// Next.js and Trigger.dev. Uses a COMPOSITE executor:
//   - synthetic "bench:*" types -> syntheticExecutor (M23; keeps the M26 E2E
//     and benchmarks working),
//   - product node types (open-url/act/extract/observe/agent/send-email) ->
//     the M24 node-executor-adapter with durable Postgres loaders (M29), so
//     real workflows execute on workers and open Browserbase sessions.
//
// Configuration (env vars; no secrets required for the local stack):
//   EVO_PHASE2_REDIS_HOST   default 127.0.0.1
//   EVO_PHASE2_REDIS_PORT   default 6390
//   EVO_PHASE2_REDIS_PASSWORD  optional; never logged
//   EVO_WORKER_ENV_PREFIX   default "evo:dev"
//   EVO_WORKER_GROUP        default "workers"
//   EVO_WORKER_ID           optional stable identity (generated if unset)
//
// Browser sessions (M25): browser nodes get their Stagehand session from the
// worker-local BrowserSessionManager — one session per run affinity key,
// reused by every browser task of that run, closed on run end and on graceful
// shutdown. BROWSERBASE_API_KEY is read from this process's environment only;
// it never travels in a task envelope. If the key is absent, browser tasks
// fail with a clear error instead of crashing the worker.
//
// Live-view parity (M29): as soon as a session opens, the worker publishes a
// `browser_session_opened` run event (with the Browserbase session id) to the
// event stream so the UI's live-view panel can attach. The final screenshot
// is persisted to run_artifacts (Phase-2 Postgres) before close, so the
// results dialog can show it.
//
// Graceful shutdown: SIGTERM/SIGINT stop claiming and drain in-flight tasks
// (default 10s) before exiting, then close all live browser sessions.
// Unfinished tasks stay pending for redelivery.

import { Stagehand } from "@browserbasehq/stagehand";
import { and, eq, isNull } from "drizzle-orm";

import { getPhase2Db } from "@/lib/db/phase2";
import { runArtifacts, workflowRuns } from "@/lib/db/schema";
import { isEvoLiveViewConnected } from "@/features/workflows/lib/evo-run-data";

import {
  BrowserSessionManager,
  captureFinalScreenshot,
} from "./browser-session-manager";
import {
  loadPredecessorOutputs,
  loadVersion,
} from "./durable-loaders";
import { PgTaskLeaseStore } from "./lease-store";
import { createNodeExecutorAdapter } from "./node-executor-adapter";
import { RedisStreamsClient, eventStreamKey } from "./redis-streams";
import { syntheticExecutor } from "./synthetic-executor";
import { Worker, type TaskExecutor } from "./worker";
import type { TaskEnvelopeView } from "./envelope-codec";

const host = process.env.EVO_PHASE2_REDIS_HOST ?? "127.0.0.1";
const port = Number(process.env.EVO_PHASE2_REDIS_PORT ?? 6390);
const password = process.env.EVO_PHASE2_REDIS_PASSWORD || undefined;
const envPrefix = process.env.EVO_WORKER_ENV_PREFIX ?? "evo:dev";
const group = process.env.EVO_WORKER_GROUP ?? "workers";
const workerId = process.env.EVO_WORKER_ID || undefined;

// Milestone 34: lease/heartbeat cadence is env-configurable so the
// crash-recovery failure-injection test can use short leases (fast reap) while
// production keeps the generous defaults. The defaults match Worker's own
// defaults (worker.ts); setting these does NOT change production behavior.
const leaseDurationMs = process.env.EVO_WORKER_LEASE_DURATION_MS
  ? Number(process.env.EVO_WORKER_LEASE_DURATION_MS)
  : undefined;
const leaseRenewIntervalMs = process.env.EVO_WORKER_LEASE_RENEW_INTERVAL_MS
  ? Number(process.env.EVO_WORKER_LEASE_RENEW_INTERVAL_MS)
  : undefined;
const heartbeatIntervalMs = process.env.EVO_WORKER_HEARTBEAT_INTERVAL_MS
  ? Number(process.env.EVO_WORKER_HEARTBEAT_INTERVAL_MS)
  : undefined;

// Live-view handshake timing (M29 parity with Phase-1 run-workflow.ts): hold
// the first browser step until the watching browser's live view connects, up to
// this deadline, then proceed anyway so an unwatched run never hangs.
const LIVE_VIEW_WAIT_MS = 60_000;
const LIVE_VIEW_POLL_MS = 1_000;

// A dedicated publisher for run events (browser_session_opened). Separate from
// the worker's task/result client so event publishing never contends with the
// claim loop.
const eventPublisher = new RedisStreamsClient({ host, port, password });

async function publishRunEvent(
  runId: string,
  kind: string,
  detail: string,
): Promise<void> {
  const event = {
    run_id: runId,
    node_id: "",
    kind,
    detail,
    wall_ms: Date.now(),
  };
  try {
    await eventPublisher.publish(
      eventStreamKey(envPrefix),
      Buffer.from(JSON.stringify(event)),
    );
  } catch (err) {
    // Event publishing is best-effort (M26: the durable store is
    // authoritative). Never fail the run over a missed UI event.
    console.log(`[worker] event publish failed (${kind}): ${String(err)}`);
  }
}

// M25: worker-local browser session ownership. The factory mirrors the legacy
// run-workflow.ts Stagehand construction (Browserbase env, Model Gateway
// routing, pino disabled for bundled/minimal environments).
const browserSessions = new BrowserSessionManager({
  factory: async (affinityKey) => {
    const apiKey = process.env.BROWSERBASE_API_KEY;
    if (!apiKey) {
      throw new Error(
        `BROWSERBASE_API_KEY is not set on this worker; cannot open a browser session for ${affinityKey}`,
      );
    }
    const stagehand = new Stagehand({
      env: "BROWSERBASE",
      apiKey,
      model: "google/gemini-2.5-flash",
      disablePino: true,
    });
    await stagehand.init();
    return {
      stagehand,
      browserbaseSessionId: stagehand.browserbaseSessionID,
    };
  },
  // M29 live-view parity: publish the session id as soon as it exists so the
  // UI can attach the live view. The id rides in `detail` (the reducer reads
  // detail or the rich field). Also persist it durably on the run row so
  // replay/screenshot/live-view can resolve the session from the durable
  // store even after the event stream is gone.
  onSessionOpened: (info) => {
    console.log(
      `[worker] browser session opened run=${info.runId} key=${info.affinityKey} browserbaseSessionId=${info.browserbaseSessionId ?? "unknown"}`,
    );
    if (info.browserbaseSessionId) {
      void saveRunBrowserbaseSession(info.runId, info.browserbaseSessionId);
      void publishRunEvent(
        info.runId,
        "browser_session_opened",
        info.browserbaseSessionId,
      );
    }
  },
  // M29 results parity: persist the final screenshot to run_artifacts so the
  // results dialog can show it (org-checked route reads it).
  onFinalScreenshot: async (info) => {
    await saveRunScreenshot(info.runId, info.screenshotBase64);
  },
  // M29 live-view parity: hold the first browser step until the watching
  // browser's live view connects (Phase-1 run-workflow.ts behavior). The
  // handshake row is written to the Phase-2 store by the connected route.
  // Fail-open: an unwatched run must never hang.
  isLiveViewConnected: isEvoLiveViewConnected,
  liveViewWaitMs: LIVE_VIEW_WAIT_MS,
  liveViewPollMs: LIVE_VIEW_POLL_MS,
  log: (msg) => console.log(msg),
});

// Persist a run's Browserbase session id on the run row (M29). The durable
// store is the authoritative source for replay/screenshot/live-view resolution;
// the event stream is best-effort. Only updates rows that don't already have a
// session id (first session wins; a run opens at most one session per affinity
// key, so this is effectively write-once). Best-effort: a persistence failure
// never fails the run.
async function saveRunBrowserbaseSession(
  runId: string,
  browserbaseSessionId: string,
): Promise<void> {
  try {
    const db = getPhase2Db();
    await db
      .update(workflowRuns)
      .set({ browserbaseSessionId })
      .where(
        and(
          eq(workflowRuns.id, runId),
          isNull(workflowRuns.browserbaseSessionId),
        ),
      );
  } catch (err) {
    console.log(
      `[worker] browserbase session id save failed: ${String(err)}`,
    );
  }
}

// Persist a run's screenshot to run_artifacts (upsert; last capture wins).
// Reads the org from the run row since the callback does not carry it.
async function saveRunScreenshot(
  runId: string,
  screenshotBase64: string,
): Promise<void> {
  try {
    const db = getPhase2Db();
    const [run] = await db
      .select({ orgId: workflowRuns.orgId })
      .from(workflowRuns)
      .where(eq(workflowRuns.id, runId));
    if (!run) return;
    await db
      .insert(runArtifacts)
      .values({ runId, orgId: run.orgId, screenshotBase64 })
      .onConflictDoUpdate({
        target: runArtifacts.runId,
        set: { screenshotBase64 },
      });
    console.log(`[worker] screenshot saved for run=${runId}`);
  } catch (err) {
    console.log(`[worker] screenshot save failed: ${String(err)}`);
  }
}

// M29: real product-node executor with durable loaders + browser sessions.
const productExecutor = createNodeExecutorAdapter({
  loadVersion,
  loadPredecessorOutputs,
  getStagehand: (task: TaskEnvelopeView) => browserSessions.getForTask(task),
});

// Composite executor: synthetic bench types keep the M26 E2E + benchmarks
// working; everything else routes to the real product-node adapter. Browser
// sessions stay open for the run's lifetime (affinity reuse) and are closed
// on graceful shutdown; screenshots are captured per browser task below.
const compositeExecutor: TaskExecutor = async (task, signal) => {
  if (task.nodeType === "start" || task.nodeType.startsWith("bench:")) {
    return syntheticExecutor(task, signal);
  }
  const result = await productExecutor(task, signal);
  // Capture a screenshot after each browser task so the results dialog shows
  // the latest page state even before the run ends (upsert; last wins).
  if (result.completed || result.error) {
    const key = browserSessions.affinityKeyFor(task);
    const stagehand = browserSessions.peek(key);
    if (stagehand) {
      const shot = await captureFinalScreenshot(stagehand).catch(
        () => undefined,
      );
      if (shot) await saveRunScreenshot(task.runId, shot);
    }
  }
  return result;
};

// M31: durable worker-registry + task-lease store over the local Phase-2
// Postgres. The worker registers/heartbeats itself and acquires/renews a lease
// per attempt; the C++ scheduler's lease monitor reaps attempts whose lease
// expires (lost worker) and re-dispatches the node.
const leaseStore = new PgTaskLeaseStore();

const worker = new Worker({
  redis: { host, port, password },
  envPrefix,
  group,
  workerId,
  executor: compositeExecutor,
  onShutdown: () => browserSessions.closeAll(),
  // M30: a CANCEL_RUN control message aborts this worker's in-flight attempts
  // for the run (worker.ts); close the run's browser session promptly here so
  // Stagehand/Browserbase resources stop as soon as the cancel propagates.
  onCancelRun: (runId) => browserSessions.closeAllForRun(runId),
  // M31: worker registry + task leases. M34: cadence is env-configurable
  // (undefined => Worker's production defaults).
  leaseStore,
  leaseDurationMs,
  leaseRenewIntervalMs,
  heartbeatIntervalMs,
});

let shuttingDown = false;
async function shutdown(signal: string): Promise<void> {
  if (shuttingDown) return;
  shuttingDown = true;
  console.log(`[worker] received ${signal}; draining...`);
  await worker.stop();
  await eventPublisher.disconnect().catch(() => undefined);
  await leaseStore.close().catch(() => undefined);
  process.exit(0);
}

process.on("SIGTERM", () => void shutdown("SIGTERM"));
process.on("SIGINT", () => void shutdown("SIGINT"));

worker
  .start()
  .then(async () => {
    await eventPublisher.connect().catch(() => undefined);
    console.log(`[worker] ${worker.workerId} running (product executors)`);
  })
  .catch((err) => {
    console.error("[worker] failed to start:", err);
    process.exit(1);
  });
