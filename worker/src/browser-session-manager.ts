// Phase 2 — distributed browser session ownership (Milestone 25).
//
// A worker-local BrowserSessionManager keyed by run/affinity key. It owns the
// run's Stagehand/Browserbase session for as long as that session is live:
//
//   - One session per affinity key (default key "run:<runId>"), opened lazily
//     on the first browser task and reused by every later task with the same
//     key. Same-key tasks are therefore pinned to the owning worker while the
//     session is live (the scheduler's capacity-1 affinity policy, M12, routes
//     them here; this manager enforces one-session-per-key locally).
//   - Publishes the Browserbase session id via onSessionOpened AS SOON AS it
//     exists (M25 step 3); the durable engine-neutral event wiring consumes
//     this callback in M26/M27.
//   - Reuses the Phase-1 live-view handshake: before the first browser action,
//     if a viewer is expected, poll isLiveViewConnected(sessionId) up to
//     liveViewWaitMs, then proceed anyway (an unwatched run must not hang).
//   - Reuses the Phase-1 final-screenshot capture before close (jpeg q70 ->
//     base64), surfaced via onFinalScreenshot.
//   - Closes the session on success, failure, cancellation (closeForRun) and
//     on worker graceful shutdown (closeAll).
//
// WORKER-CRASH SEMANTICS (documented, NOT implemented): if the worker process
// crashes, the Browserbase session is NOT recovered or reattached — it leaks
// until Browserbase's own idle timeout. Crash recovery of browser sessions is
// M34. This manager does not pretend the session survives a crash.
//
// Testability: the Stagehand session comes from an injectable factory; the
// live-view check and screenshot capture are injectable too, so automated
// tests never touch Browserbase.

import type { Stagehand } from "@browserbasehq/stagehand";

import type { TaskEnvelopeView } from "./envelope-codec";

export interface BrowserSessionHandle {
  stagehand: Stagehand;
  browserbaseSessionId?: string;
}

/** Opens a fresh Stagehand session for an affinity key. */
export type StagehandFactory = (
  affinityKey: string,
) => Promise<BrowserSessionHandle>;

export interface SessionOpenedInfo {
  affinityKey: string;
  runId: string;
  browserbaseSessionId?: string;
}

export interface FinalScreenshotInfo {
  affinityKey: string;
  runId: string;
  screenshotBase64: string;
}

export interface BrowserSessionManagerOptions {
  factory: StagehandFactory;
  /** Called as soon as the session id exists (M25 step 3). */
  onSessionOpened?: (info: SessionOpenedInfo) => Promise<void> | void;
  /** Phase-1 live-view handshake (data.ts isLiveViewConnected). Injectable. */
  isLiveViewConnected?: (sessionId: string) => Promise<boolean>;
  liveViewWaitMs?: number;
  liveViewPollMs?: number;
  /** Final screenshot capture before close. Defaults to the Phase-1 pattern. */
  captureScreenshot?: (stagehand: Stagehand) => Promise<string | undefined>;
  onFinalScreenshot?: (info: FinalScreenshotInfo) => Promise<void> | void;
  log?: (msg: string) => void;
}

interface ManagedSession {
  affinityKey: string;
  runId: string;
  handle: BrowserSessionHandle;
  /** True once the live-view wait has been performed for this session. */
  liveViewWaited: boolean;
  closed: boolean;
}

/** Default final-screenshot capture, mirroring run-workflow.ts. */
export async function captureFinalScreenshot(
  stagehand: Stagehand,
): Promise<string | undefined> {
  try {
    const pages = stagehand.context?.pages();
    const activePage =
      pages && pages.length > 0 ? pages[pages.length - 1] : undefined;
    if (!activePage) return undefined;
    const buffer = await activePage.screenshot({ type: "jpeg", quality: 70 });
    return buffer.toString("base64");
  } catch {
    return undefined;
  }
}

export class BrowserSessionManager {
  private opts: Required<
    Pick<BrowserSessionManagerOptions, "factory">
  > & BrowserSessionManagerOptions;
  private sessions = new Map<string, ManagedSession>();
  // Single-flight: concurrent getForTask for the same key share one open.
  private opening = new Map<string, Promise<ManagedSession>>();
  private log: (msg: string) => void;

  constructor(options: BrowserSessionManagerOptions) {
    this.opts = options;
    this.log = options.log ?? (() => {});
  }

