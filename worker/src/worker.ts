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
  ControlKind,
  decodeControlEnvelope,
  decodeTaskEnvelope,
  encodeResultEnvelope,
  ErrorClass,
  ResultStatus,
  type TaskEnvelopeView,
} from "./envelope-codec";
import {
  controlStreamKey,
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

// Milestone 31 — durable worker-registry + task-lease store. The worker
// registers/heartbeats itself and acquires/renews a lease per attempt. The
// scheduler (C++ DistributedRunLoop) scans for expired leases and reaps them.
//
// Semantics (mirror the C++ RunStore):
//   - A heartbeat proves the PROCESS is alive (registry row). Its cadence and
//     expiry are defined SEPARATELY from the per-task lease duration.
//   - A lease proves a specific ATTEMPT is being worked. The worker acquires
//     it on claim and renews it while work legitimately runs. If the worker
//     dies, renewals stop and the lease expires -> the scheduler reaps the
//     attempt and re-dispatches the node (recovery, not failure).
// All timestamps are wall-clock UTC milliseconds.
export interface TaskLeaseStore {
  workerHeartbeat(
    workerId: string,
    envPrefix: string,
    nowWallMs: number,
  ): Promise<boolean>;
  acquireAttemptLease(
    runId: string,
    nodeId: string,
    attemptNumber: number,
    workerId: string,
    acquiredWallMs: number,
    expiresWallMs: number,
  ): Promise<boolean>;
  renewAttemptLease(
    runId: string,
    nodeId: string,
    attemptNumber: number,
    workerId: string,
    renewedWallMs: number,
    expiresWallMs: number,
  ): Promise<boolean>;
}

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
  /**
   * Called when a CANCEL_RUN control message arrives for a run (Milestone 30),
   * after this worker has aborted its in-flight attempts for that run. The
   * BrowserSessionManager closes the run's browser session here so Stagehand
   * resources stop promptly. Errors are logged, never fatal.
   */
  onCancelRun?: (runId: string, reason: string) => Promise<void> | void;
  /**
   * Milestone 31: durable worker-registry + task-lease store. When provided,
   * the worker registers/heartbeats itself and acquires/renews a lease per
   * attempt. When omitted, lease monitoring is disabled (the scheduler's scan
   * finds nothing to reap for this worker's attempts).
   */
  leaseStore?: TaskLeaseStore;
  /** Lease duration stamped on acquire/renew (default 30s). */
  leaseDurationMs?: number;
  /** How often an in-flight attempt's lease is renewed (default 10s). Must be
   * well under leaseDurationMs so a renewing worker is never reaped. */
  leaseRenewIntervalMs?: number;
  /** How often the worker heartbeats its registry row (default 5s). Separate
   * cadence from the lease (M31 step 1). */
  heartbeatIntervalMs?: number;
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
  onCancelRun?: (runId: string, reason: string) => Promise<void> | void;
  leaseStore?: TaskLeaseStore;
  leaseDurationMs: number;
  leaseRenewIntervalMs: number;
  heartbeatIntervalMs: number;
  log: (msg: string) => void;
}

export class Worker {
  private cfg: ResolvedWorkerConfig;
  private client: RedisStreamsClient;
  // M30: the control loop gets its OWN connection. Redis processes commands
  // sequentially per connection, so sharing the task-loop connection would
  // let one blocking XREADGROUP delay the other by a full read slice.
  private controlClient: RedisStreamsClient;
  private stopping = false;
  private inFlight = new Set<Promise<void>>();
  private abortController = new AbortController();
  private loopPromise: Promise<void> | null = null;
  private controlLoopPromise: Promise<void> | null = null;

  // Milestone 30 — per-run cancellation. Each canceled run gets its own
  // AbortController; in-flight tasks for that run receive a signal that
  // combines the worker-wide shutdown signal with the run's cancel signal
  // (AbortSignal.any). Queued tasks for a canceled run are short-circuited
  // at claim time. Duplicate CANCEL_RUN deliveries are no-ops (the set is
  // the dedupe key).
  private canceledRuns = new Set<string>();
  private runAbortControllers = new Map<string, AbortController>();

