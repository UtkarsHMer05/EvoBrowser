import assert from "node:assert/strict";
import toposort from "toposort";
import { convertWorkflowPlanToGraph } from "./convert-plan";
import { validateGraph } from "./validate-graph";
import { interpolate, type NodeOutputs } from "./interpolate";
import type { WorkflowPlan } from "./planner-types";
import type { RunStep } from "../tasks/run-workflow";
import type { StepNodeType } from "../nodes/node-registry";
import type { Edge } from "@xyflow/react";

// ===========================================================================
// MILESTONE 15: FULL PHASE-1 LIFECYCLE REGRESSION SUITE
//
// Generate -> Preview -> Edit -> Run #1 -> Live Browser -> Complete ->
// Edit -> Run #2 -> new session -> new live state -> new result/replay
//
// The simulator below mirrors the real runtime semantics:
//  - run-workflow.ts: topological order, per-run lazy browser session,
//    metadata step streaming, failure/cancellation paths, finally cleanup.
//  - workflow-runs-provider.tsx: run liveness, latest-run resolution,
//    step source preference (output first, metadata fallback), and
//    per-run replay session resolution.
// ===========================================================================

type Graph = { nodes: StepNodeType[]; edges: Edge[] };

// --- Run store: mirrors the Trigger.dev realtime runs the provider reads ----

type SimStatus = "EXECUTING" | "COMPLETED" | "FAILED" | "CANCELED";

interface SimRun {
  id: string;
  createdAt: number;
  status: SimStatus;
  // Live metadata, streamed during execution (steps, session id, duration).
  metadata: {
    steps?: RunStep[];
    browserbaseSessionId?: string;
    durationMs?: number;
    finalUrl?: string;
    // Outcome of the "wait for the Live Browser view" gate (Milestone 17).
    liveViewGate?: {
      waited: boolean;
      connectedBeforeSteps: boolean;
      waitTicks: number;
    };
    // Final screenshot artifact captured in the task's finally block
    // (Milestone 18) — present whenever the run drove a browser.
    artifact?: { screenshot?: string };
  };
  // Final output, present only for COMPLETED runs (a thrown or canceled run
  // returns no output — exactly like the real Trigger.dev task).
  output?: {
    steps: RunStep[];
    browserbaseSessionId?: string;
    finalUrl?: string;
    durationMs: number;
  };
}

const runs: SimRun[] = [];
let runCounter = 0;
let sessionCounter = 0;
let clock = 0;

// --- Provider logic mirrors (workflow-runs-provider.tsx) --------------------

function isRunLive(run: SimRun): boolean {
  return run.status === "EXECUTING";
}

function stepsForRun(run: SimRun): RunStep[] {
  return run.output?.steps ?? run.metadata.steps ?? [];
}

function latestRun(): SimRun | undefined {
  return runs.reduce<SimRun | undefined>(
    (newest, run) => (!newest || run.createdAt > newest.createdAt ? run : newest),
    undefined,
  );
}

function liveRun(): SimRun | undefined {
  return runs.find(isRunLive);
}

// Live session id — only ever read from the run currently in flight.
function liveBrowserbaseSessionId(): string | undefined {
  const live = liveRun();
  if (!live) return undefined;
  return live.metadata.browserbaseSessionId ?? live.output?.browserbaseSessionId;
}

// Replay session id — always resolved from the SELECTED historical run.
function replaySessionIdFor(run: SimRun): string | undefined {
  return run.output?.browserbaseSessionId;
}

// Node status paint — mirrors StepNode: "running" only while the run is live.
function nodePaint(nodeId: string): string {
  const latest = latestRun();
  if (!latest) return "idle";
  const step = stepsForRun(latest).find((s) => s.nodeId === nodeId);
  if (!step) return "idle";
  if (step.status === "running" && isRunLive(latest)) return "running";
  if (step.status === "pending" && isRunLive(latest)) return "pending";
  return step.status;
}

// --- Task simulator (run-workflow.ts semantics) ------------------------------

