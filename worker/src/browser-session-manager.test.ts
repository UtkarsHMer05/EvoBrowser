// Milestone 25: distributed browser session ownership + live-view parity.
//
// Tests the worker-local BrowserSessionManager with a FAKE Stagehand — no
// Browserbase, no network, no real browser. Covers:
//   1. One session per affinity key, opened lazily, reused by later tasks.
//   2. Session id published via onSessionOpened as soon as it exists.
//   3. Live-view handshake polled before the first browser action (once).
//   4. Live-view wait times out and proceeds (an unwatched run must not hang).
//   5. Concurrent getForTask for the same key opens exactly one session.
//   6. closeForRun captures the final screenshot, publishes it, closes the
//      session, and is idempotent.
//   7. closeAll (graceful shutdown) closes every live session.
//   8. Default affinity key is "run:<runId>" when the envelope has none.
//   9. Adapter integration: a browser node gets its session through the
//      manager via the adapter's getStagehand hook.

import assert from "node:assert/strict";

import type { Stagehand } from "@browserbasehq/stagehand";

import { BrowserSessionManager } from "./browser-session-manager";
import type { TaskEnvelopeView } from "./envelope-codec";
import { createNodeExecutorAdapter } from "./node-executor-adapter";
import type { WorkflowGraph } from "@/lib/db/schema";
import type { NodeType } from "@/features/workflows/nodes/node-registry";

let passed = 0;
const ok = (label: string) => {
  passed++;
  console.log(`  ok   ${label}`);
};

// --- Fake Stagehand ---------------------------------------------------------

interface FakeStagehand {
  closed: boolean;
  closeCalls: number;
  screenshotCalls: number;
  close: () => Promise<void>;
  context: { pages: () => Array<{ screenshot: (o: unknown) => Promise<Buffer> }> };
}

function makeFakeStagehand(sessionId: string): {
  stagehand: Stagehand;
  fake: FakeStagehand;
} {
  const fake: FakeStagehand = {
    closed: false,
    closeCalls: 0,
    screenshotCalls: 0,
    close: async () => {
      fake.closeCalls++;
      fake.closed = true;
    },
    context: {
      pages: () => [
        {
          screenshot: async () => {
            fake.screenshotCalls++;
            return Buffer.from(`shot:${sessionId}`);
          },
        },
      ],
    },
  };
  return { stagehand: fake as unknown as Stagehand, fake };
}

function makeTask(overrides: Partial<TaskEnvelopeView>): TaskEnvelopeView {
  return {
    runId: "run-1",
    workflowVersionId: "wfv-1",
    orgId: "org-1",
    nodeId: "open_url_1",
    attemptNumber: 1,
    resourceClass: 1,
    affinityKey: "",
    traceId: "",
    spanId: "",
    nodeType: "open-url",
    nodePayloadJson: "",
    ...overrides,
  };
}

