// Phase 2 — distributed worker entry point (Milestone 23).
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
// Graceful shutdown: SIGTERM/SIGINT stop claiming and drain in-flight tasks
// (default 10s) before exiting. Unfinished tasks stay pending for redelivery.

import { Worker } from "./worker";
import { syntheticExecutor } from "./synthetic-executor";

const host = process.env.EVO_PHASE2_REDIS_HOST ?? "127.0.0.1";
const port = Number(process.env.EVO_PHASE2_REDIS_PORT ?? 6390);
const password = process.env.EVO_PHASE2_REDIS_PASSWORD || undefined;
const envPrefix = process.env.EVO_WORKER_ENV_PREFIX ?? "evo:dev";
const group = process.env.EVO_WORKER_GROUP ?? "workers";
const workerId = process.env.EVO_WORKER_ID || undefined;

const worker = new Worker({
  redis: { host, port, password },
  envPrefix,
  group,
  workerId,
  executor: syntheticExecutor,
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