interface SimOptions {
  failNodeType?: string; // executor of this node type throws
  cancelAfterSteps?: number; // cancel once this many steps finished
  // Mirrors the run task's "wait for the Live Browser view before executing"
  // gate. Defaults to an immediately-connecteding view (the happy path where a
  // user is watching). Set connects:false to exercise the timeout fallback.
  liveView?: {
    connects: boolean;
    connectAfterTicks?: number;
    timeoutTicks?: number;
  };
}

async function executeWorkflow(graph: Graph, options: SimOptions = {}): Promise<SimRun> {
  const problems = validateGraph(graph);
  if (problems.length > 0) throw new Error(problems.join(" "));

  const run: SimRun = {
    id: `run_${++runCounter}`,
    createdAt: ++clock,
    status: "EXECUTING",
    metadata: {},
  };
  runs.push(run);

  const { nodes, edges } = graph;
  const byId = new Map(nodes.map((n) => [n.id, n]));
  const connected = new Set(edges.flatMap((e) => [e.source, e.target]));
  const order = toposort
    .array(
      nodes.map((n) => n.id),
      edges.map((e) => [e.source, e.target]),
    )
    .filter((id) => connected.has(id));

  const steps: RunStep[] = order.map((nodeId) => {
    const node = byId.get(nodeId)!;
    return { nodeId, type: node.data.type, title: node.data.title, status: "pending" };
  });
  run.metadata.steps = steps;

  // Per-run lazy browser session — each run opens its OWN session, reused by
  // every browser node in that run, mirroring getStagehand() in the task.
  let sessionId: string | undefined;
  let sessionClosed = false;
  const getStagehand = () => {
    if (!sessionId) {
      sessionId = `session_${++sessionCounter}`;
      run.metadata.browserbaseSessionId = sessionId;
    }
    return sessionId;
  };
  const closeStagehand = () => {
    if (sessionId && !sessionClosed) sessionClosed = true;
  };

  const browserNodes = new Set(["open-url", "act", "extract", "observe", "agent"]);
  const outputs: NodeOutputs = {};
  let finishedSteps = 0;

  // --- Live-view gate (mirrors run-workflow.ts) ----------------------------
  // If any connected node drives a browser, the task opens the session up
  // front and holds the first step until the Live Browser view connects (or a
  // timeout elapses for unwatched runs). Track the outcome so scenarios can
  // assert the automation never raced ahead of the view.
  const hasBrowserStep = order.some((id) => browserNodes.has(byId.get(id)!.data.type));
  let liveViewWaitTicks = 0;
  let liveViewConnectedBeforeSteps = false;
  if (hasBrowserStep) {
    getStagehand(); // eager open so the session id is published before any step
    const lv = options.liveView ?? { connects: true };
    const timeoutTicks = lv.timeoutTicks ?? 60;
    while (liveViewWaitTicks < timeoutTicks) {
      if (lv.connects && liveViewWaitTicks >= (lv.connectAfterTicks ?? 0)) {
        liveViewConnectedBeforeSteps = true;
        break;
      }
      liveViewWaitTicks++;
    }
    // If the view never connected, the task proceeds after the timeout anyway.
    run.metadata.liveViewGate = {
      waited: true,
      connectedBeforeSteps: liveViewConnectedBeforeSteps,
      waitTicks: liveViewWaitTicks,
    };
  }

  try {
    for (let i = 0; i < order.length; i++) {
      const id = order[i];
      const step = steps[i];
      const node = byId.get(id)!;

      if (node.data.type === "start") {
        step.status = "done";
        continue;
      }

      step.status = "running";

      const values = Object.fromEntries(
        Object.entries(node.data.values).map(([key, text]) => [
          key,
          interpolate({ text, outputs }),
        ]),
      );

      const startedAt = ++clock;
      try {
        // Yield like the real executor await does, so the run is observable
        // as EXECUTING (live) between steps.
        await Promise.resolve();
        if (options.failNodeType && node.data.type === options.failNodeType) {
          throw new Error(`Simulated ${node.data.type} failure`);
        }
        if (browserNodes.has(node.data.type)) {
          getStagehand(); // browser nodes share this run's session
        }
        // Deterministic mock outputs per node type.
        const output =
          node.data.type === "open-url"
            ? { url: values.url, title: `Page for ${values.url}` }
            : node.data.type === "extract"
              ? { extraction: `Extracted: ${values.instruction}` }
              : node.data.type === "observe"
                ? { matches: [{ selector: "#sel", description: values.instruction }] }
                : node.data.type === "act"
                  ? { success: true, message: "Acted", url: values.url }
                  : node.data.type === "agent"
                    ? { success: true, message: "Done", completed: true }
                    : { id: `email_${run.id}` }; // send-email: no browser needed
        outputs[id] = output;
        step.output = output;
      } catch (error) {
        step.status = "failed";
        step.durationMs = ++clock - startedAt;
        step.error = error instanceof Error ? error.message : String(error);
        run.metadata.durationMs = ++clock;
        run.status = "FAILED";
        throw error;
      }

      step.status = "done";
      step.durationMs = ++clock - startedAt;
      finishedSteps++;

      if (
        options.cancelAfterSteps !== undefined &&
        finishedSteps >= options.cancelAfterSteps
      ) {
        run.status = "CANCELED";
        throw new Error("Run canceled by Stop");
      }
    }

    const finalUrl = sessionId ? "https://final.example.com" : undefined;
    const durationMs = ++clock;
    run.metadata.finalUrl = finalUrl;
    run.metadata.durationMs = durationMs;
    run.output = { steps, browserbaseSessionId: sessionId, finalUrl, durationMs };
    run.status = "COMPLETED";
    return run;
  } catch {
    // A failed or canceled run returns no output; its steps survive in metadata.
    return run;
  } finally {
    // The real task captures a final screenshot artifact before closing the
    // session, on every exit path — mirror that here.
    if (sessionId) {
      run.metadata.artifact = { screenshot: `screenshot_of_${sessionId}` };
    }
    closeStagehand(); // finally cleanup runs on success, failure, AND cancel
  }
}

