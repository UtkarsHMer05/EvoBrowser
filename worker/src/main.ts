// Phase 2 — distributed worker entry point (Milestone 23, browser wiring M25).
//
// Runs one worker process against the local Phase-2 Redis. Separate from
// Next.js and Trigger.dev. Uses the synthetic executor (M23); real node
// executors are wired in M24.
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
// Graceful shutdown: SIGTERM/SIGINT stop claiming and drain in-flight tasks
// (default 10s) before exiting, then close all live browser sessions.
// Unfinished tasks stay pending for redelivery.

import { Stagehand } from "@browserbasehq/stagehand";

import { BrowserSessionManager } from "./browser-session-manager";
import { Worker } from "./worker";
import { syntheticExecutor } from "./synthetic-executor";

const host = process.env.EVO_PHASE2_REDIS_HOST ?? "127.0.0.1";
const port = Number(process.env.EVO_PHASE2_REDIS_PORT ?? 6390);
const password = process.env.EVO_PHASE2_REDIS_PASSWORD || undefined;
const envPrefix = process.env.EVO_WORKER_ENV_PREFIX ?? "evo:dev";
const group = process.env.EVO_WORKER_GROUP ?? "workers";
const workerId = process.env.EVO_WORKER_ID || undefined;

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
  // M25 step 3: publish the session id as soon as it exists. The durable
  // engine-neutral event write is wired in M26/M27; for now the id is logged
  // so the owning worker and its session are observable.
  onSessionOpened: (info) => {
    console.log(
      `[worker] browser session opened run=${info.runId} key=${info.affinityKey} browserbaseSessionId=${info.browserbaseSessionId ?? "unknown"}`,
    );
  },
  log: (msg) => console.log(msg),
});

const worker = new Worker({
  redis: { host, port, password },
  envPrefix,
  group,
  workerId,
  executor: syntheticExecutor,
  onShutdown: () => browserSessions.closeAll(),
});

let shuttingDown = false;
async function shutdown(signal: string): Promise<void> {
  if (shuttingDown) return;
  shuttingDown = true;
  console.log(`[worker] received ${signal}; draining...`);
  await worker.stop();
  process.exit(0);
}

process.on("SIGTERM", () => void shutdown("SIGTERM"));
process.on("SIGINT", () => void shutdown("SIGINT"));

worker
  .start()
  .then(() => {
    console.log(`[worker] ${worker.workerId} running (synthetic executor)`);
  })
  .catch((err) => {
    console.error("[worker] failed to start:", err);
    process.exit(1);
  });