async function main() {
  // --- 1+2. Lazy open, reuse, session id published -------------------------
  {
    let opens = 0;
    const opened: Array<{ affinityKey: string; sessionId?: string }> = [];
    const mgr = new BrowserSessionManager({
      factory: async () => {
        opens++;
        const { stagehand } = makeFakeStagehand(`sess-${opens}`);
        return { stagehand, browserbaseSessionId: `sess-${opens}` };
      },
      onSessionOpened: (info) => {
        opened.push({
          affinityKey: info.affinityKey,
          sessionId: info.browserbaseSessionId,
        });
      },
    });

    const s1 = await mgr.getForTask(makeTask({ runId: "run-A" }));
    const s2 = await mgr.getForTask(makeTask({ runId: "run-A" }));
    assert.equal(opens, 1, "one session per affinity key");
    assert.equal(s1, s2, "same Stagehand instance reused");
    assert.equal(mgr.liveCount(), 1, "one live session");
    assert.deepEqual(
      opened,
      [{ affinityKey: "run:run-A", sessionId: "sess-1" }],
      "session id published as soon as it exists",
    );
    ok("one session per affinity key, reused, id published on open");
  }

  // --- 3. Live-view handshake before first browser action -------------------
  {
    let checks = 0;
    const mgr = new BrowserSessionManager({
      factory: async () => {
        const { stagehand } = makeFakeStagehand("sess-lv");
        return { stagehand, browserbaseSessionId: "sess-lv" };
      },
      isLiveViewConnected: async () => {
        checks++;
        return checks >= 2; // connects on the second poll
      },
      liveViewWaitMs: 5_000,
      liveViewPollMs: 5,
    });

    await mgr.getForTask(makeTask({ runId: "run-LV" }));
    assert.ok(checks >= 2, "polled until live view connected");
    const checksAfterFirst = checks;

    // Second task on the same session must NOT re-wait (handshake is once).
    await mgr.getForTask(makeTask({ runId: "run-LV" }));
    assert.equal(checks, checksAfterFirst, "handshake performed once per session");
    ok("live-view handshake polled before first action, once per session");
  }

  // --- 4. Live-view wait times out and proceeds ------------------------------
  {
    const mgr = new BrowserSessionManager({
      factory: async () => {
        const { stagehand } = makeFakeStagehand("sess-timeout");
        return { stagehand, browserbaseSessionId: "sess-timeout" };
      },
      isLiveViewConnected: async () => false,
      liveViewWaitMs: 30,
      liveViewPollMs: 5,
    });
    const start = Date.now();
    const s = await mgr.getForTask(makeTask({ runId: "run-T" }));
    assert.ok(s, "session returned even though live view never connected");
    assert.ok(Date.now() - start >= 30, "waited the configured window");
    ok("unwatched run does not hang: proceeds after live-view timeout");
  }

  // --- 5. Concurrent open is single-flight ----------------------------------
  {
    let opens = 0;
    const mgr = new BrowserSessionManager({
      factory: async () => {
        opens++;
        await new Promise((r) => setTimeout(r, 20));
        const { stagehand } = makeFakeStagehand(`sess-c${opens}`);
        return { stagehand, browserbaseSessionId: `sess-c${opens}` };
      },
    });
    const [a, b, c] = await Promise.all([
      mgr.getForTask(makeTask({ runId: "run-C" })),
      mgr.getForTask(makeTask({ runId: "run-C" })),
      mgr.getForTask(makeTask({ runId: "run-C" })),
    ]);
    assert.equal(opens, 1, "concurrent getForTask opens exactly one session");
    assert.equal(a, b, "all callers share the session");
    assert.equal(b, c, "all callers share the session");
    ok("concurrent same-key tasks share one open (single-flight)");
  }

  // --- 6. closeForRun: screenshot, publish, close, idempotent ---------------
  {
    const shots: Array<{ runId: string; b64: string }> = [];
    let fake: FakeStagehand | undefined;
    const mgr = new BrowserSessionManager({
      factory: async () => {
        const made = makeFakeStagehand("sess-close");
        fake = made.fake;
        return { stagehand: made.stagehand, browserbaseSessionId: "sess-close" };
      },
      onFinalScreenshot: (info) => {
        shots.push({ runId: info.runId, b64: info.screenshotBase64 });
      },
    });
    await mgr.getForTask(makeTask({ runId: "run-X" }));

    await mgr.closeForRun("run-X");
    assert.equal(fake!.closeCalls, 1, "stagehand closed");
    assert.equal(fake!.screenshotCalls, 1, "final screenshot captured before close");
    assert.equal(shots.length, 1, "screenshot published");
    assert.equal(shots[0].runId, "run-X");
    assert.equal(
      Buffer.from(shots[0].b64, "base64").toString(),
      "shot:sess-close",
      "screenshot bytes round-trip",
    );
    assert.equal(mgr.liveCount(), 0, "no live sessions after close");

    await mgr.closeForRun("run-X"); // idempotent
    assert.equal(fake!.closeCalls, 1, "second close is a no-op");
    ok("closeForRun: screenshot -> publish -> close, idempotent");
  }

  // --- 7. closeAll on graceful shutdown --------------------------------------
  {
    const closed: string[] = [];
    const mgr = new BrowserSessionManager({
      factory: async (key) => {
        const { stagehand, fake } = makeFakeStagehand(`sess-${key}`);
        const orig = fake.close;
        fake.close = async () => {
          closed.push(key);
          await orig();
        };
        return { stagehand, browserbaseSessionId: `sess-${key}` };
      },
      captureScreenshot: async () => undefined,
    });
    await mgr.getForTask(makeTask({ runId: "run-1" }));
    await mgr.getForTask(makeTask({ runId: "run-2" }));
    assert.equal(mgr.liveCount(), 2);
    await mgr.closeAll();
    assert.equal(mgr.liveCount(), 0, "all sessions closed on shutdown");
    assert.deepEqual(closed.sort(), ["run:run-1", "run:run-2"]);
    ok("closeAll closes every live session (graceful shutdown)");
  }

  // --- 8. Default affinity key ------------------------------------------------
  {
    const mgr = new BrowserSessionManager({
      factory: async () => {
        const { stagehand } = makeFakeStagehand("s");
        return { stagehand };
      },
    });
    assert.equal(
      mgr.affinityKeyFor(makeTask({ runId: "run-K", affinityKey: "" })),
      "run:run-K",
      "empty affinity key defaults to run:<runId>",
    );
    assert.equal(
      mgr.affinityKeyFor(makeTask({ runId: "run-K", affinityKey: "custom" })),
      "custom",
      "explicit affinity key wins",
    );
    ok("affinity key defaults to run:<runId>");
  }

  // --- 9. Adapter integration: browser node gets session via manager --------
  {
    const seenStagehands: Stagehand[] = [];
    const mgr = new BrowserSessionManager({
      factory: async () => {
        const { stagehand } = makeFakeStagehand("sess-adapt");
        return { stagehand, browserbaseSessionId: "sess-adapt" };
      },
    });

    const graph: WorkflowGraph = {
      nodes: [
        {
          id: "open_url_1",
          type: "step",
          position: { x: 0, y: 0 },
          data: {
            type: "open-url",
            kind: "action",
            title: "Open",
            values: { url: "https://example.com" },
          },
        },
      ],
      edges: [],
    };

    const openUrlOverride = async ({
      getStagehand,
    }: {
      values: Record<string, string>;
      getStagehand: () => Promise<Stagehand>;
    }) => {
      const s = await getStagehand();
      seenStagehands.push(s);
      return { url: "https://example.com", title: "Example" };
    };

    const adapter = createNodeExecutorAdapter({
      loadVersion: async () => ({ workflowVersionId: "wfv-1", graph }),
      loadPredecessorOutputs: async () => ({}),
      getStagehand: (task) => mgr.getForTask(task),
      executorOverrides: { "open-url": openUrlOverride } as Partial<
        Record<NodeType, typeof openUrlOverride>
      >,
    });

    const task = makeTask({ runId: "run-AD", nodeId: "open_url_1" });
    const result = await adapter(task, new AbortController().signal);
    assert.equal(result.completed, true, "browser node completed");
    assert.equal(seenStagehands.length, 1, "executor received a session");
    assert.equal(mgr.liveCount(), 1, "session stays live for the run");

    // A second browser task in the same run reuses the SAME session.
    const result2 = await adapter(task, new AbortController().signal);
    assert.equal(result2.completed, true);
    assert.equal(seenStagehands[0], seenStagehands[1], "session reused across tasks");

    await mgr.closeForRun("run-AD");
    assert.equal(mgr.liveCount(), 0, "session closed at end of run");
    ok("adapter integration: browser node session owned + reused + closed by manager");
  }

  console.log(`\nALL M25 BROWSER SESSION MANAGER TESTS PASSED! (${passed}/${passed})`);
}

main().catch((err) => {
  console.error("M25 browser session manager test FAILED:", err);
  process.exit(1);
});