  // Milestone 31 — worker-registry heartbeat timer (process liveness). Runs
  // on its own cadence, separate from per-task lease renewal.
  private heartbeatTimer: ReturnType<typeof setInterval> | null = null;

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
      onCancelRun: config.onCancelRun,
      leaseStore: config.leaseStore,
      leaseDurationMs: config.leaseDurationMs ?? 30_000,
      leaseRenewIntervalMs: config.leaseRenewIntervalMs ?? 10_000,
      heartbeatIntervalMs: config.heartbeatIntervalMs ?? 5_000,
      log: config.log ?? ((m: string) => console.log(m)),
    };
    this.client = new RedisStreamsClient(config.redis);
    this.controlClient = new RedisStreamsClient(config.redis);
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

  get controlStream(): string {
    return controlStreamKey(this.cfg.envPrefix);
  }

  /** True when a CANCEL_RUN control message has been seen for this run. */
  isRunCanceled(runId: string): boolean {
    return this.canceledRuns.has(runId);
  }

  async start(): Promise<void> {
    await this.client.connect();
    await this.controlClient.connect();
    await this.client.ensureGroup(this.taskStream, this.cfg.group);
    // Note: the worker PUBLISHES to the result stream; the scheduler (M26)
    // owns the result-stream consumer group, so we do not create one here.
    //
    // M30: control-stream fan-out. Every worker must see every control
    // message, so each worker reads with its OWN consumer group (a shared
    // group would deliver each message to only one worker). The group is
    // created with start id "0" so a worker that (re)joins mid-run still
    // receives cancellations published before it subscribed.
    await this.controlClient.ensureGroup(
      this.controlStream,
      this.controlGroup(),
      "0",
    );
    this.cfg.log(`[${this.workerId}] started; tasks=${this.taskStream} group=${this.cfg.group}`);
    this.loopPromise = this.readLoop();
    this.controlLoopPromise = this.controlLoop();

    // M31: register + heartbeat the worker registry row. The first heartbeat
    // registers (upsert); subsequent ones refresh last_heartbeat_at. Runs on
    // its own cadence, separate from per-task lease renewal.
    if (this.cfg.leaseStore) {
      await this.heartbeat().catch(() => undefined);
      this.heartbeatTimer = setInterval(() => {
        void this.heartbeat();
      }, this.cfg.heartbeatIntervalMs);
      // Don't keep the event loop alive just for the heartbeat.
      this.heartbeatTimer.unref?.();
    }
  }

  // M31: upsert this worker's liveness row. Best-effort — a missed heartbeat
  // is retried on the next tick; the registry expiry is generous enough to
  // tolerate a few misses.
  private async heartbeat(): Promise<void> {
    if (!this.cfg.leaseStore) return;
    try {
      await this.cfg.leaseStore.workerHeartbeat(
        this.cfg.workerId,
        this.cfg.envPrefix,
        Date.now(),
      );
    } catch (err) {
      this.cfg.log(`[${this.workerId}] heartbeat failed: ${String(err)}`);
    }
  }

  /** Per-worker control consumer group (fan-out; see start()). */
  private controlGroup(): string {
    return `control-${this.cfg.workerId}`;
  }

  /**
   * Get-or-create the AbortController for a run. Created lazily at task start
   * so an in-flight task holds a signal that a later CANCEL_RUN can abort;
   * the same instance is aborted by applyControlMessage.
   */
  private runAbortControllerFor(runId: string): AbortController {
    let controller = this.runAbortControllers.get(runId);
    if (!controller) {
      controller = new AbortController();
      this.runAbortControllers.set(runId, controller);
    }
    return controller;
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

  // M30: control-stream fan-out loop. Reads CANCEL_RUN messages from this
  // worker's own consumer group and applies them: abort in-flight attempts
  // for the run, short-circuit future queued tasks, and close the run's
  // browser session via onCancelRun. Duplicate deliveries are no-ops.
  private async controlLoop(): Promise<void> {
    while (!this.stopping) {
      let msg;
      try {
        msg = await this.controlClient.readGroup(
          this.controlStream,
          this.controlGroup(),
          this.workerId,
          this.cfg.readBlockMs,
        );
      } catch (err) {
        this.cfg.log(`[${this.workerId}] control read error: ${String(err)}`);
        await sleep(this.cfg.readBlockMs);
        continue;
      }
      if (!msg) continue;
      try {
        await this.applyControlMessage(msg.payload);
      } catch (err) {
        this.cfg.log(
          `[${this.workerId}] control message handling error: ${String(err)}`,
        );
      }
      // Ack applied AND ignored/malformed control messages alike: they are
      // consumed, never reprocessed (handlers are idempotent anyway).
      await this.controlClient
        .ack(this.controlStream, this.controlGroup(), msg.id)
        .catch(() => undefined);
    }
  }

  private async applyControlMessage(payload: Buffer): Promise<void> {
    let control;
    try {
      control = await decodeControlEnvelope(payload);
    } catch (err) {
      this.cfg.log(
        `[${this.workerId}] QUARANTINE malformed control envelope: ${String(err)}`,
      );
      return;
    }
    if (control.kind !== ControlKind.CANCEL_RUN || !control.runId) return;
    if (this.canceledRuns.has(control.runId)) return; // duplicate delivery

    this.canceledRuns.add(control.runId);
    // Abort the SAME controller the in-flight task(s) hold a signal to.
    const controller = this.runAbortControllerFor(control.runId);
    controller.abort();
    this.cfg.log(
      `[${this.workerId}] CANCEL_RUN run=${control.runId} reason=${control.reason || "-"}; aborting in-flight attempts`,
    );

    // Close the run's browser session promptly (M30 step 4). Best-effort:
    // a close failure never blocks cancellation — Browserbase's idle
    // timeout is the backstop.
    if (this.cfg.onCancelRun) {
      try {
        await this.cfg.onCancelRun(control.runId, control.reason);
      } catch (err) {
        this.cfg.log(
          `[${this.workerId}] onCancelRun error run=${control.runId}: ${String(err)}`,
        );
      }
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

    // M30: short-circuit queued tasks for a canceled run. The run is already
    // terminal canceled in the durable store; executing the task would only
    // produce a late result the scheduler rejects. Publish a CANCELED result
    // (durable handoff, same as any other outcome) and ack.
    if (this.canceledRuns.has(task.runId)) {
      this.cfg.log(
        `[${this.workerId}] short-circuit queued task run=${task.runId} node=${task.nodeId} (run canceled)`,
      );
      await this.publishCanceledResult(task, startedAt);
      await this.client.ack(this.taskStream, this.cfg.group, messageId);
      return;
    }

    // Per-run signal: worker-wide shutdown OR this run's cancellation. The
    // run controller is created lazily HERE (not only on cancel) so a task
    // that started before its cancel arrived still holds a signal that the
    // later CANCEL_RUN can abort.
    const runController = this.runAbortControllerFor(task.runId);
    const signal = AbortSignal.any([
      this.abortController.signal,
      runController.signal,
    ]);

    // M31: acquire the attempt lease before executing. This stamps worker_id
    // + lease_acquired/renewed/expires on the durable attempt row, taking over
    // the queue-wait lease the scheduler initialized at dispatch. If the
    // acquire fails (another worker holds an unexpired lease), skip execution
    // and ack — the rightful owner will hand off the result.
    let leaseHeld = false;
    if (this.cfg.leaseStore) {
      try {
        leaseHeld = await this.cfg.leaseStore.acquireAttemptLease(
          task.runId,
          task.nodeId,
          task.attemptNumber,
          this.workerId,
          Date.now(),
          Date.now() + this.cfg.leaseDurationMs,
        );
      } catch (err) {
        this.cfg.log(
          `[${this.workerId}] lease acquire failed run=${task.runId} node=${task.nodeId}: ${String(err)}`,
        );
      }
      if (!leaseHeld) {
        this.cfg.log(
          `[${this.workerId}] lease not acquired (held by another worker); skipping run=${task.runId} node=${task.nodeId}`,
        );
        await this.client.ack(this.taskStream, this.cfg.group, messageId);
        return;
      }
    }

    // M31: renew the lease periodically while work legitimately runs. A
    // renewing worker is never reaped (its expiry keeps moving forward). If
    // the worker dies, renewals stop and the scheduler's scan reaps the
    // attempt after leaseDurationMs.
    let renewTimer: ReturnType<typeof setInterval> | null = null;
    if (leaseHeld && this.cfg.leaseStore) {
      renewTimer = setInterval(() => {
        void this.cfg.leaseStore
          ?.renewAttemptLease(
            task.runId,
            task.nodeId,
            task.attemptNumber,
            this.workerId,
            Date.now(),
            Date.now() + this.cfg.leaseDurationMs,
          )
          .catch((err) => {
            this.cfg.log(
              `[${this.workerId}] lease renew failed run=${task.runId} node=${task.nodeId}: ${String(err)}`,
            );
          });
      }, this.cfg.leaseRenewIntervalMs);
      renewTimer.unref?.();
    }

    let result: ExecutorResult;
    try {
      result = await this.cfg.executor(task, signal);
    } catch (err) {
      // An abort from a CANCEL_RUN message is a cancellation, not a failure:
      // report it as such so the durable attempt row + scheduler see the
      // right semantics (ERROR_CANCELED, not retryable). A worker-shutdown
      // abort keeps the legacy transient-failure classification.
      if (this.canceledRuns.has(task.runId)) {
        result = {
          completed: false,
          error: "canceled",
          errorClass: ErrorClass.ERROR_CANCELED,
          retryable: false,
        };
      } else {
        result = {
          completed: false,
          error: err instanceof Error ? err.message : String(err),
          errorClass: ErrorClass.ERROR_TRANSIENT,
          retryable: true,
        };
      }
    } finally {
      // Stop renewing once the attempt has finished (success or failure); the
      // result handoff below makes the lease moot.
      if (renewTimer) clearInterval(renewTimer);
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
        : this.canceledRuns.has(task.runId)
          ? ResultStatus.CANCELED
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

  // M30: durable handoff for a short-circuited queued task of a canceled run.
  // Publishes a CANCELED result (never executed) and returns; the caller acks.
  private async publishCanceledResult(
    task: TaskEnvelopeView,
    startedAt: number,
  ): Promise<void> {
    const resultBytes = await encodeResultEnvelope({
      runId: task.runId,
      nodeId: task.nodeId,
      attemptNumber: task.attemptNumber,
      traceId: task.traceId,
      completed: false,
      error: "canceled",
      status: ResultStatus.CANCELED,
      errorClass: ErrorClass.ERROR_CANCELED,
      retryable: false,
      workerId: this.workerId,
      startedAtWallMs: startedAt,
      finishedAtWallMs: Date.now(),
    });
    for (let attempt = 0; attempt < 3; attempt++) {
      try {
        await this.client.publish(this.resultStream, resultBytes);
        return;
      } catch (err) {
        this.cfg.log(
          `[${this.workerId}] canceled-result publish failed (attempt ${attempt + 1}): ${String(err)}`,
        );
        await sleep(50 * (attempt + 1));
      }
    }
    this.cfg.log(
      `[${this.workerId}] canceled-result NOT published for run=${task.runId} node=${task.nodeId}`,
    );
  }

  /**
   * Graceful shutdown: stop claiming, drain in-flight work up to
   * drainTimeoutMs, then disconnect. Unfinished tasks remain pending
   * (unacked) for redelivery — never silently abandoned.
   */
  async stop(): Promise<void> {
    this.stopping = true;
    // M31: stop heartbeating; the registry row will age out on its own.
    if (this.heartbeatTimer) {
      clearInterval(this.heartbeatTimer);
      this.heartbeatTimer = null;
    }
    this.abortController.abort();
    if (this.loopPromise) await this.loopPromise;
    if (this.controlLoopPromise) await this.controlLoopPromise;

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
    await this.controlClient.disconnect();
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