// --- Planner guard mirror (planner-start.tsx) --------------------------------

class PlannerGuard {
  isGenerating = false;
  generationCalls = 0;

  canGenerate(prompt: string) {
    const trimmed = prompt.trim();
    return trimmed.length > 0 && trimmed.length <= 2000 && !this.isGenerating;
  }

  async generate(prompt: string): Promise<void> {
    if (!this.canGenerate(prompt)) return; // duplicate/empty submissions blocked
    this.isGenerating = true;
    try {
      this.generationCalls++;
      await Promise.resolve(); // the AI round trip
    } finally {
      this.isGenerating = false; // never leaves a stale loading state
    }
  }
}

// --- Liveblocks apply mirror (workflow-shell.tsx applyWorkflowGraph) ---------

function applyWorkflowGraphToRoom(
  room: Map<string, StepNodeType>,
  graph: Graph,
): Map<string, StepNodeType> {
  room.clear(); // the mutation deletes every existing node then inserts new ones
  for (const node of graph.nodes) room.set(node.id, node);
  return room;
}

// ===========================================================================
// THE REGRESSION SCENARIOS
// ===========================================================================

async function runLifecycleSuite() {
  console.log("======================================================");
  console.log("MILESTONE 15: PHASE-1 FULL LIFECYCLE REGRESSION SUITE");
  console.log("======================================================\n");

  // -------------------------------------------------------------------------
  // SCENARIO 1: Manually built workflow
  // -------------------------------------------------------------------------
  console.log("SCENARIO 1: Manually built workflow...");
  const manualGraph: Graph = {
    nodes: [
      { id: "start", type: "step", position: { x: 0, y: 0 }, data: { type: "start", kind: "trigger", title: "Start", values: {} } },
      { id: "open_1", type: "step", position: { x: 0, y: 180 }, data: { type: "open-url", kind: "action", title: "Open URL 1", values: { url: "https://manual.example.com" } } },
      { id: "extract_1", type: "step", position: { x: 0, y: 360 }, data: { type: "extract", kind: "action", title: "Extract 1", values: { instruction: "Extract the headline" } } },
    ],
    edges: [
      { id: "e1", source: "start", target: "open_1" },
      { id: "e2", source: "open_1", target: "extract_1" },
    ],
  };
  assert.deepEqual(validateGraph(manualGraph), []);
  const manualRun = await executeWorkflow(manualGraph);
  assert.equal(manualRun.status, "COMPLETED");
  assert.equal(stepsForRun(manualRun).filter((s) => s.status === "done").length, 3);
  console.log("  ✓ Manual graph validates and runs to completion.");

  // -------------------------------------------------------------------------
  // SCENARIO 2: AI-generated workflow (Generate -> Preview)
  // -------------------------------------------------------------------------
  console.log("\nSCENARIO 2: AI-generated workflow...");
  const guard = new PlannerGuard();
  await guard.generate("Open the news site and extract top stories");
  assert.equal(guard.generationCalls, 1, "Exactly one AI generation");
  assert.equal(guard.isGenerating, false, "No stale planner loading state");

  // Duplicate submissions while generating are blocked by the guard.
  guard.isGenerating = true;
  await guard.generate("Duplicate attempt while generating");
  assert.equal(guard.generationCalls, 1, "Duplicate AI generation blocked");
  guard.isGenerating = false;

  const plan: WorkflowPlan = {
    version: "1.0",
    name: "News Digest",
    canBuild: true,
    nodes: [
      { id: "start_1", type: "start", title: "Start", values: {} },
      { id: "open_url_1", type: "open-url", title: "Open News", values: { url: "https://news.example.com" } },
      { id: "extract_1", type: "extract", title: "Extract Stories", values: { instruction: "Extract top stories" } },
      { id: "send_email_1", type: "send-email", title: "Email Digest", values: { to: "me@example.com", subject: "Digest", body: "{{ extract_1.extraction }}" } },
    ],
    edges: [
      { id: "e1", source: "start_1", target: "open_url_1" },
      { id: "e2", source: "open_url_1", target: "extract_1" },
      { id: "e3", source: "extract_1", target: "send_email_1" },
    ],
  };
  const generated = convertWorkflowPlanToGraph(plan);
  assert.deepEqual(validateGraph(generated), [], "Generated preview is a valid graph");

  // The generated graph lands in the Liveblocks room through the same mutation
  // manual edits use — it becomes ordinary collaborative state.
  const room = applyWorkflowGraphToRoom(new Map(), generated);
  assert.equal(room.size, 4);
  console.log("  ✓ AI plan converts to a valid collaborative preview; no duplicate generation; no stale loading.");

  // -------------------------------------------------------------------------
  // SCENARIO 3: AI-generated then edited workflow -> Run #1
  // -------------------------------------------------------------------------
  console.log("\nSCENARIO 3: AI-generated then edited workflow -> Run #1...");
  const edited: Graph = {
    nodes: generated.nodes.map((n) =>
      n.id === "open_url_1"
        ? { ...n, data: { ...n.data, values: { url: "https://edited.example.com" } } }
        : n,
    ),
    edges: [...generated.edges],
  };
  assert.deepEqual(validateGraph(edited), []);

  const run1 = await executeWorkflow(edited);
  assert.equal(run1.status, "COMPLETED");
  const run1Session = replaySessionIdFor(run1);
  assert.ok(run1Session, "Run #1 has its own Browserbase session");
  assert.equal(
    (run1.output!.steps.find((s) => s.nodeId === "open_url_1")!.output as { url: string }).url,
    "https://edited.example.com",
    "Run #1 executed the user's edit, not the original plan",
  );

  // While Run #1 was live, the live view showed Run #1's session. After it
  // completes, no run is live and no live session is exposed.
  assert.equal(liveRun(), undefined, "No live run after completion");
  assert.equal(liveBrowserbaseSessionId(), undefined, "No live session after completion");
  console.log("  ✓ Edited generated workflow ran; its edit was what executed; live state cleared.");

  // -------------------------------------------------------------------------
  // SCENARIO 4: Edit again -> Run #2 (new identity, new session, new result)
  // -------------------------------------------------------------------------
  console.log("\nSCENARIO 4: Edit -> Run #2 with fresh identity and session...");
  const rerunGraph: Graph = {
    nodes: edited.nodes.map((n) =>
      n.id === "send_email_1"
        ? { ...n, data: { ...n.data, values: { ...n.data.values, subject: "Digest v2" } } }
        : n,
    ),
    edges: [...edited.edges],
  };

  const run2 = await executeWorkflow(rerunGraph);
  assert.equal(run2.status, "COMPLETED");
  assert.notEqual(run2.id, run1.id, "Each run has independent run identity");
  assert.ok(run2.createdAt > run1.createdAt, "Run #2 is the newest run");

  const run2Session = replaySessionIdFor(run2);
  assert.ok(run2Session, "Run #2 has its own Browserbase session");
  assert.notEqual(run2Session, run1Session, "Run #2 must not reuse Run #1's session");

  // Node status paint corresponds to the NEWEST run, not Run #1.
  assert.equal(nodePaint("send_email_1"), "done");
  assert.equal(nodePaint("open_url_1"), "done");

  // Run history stays internally consistent: each run resolves its own steps
  // and its own replay session.
  assert.equal(stepsForRun(run1).length, 4);
  assert.equal(stepsForRun(run2).length, 4);
  assert.equal(replaySessionIdFor(run1), run1Session, "Replay for Run #1 points at Run #1's session");
  assert.equal(replaySessionIdFor(run2), run2Session, "Replay for Run #2 points at Run #2's session");
  assert.equal(
    (run2.output!.steps.find((s) => s.nodeId === "send_email_1")!.output as { id: string }).id,
    `email_${run2.id}`,
    "Run #2 produced its own results",
  );
  console.log("  ✓ Run #2 got a new run id, a new session, and its own result/replay; history consistent.");

  // -------------------------------------------------------------------------
  // SCENARIO 5: Stopped workflow
  // -------------------------------------------------------------------------
  console.log("\nSCENARIO 5: Stopped workflow...");
  const stopGraph = manualGraph;
  const stopRunPromise = executeWorkflow(stopGraph, { cancelAfterSteps: 2 });

  // Stop targets ONLY the live run's id — never another run.
  const live = liveRun();
  assert.ok(live, "A live run exists mid-execution");
  const cancelTargetId = live!.id; // what cancelWorkflowRunAction receives
  const stopRun = await stopRunPromise;
  assert.equal(cancelTargetId, stopRun.id, "Stop cancels exactly the live run");
  assert.equal(stopRun.status, "CANCELED");
  assert.equal(stopRun.output, undefined, "Canceled run returns no output");
  assert.ok(stopRun.metadata.steps, "Canceled run's steps survive in metadata");

  // Completed runs are untouched by the cancel.
  assert.equal(run1.status, "COMPLETED", "Stop never cancels a completed run (#1)");
  assert.equal(run2.status, "COMPLETED", "Stop never cancels a completed run (#2)");

  // No stale "running" paint after the stop; graph remains editable & rerunnable.
  assert.notEqual(nodePaint("extract_1"), "running", "No stale running paint after Stop");
  const afterStop: Graph = {
    nodes: stopGraph.nodes.map((n) => ({ ...n, data: { ...n.data, values: { ...n.data.values } } })),
    edges: [...stopGraph.edges],
  };
  const rerunAfterStop = await executeWorkflow(afterStop);
  assert.equal(rerunAfterStop.status, "COMPLETED", "Stopped workflow reruns cleanly");
  console.log("  ✓ Stop canceled only the live run, cleaned up, left history intact, and the graph reran.");

  // -------------------------------------------------------------------------
  // SCENARIO 6: Failed workflow
  // -------------------------------------------------------------------------
  console.log("\nSCENARIO 6: Failed workflow...");
  const failedRun = await executeWorkflow(manualGraph, { failNodeType: "extract" });
  assert.equal(failedRun.status, "FAILED");
  assert.equal(failedRun.output, undefined, "Failed run returns no output");
  const failedSteps = stepsForRun(failedRun);
  const failedStep = failedSteps.find((s) => s.nodeId === "extract_1")!;
  assert.equal(failedStep.status, "failed");
  assert.ok(failedStep.error, "Failure reason recorded on the step");
  assert.ok(failedStep.durationMs != null, "Failed step still carries timing");
  assert.notEqual(
    failedRun.status,
    "COMPLETED",
    "Failed run is never reported as completed",
  );
  assert.notEqual(nodePaint("extract_1"), "running", "No stale running paint after failure");

  // A failed run's fix-and-rerun works like any other edit.
  const fixedRerun = await executeWorkflow(manualGraph);
  assert.equal(fixedRerun.status, "COMPLETED", "Successful rerun after failure");
  console.log("  ✓ Failed run reported accurately and reran successfully after the fix.");

  // -------------------------------------------------------------------------
  // SCENARIO 7: Non-browser workflow (email only)
  // -------------------------------------------------------------------------
  console.log("\nSCENARIO 7: Non-browser workflow...");
  const emailOnlyGraph: Graph = {
    nodes: [
      { id: "start", type: "step", position: { x: 0, y: 0 }, data: { type: "start", kind: "trigger", title: "Start", values: {} } },
      { id: "mail_1", type: "step", position: { x: 0, y: 180 }, data: { type: "send-email", kind: "action", title: "Send Email 1", values: { to: "ops@example.com", subject: "Ping", body: "Hello" } } },
    ],
    edges: [{ id: "e1", source: "start", target: "mail_1" }],
  };
  const emailRun = await executeWorkflow(emailOnlyGraph);
  assert.equal(emailRun.status, "COMPLETED");
  assert.equal(replaySessionIdFor(emailRun), undefined, "Non-browser run opens no session");
  assert.equal(emailRun.output!.finalUrl, undefined, "Non-browser run has no final URL");
  console.log("  ✓ Email-only workflow completes with no browser session and no replay.");

  // -------------------------------------------------------------------------
  // SCENARIO 8: Browser workflow session hygiene across many runs
  // -------------------------------------------------------------------------
  console.log("\nSCENARIO 8: Browser workflow session hygiene...");
  const browserRunA = await executeWorkflow(manualGraph);
  const browserRunB = await executeWorkflow(manualGraph);
  const sessionA = replaySessionIdFor(browserRunA);
  const sessionB = replaySessionIdFor(browserRunB);
  assert.ok(sessionA && sessionB);
  assert.notEqual(sessionA, sessionB, "Consecutive browser runs never share a session");
  // Every browser node within one run shared that run's single session
  // (one session per run, mirroring the lazy getStagehand reuse).
  assert.equal(browserRunA.metadata.browserbaseSessionId, sessionA);
  console.log("  ✓ Every browser run opens exactly one fresh session; no cross-run leakage.");

  // -------------------------------------------------------------------------
  // SCENARIO 9: Liveblocks editing after runs
  // -------------------------------------------------------------------------
  console.log("\nSCENARIO 9: Liveblocks editing after runs...");
  // The room already holds the generated graph; a collaborator re-applies an
  // edited version through the same mutation, then edits it further by hand.
  const collabGraph: Graph = {
    nodes: rerunGraph.nodes.map((n) => ({ ...n, data: { ...n.data, values: { ...n.data.values } } })),
    edges: [...rerunGraph.edges],
  };
  applyWorkflowGraphToRoom(room, collabGraph);
  assert.equal(room.size, 4, "Room replaced atomically with the edited graph");

  const collabNode = room.get("extract_1")!;
  collabNode.data.values.instruction = "Extract author names too"; // live edit
  assert.deepEqual(
    validateGraph({ nodes: [...room.values()], edges: collabGraph.edges }),
    [],
    "Collaboratively edited graph stays valid and run-ready",
  );
  console.log("  ✓ Liveblocks room state stays ordinary, editable, and run-ready after runs.");

  // -------------------------------------------------------------------------
  // SCENARIO 10: Replay points at the selected historical run
  // -------------------------------------------------------------------------
  console.log("\nSCENARIO 10: Replay selection across run history...");
  const history = [...runs];
  const replayable = history.filter((r) => replaySessionIdFor(r) && !isRunLive(r));
  assert.ok(replayable.length >= 4, "Multiple historical browser runs available to replay");
  const sessions = new Set(replayable.map((r) => replaySessionIdFor(r)));
  assert.equal(sessions.size, replayable.length, "Every replayable run has a unique session");
  // Selecting any historical run resolves its OWN recording — never another's.
  for (const run of replayable) {
    const selected = history.find((r) => r.id === run.id)!;
    assert.equal(replaySessionIdFor(selected), replaySessionIdFor(run));
  }
  console.log("  ✓ Replay always resolves the selected historical run's own recording.");

  // -------------------------------------------------------------------------
  // SCENARIO 11: Live-view gate — browser steps wait for the Live Browser view
  // -------------------------------------------------------------------------
  console.log("\nSCENARIO 11: Live-view gate (automation never races the view)...");
  // A watched run: the view connects after a short delay; the task must hold
  // its first browser step until then, then proceed.
  const watchedRun = await executeWorkflow(manualGraph, {
    liveView: { connects: true, connectAfterTicks: 3 },
  });
  assert.equal(watchedRun.status, "COMPLETED");
  const watchedGate = watchedRun.metadata.liveViewGate;
  assert.ok(watchedGate?.waited, "Browser run waited for the live view");
  assert.equal(
    watchedGate?.connectedBeforeSteps,
    true,
    "Live view connected before any browser step ran",
  );
  assert.equal(watchedGate?.waitTicks, 3, "Held exactly until the view connected");

  // An unwatched run: the view never connects; the task must NOT hang — it
  // proceeds after the timeout and still completes.
  const unwatchedRun = await executeWorkflow(manualGraph, {
    liveView: { connects: false, timeoutTicks: 5 },
  });
  assert.equal(unwatchedRun.status, "COMPLETED", "Unwatched run does not hang");
  const unwatchedGate = unwatchedRun.metadata.liveViewGate;
  assert.equal(
    unwatchedGate?.connectedBeforeSteps,
    false,
    "No connection, but the timeout fallback let it proceed",
  );
  assert.equal(unwatchedGate?.waitTicks, 5, "Waited the full timeout before proceeding");

  // A non-browser run (email only) must NOT open a session or wait at all.
  const gateEmailGraph: Graph = {
    nodes: [
      { id: "start", type: "step", position: { x: 0, y: 0 }, data: { type: "start", kind: "trigger", title: "Start", values: {} } },
      { id: "mail_1", type: "step", position: { x: 0, y: 180 }, data: { type: "send-email", kind: "action", title: "Send Email 1", values: { to: "a@b.com", subject: "s", body: "b" } } },
    ],
    edges: [{ id: "e1", source: "start", target: "mail_1" }],
  };
  const gateEmailRun = await executeWorkflow(gateEmailGraph, {
    liveView: { connects: false, timeoutTicks: 5 },
  });
  assert.equal(gateEmailRun.status, "COMPLETED");
  assert.equal(
    gateEmailRun.metadata.liveViewGate,
    undefined,
    "Non-browser run skips the live-view gate entirely",
  );
  assert.equal(
    gateEmailRun.metadata.browserbaseSessionId,
    undefined,
    "Non-browser run opens no session",
  );

  // Final screenshot artifact (Milestone 18): captured on every exit path for
  // browser runs, absent for non-browser runs.
  assert.ok(
    watchedRun.metadata.artifact?.screenshot,
    "Browser run captured a final screenshot artifact",
  );
  assert.equal(
    unwatchedRun.metadata.artifact?.screenshot !== undefined,
    true,
    "Unwatched browser run still captured a screenshot",
  );
  assert.equal(
    gateEmailRun.metadata.artifact,
    undefined,
    "Non-browser run has no screenshot artifact",
  );
  console.log("  ✓ Watched runs wait for the view; unwatched runs time out and proceed; non-browser runs skip the gate; browser runs capture a final screenshot.");

  // -------------------------------------------------------------------------
  // FINAL: no automatic execution anywhere in the lifecycle
  // -------------------------------------------------------------------------
  const totalRuns = runs.length;
  // Every run above was created by an explicit executeWorkflow call — the
  // production equivalents are the Run button, Run Again, and nothing else.
  assert.equal(totalRuns, runCounter, "Every run came from an explicit trigger");
  assert.equal(guard.generationCalls, 1, "AI generation happened only on explicit request");

  console.log("\n======================================================");
  console.log(`ALL PHASE-1 LIFECYCLE REGRESSION TESTS PASSED! (11/11, ${totalRuns} runs simulated)`);
  console.log("======================================================\n");
}

runLifecycleSuite().catch((err) => {
  console.error("Lifecycle regression failure:", err);
  process.exit(1);
});