  /** The affinity key for a task; defaults to "run:<runId>". */
  affinityKeyFor(task: TaskEnvelopeView): string {
    return task.affinityKey || `run:${task.runId}`;
  }

  /**
   * Return the live Stagehand for an affinity key WITHOUT opening one.
   * Undefined when no session is open (or it was closed). Used to capture a
   * screenshot after a task without forcing a session open.
   */
  peek(affinityKey: string): Stagehand | undefined {
    const session = this.sessions.get(affinityKey);
    if (!session || session.closed) return undefined;
    return session.handle.stagehand;
  }

  /**
   * Get (or open) the Stagehand session for a task's affinity key. Waits for
   * the live-view handshake once per session before the first browser action.
   */
  async getForTask(task: TaskEnvelopeView): Promise<Stagehand> {
    const key = this.affinityKeyFor(task);
    const session = await this.getOrOpen(key, task.runId);
    if (!session.liveViewWaited) {
      session.liveViewWaited = true;
      await this.waitForLiveView(session);
    }
    return session.handle.stagehand;
  }

  private async getOrOpen(
    key: string,
    runId: string,
  ): Promise<ManagedSession> {
    const existing = this.sessions.get(key);
    if (existing && !existing.closed) return existing;

    const inFlight = this.opening.get(key);
    if (inFlight) return inFlight;

    const openPromise = (async () => {
      const handle = await this.opts.factory(key);
      const session: ManagedSession = {
        affinityKey: key,
        runId,
        handle,
        liveViewWaited: false,
        closed: false,
      };
      this.sessions.set(key, session);
      // Publish the session id as soon as it exists (M25 step 3).
      if (this.opts.onSessionOpened) {
        await this.opts.onSessionOpened({
          affinityKey: key,
          runId,
          browserbaseSessionId: handle.browserbaseSessionId,
        });
      }
      this.log(`[browser] opened session for ${key}`);
      return session;
    })();

    this.opening.set(key, openPromise);
    try {
      return await openPromise;
    } finally {
      this.opening.delete(key);
    }
  }

  private async waitForLiveView(session: ManagedSession): Promise<void> {
    const sessionId = session.handle.browserbaseSessionId;
    const check = this.opts.isLiveViewConnected;
    if (!sessionId || !check) return;

    const waitMs = this.opts.liveViewWaitMs ?? 60_000;
    const pollMs = this.opts.liveViewPollMs ?? 1_000;
    const deadline = Date.now() + waitMs;
    while (Date.now() < deadline) {
      try {
        if (await check(sessionId)) {
          this.log(`[browser] live view connected for ${session.affinityKey}`);
          return;
        }
      } catch {
        // Polling errors must not fail the run; keep waiting.
      }
      await new Promise((r) => setTimeout(r, pollMs));
    }
    this.log(
      `[browser] live view did not connect in time for ${session.affinityKey}; proceeding`,
    );
  }

  /**
   * Close the session for a run (success/failure/cancellation path). Captures
   * the final screenshot first (best-effort), then closes. Idempotent.
   */
  async closeForRun(runId: string): Promise<void> {
    const key = `run:${runId}`;
    await this.closeKey(key);
  }

  /** Close every live session (worker graceful shutdown). */
  async closeAll(): Promise<void> {
    const keys = [...this.sessions.keys()];
    for (const key of keys) {
      await this.closeKey(key);
    }
  }

  private async closeKey(key: string): Promise<void> {
    const session = this.sessions.get(key);
    if (!session || session.closed) return;
    session.closed = true;

    const capture = this.opts.captureScreenshot ?? captureFinalScreenshot;
    const screenshot = await capture(session.handle.stagehand).catch(
      () => undefined,
    );
    if (screenshot && this.opts.onFinalScreenshot) {
      await Promise.resolve(
        this.opts.onFinalScreenshot({
          affinityKey: key,
          runId: session.runId,
          screenshotBase64: screenshot,
        }),
      ).catch(() => undefined);
    }

    try {
      await session.handle.stagehand.close();
    } catch (err) {
      this.log(`[browser] error closing ${key}: ${String(err)}`);
    }
    this.sessions.delete(key);
    this.log(`[browser] closed session for ${key}`);
  }

  /** Number of live sessions (diagnostic/test helper). */
  liveCount(): number {
    return [...this.sessions.values()].filter((s) => !s.closed).length;
  }
}
