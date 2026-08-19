// Phase 2 — TypeScript distributed worker service (Milestone 23).
//
// A standalone worker process, separate from Next.js and Trigger.dev. It:
//   1. joins a Redis Streams consumer group on the task stream,
//   2. claims TaskEnvelope messages (at-least-once delivery),
//   3. executes them with a pluggable TaskExecutor (synthetic mode in M23;
//      real node executors are wired in M24),
//   4. publishes a ResultEnvelope to the result stream,
//   5. acks the task message ONLY after the result is durably published
//      (the M23 "durable handoff" rule — never ack before the result lands).
//
// Graceful shutdown (lease semantics): on stop(), the worker stops claiming
// new work, waits up to drainTimeoutMs for in-flight tasks to finish and
// hand off their results, then disconnects. Tasks that do not finish in time
// are NOT acked — they stay pending in the consumer group and are redelivered
// to another worker (M31 will add explicit lease tracking on top). A slow
// worker is therefore never confused with a dead one at the ack layer.
//
// Duplicate delivery: Redis Streams is at-least-once; a redelivered task may
// execute twice. The scheduler dedupes result events by attempt id (M22
// ResultDedupe), so the worker does not attempt transport-level dedupe here.
//
// Timestamps in result envelopes are wall-clock UTC (Date.now()); in-process
// timing is diagnostic only and never published.

import { randomBytes } from "node:crypto";
import os from "node:os";

import {
  decodeTaskEnvelope,
  encodeResultEnvelope,
  ErrorClass,
  ResultStatus,
  type TaskEnvelopeView,
} from "./envelope-codec";
import {
  RedisStreamsClient,
  resultStreamKey,
  taskStreamKey,
  type RedisStreamsConfig,
} from "./redis-streams";

// Result of executing one task.
export interface ExecutorResult {
  completed: boolean;
  output?: string; // opaque JSON owned by the executor
  error?: string;
  errorClass?: number; // ErrorClass value
  retryable?: boolean; // advisory hint (M22)
}

// Pluggable executor. M23 ships a synthetic executor; M24 wires the real
// interpolation + node executors behind this same interface.
export type TaskExecutor = (
  task: TaskEnvelopeView,
  signal: AbortSignal,
) => Promise<ExecutorResult>;

export interface WorkerConfig {
  redis: RedisStreamsConfig;
  /** Stream namespace prefix, e.g. "evo:dev". */
  envPrefix: string;
  /** Consumer group shared by all workers of this stack. */
  group: string;
  /** Stable identity for this process. Generated if omitted. */
  workerId?: string;
  /** Blocking-read slice; also the stop-latency granularity. */
  readBlockMs?: number;
  /** Max time to wait for in-flight tasks during shutdown. */
  drainTimeoutMs?: number;
  executor: TaskExecutor;
  /**
   * Called during graceful shutdown after in-flight tasks drain (M25): the
   * BrowserSessionManager closes every live browser session here. Errors are
   * logged, never fatal — shutdown must complete.
   */
  onShutdown?: () => Promise<void>;
  /** Optional logger; defaults to console. */
  log?: (msg: string) => void;
}

export function generateWorkerId(): string {
  return `worker-${os.hostname()}-${process.pid}-${randomBytes(2).toString("hex")}`;
}

interface ResolvedWorkerConfig {
  redis: RedisStreamsConfig;
  envPrefix: string;
  group: string;
  workerId: string;
  readBlockMs: number;
  drainTimeoutMs: number;
  executor: TaskExecutor;
  onShutdown?: () => Promise<void>;
  log: (msg: string) => void;
}

export class Worker {
  private cfg: ResolvedWorkerConfig;
  private client: RedisStreamsClient;
  private stopping = false;
  private inFlight = new Set<Promise<void>>();
  private abortController = new AbortController();
  private loopPromise: Promise<void> | null = null;

  constructor(config: WorkerConfig) {
    this.cfg = {
      redis: config.redis,
      envPrefix: config.envPrefix,
      group: config.group,
      workerId: config.workerId ?? generateWorkerId(),
      readBlockMs: config.readBlockMs ?? 500,
      drainTimeoutMs: config.drainTimeoutMs ?? 10_000,
      executor: config.executor,
      onShutdown: config.onShutdown,
      log: config.log ?? ((m: string) => console.log(m)),
    };
    this.client = new RedisStreamsClient(config.redis);
  }

  get workerId(): string {
    return this.cfg.workerId;
  }

  get taskStream(): string {
    return taskStreamKey(this.cfg.envPrefix);
  }

  get resultStream(): string {
    return resultStreamKey(this.cfg.envPrefix);
  }

  async start(): Promise<void> {
    await this.client.connect();
    await this.client.ensureGroup(this.taskStream, this.cfg.group);
    // Note: the worker PUBLISHES to the result stream; the scheduler (M26)
    // owns the result-stream consumer group, so we do not create one here.
    this.cfg.log(`[${this.workerId}] started; tasks=${this.taskStream} group=${this.cfg.group}`);
    this.loopPromise = this.readLoop();
  }

  private async readLoop(): Promise<void> {
    while (!this.stopping) {
      let msg;
      try {
        msg = await this.client.readGroup(
          this.taskStream,
          this.cfg.group,
          this.workerId,
          this.cfg.readBlockMs,
        );
      } catch (err) {
        // Transport failure: back off one slice and retry (bounded by the
        // caller's supervision; the client itself retries internally too).
        this.cfg.log(`[${this.workerId}] read error: ${String(err)}`);
        await sleep(this.cfg.readBlockMs);
        continue;
      }
      if (!msg) continue; // timeout slice; loop re-checks stopping
      const job = this.process(msg.id, msg.payload).catch((err) => {
        this.cfg.log(`[${this.workerId}] unhandled process error: ${String(err)}`);
      });
      this.inFlight.add(job);
      job.finally(() => this.inFlight.delete(job));
    }
  }

  private async process(messageId: string, payload: Buffer): Promise<void> {
    const startedAt = Date.now();
    let task: TaskEnvelopeView;
    try {
      task = await decodeTaskEnvelope(payload);
    } catch (err) {
      // Malformed envelope: quarantine (log) and ack so it does not poison
      // the pending list forever. It can never be executed.
      this.cfg.log(
        `[${this.workerId}] QUARANTINE malformed envelope msg=${messageId}: ${String(err)}`,
      );
      await this.client.ack(this.taskStream, this.cfg.group, messageId);
      return;
    }

    let result: ExecutorResult;
    try {
      result = await this.cfg.executor(task, this.abortController.signal);
    } catch (err) {
      result = {
        completed: false,
        error: err instanceof Error ? err.message : String(err),
        errorClass: ErrorClass.ERROR_TRANSIENT,
        retryable: true,
      };
    }

    // Durable handoff rule (M23 step 7): publish the result FIRST; ack the
    // task only after the result is on the result stream. If publishing
    // fails, we do NOT ack — the task stays pending and will be redelivered
    // (duplicate results are deduped scheduler-side by attempt id, M22).
    const resultBytes = await encodeResultEnvelope({
      runId: task.runId,
      nodeId: task.nodeId,
      attemptNumber: task.attemptNumber,
      traceId: task.traceId,
      completed: result.completed,
      output: result.output,
      error: result.error,
      status: result.completed
        ? ResultStatus.OK
        : ResultStatus.NODE_FAILED,
      errorClass: result.errorClass,
      retryable: result.retryable,
      workerId: this.workerId,
      startedAtWallMs: startedAt,
      finishedAtWallMs: Date.now(),
    });

    let published = false;
    for (let attempt = 0; attempt < 3 && !published; attempt++) {
      try {
        await this.client.publish(this.resultStream, resultBytes);
        published = true;
      } catch (err) {
        this.cfg.log(
          `[${this.workerId}] result publish failed (attempt ${attempt + 1}): ${String(err)}`,
        );
        await sleep(50 * (attempt + 1));
      }
    }
    if (!published) {
      // Leave unacked for redelivery; the result was not durably handed off.
      this.cfg.log(
        `[${this.workerId}] result NOT published; leaving msg=${messageId} unacked for redelivery`,
      );
      return;
    }

    await this.client.ack(this.taskStream, this.cfg.group, messageId);
  }

  /**
   * Graceful shutdown: stop claiming, drain in-flight work up to
   * drainTimeoutMs, then disconnect. Unfinished tasks remain pending
   * (unacked) for redelivery — never silently abandoned.
   */
  async stop(): Promise<void> {
    this.stopping = true;
    this.abortController.abort();
    if (this.loopPromise) await this.loopPromise;

    const deadline = Date.now() + this.cfg.drainTimeoutMs;
    while (this.inFlight.size > 0 && Date.now() < deadline) {
      await Promise.race([
        Promise.all([...this.inFlight]),
        sleep(50),
      ]);
    }
    const abandoned = this.inFlight.size;
    if (abandoned > 0) {
      this.cfg.log(
        `[${this.workerId}] shutdown with ${abandoned} task(s) still in flight; left pending for redelivery`,
      );
    }
    // M25: close every live browser session after the drain. Errors must not
    // block shutdown — the Browserbase idle timeout is the backstop.
    if (this.cfg.onShutdown) {
      try {
        await this.cfg.onShutdown();
      } catch (err) {
        this.cfg.log(`[${this.workerId}] onShutdown error: ${String(err)}`);
      }
    }
    await this.client.disconnect();
    this.cfg.log(`[${this.workerId}] stopped`);
  }

  /** Diagnostics for tests/supervision. */
  async pendingCount(): Promise<number> {
    return this.client.pendingCount(this.taskStream, this.cfg.group);
  }

  async resultStreamLength(): Promise<number> {
    return this.client.streamLength(this.resultStream);
  }
}

function sleep(ms: number): Promise<void> {
  return new Promise((resolve) => setTimeout(resolve, ms));
}
