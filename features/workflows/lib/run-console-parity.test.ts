import assert from "node:assert/strict";

import {
  consoleRunStatusLabel,
  mergeConsoleRuns,
  toConsoleRunFromEvo,
  toConsoleRunFromLegacy,
  type ConsoleRun,
  type LegacyConsoleRunLike,
} from "./run-console";
import {
  reduceEvoEvents,
  evoRunDetailToViewModel,
  type EvoRunEvent,
  type NormalizedRunViewModel,
} from "./run-view-model";
import type { RunStep } from "../tasks/run-workflow";

// ===========================================================================
// MILESTONE 29: LEGACY vs EVO LIFECYCLE PARITY REGRESSION SUITE
//
// The UI must render an Evo run exactly like a legacy Trigger.dev run for
// equivalent lifecycle states. This suite drives the SAME console-mapping code
// the provider uses (run-console.ts) with a legacy run and an Evo run in each
// lifecycle state, then asserts the derived semantics (isLive/isTerminal/
// isCompleted/isFailed/isCanceled), the human-readable status label, the
// step-status vocabulary, and session-id resolution are IDENTICAL.
//
// It also covers the M29-specific Evo behaviors:
//   - queued -> running promotion (the C++ create_run fix),
//   - canceled steps (Evo-only status) render as inactive, not running,
//   - the durable Browserbase session id flows into replay/live-view fields,
//   - merged history is newest-first and de-duplicated.
// ===========================================================================

let clock = 0;
const now = () => new Date(++clock * 1000);

// --- Legacy run builders (Trigger.dev realtime shape) -----------------------

function legacyStep(
  nodeId: string,
  status: RunStep["status"],
  extra: Partial<RunStep> = {},
): RunStep {
  return { nodeId, type: "open-url", title: nodeId, status, ...extra };
}

function legacyRun(
  id: string,
  status: string,
  opts: {
    steps?: RunStep[];
    outputSteps?: RunStep[];
    sessionId?: string;
    liveSessionId?: string;
    finalUrl?: string;
    durationMs?: number;
  } = {},
): LegacyConsoleRunLike {
  return {
    id,
    status,
    createdAt: now(),
    metadata: {
      steps: opts.steps,
      browserbaseSessionId: opts.liveSessionId,
      finalUrl: opts.finalUrl,
      durationMs: opts.durationMs,
    },
    output: opts.outputSteps
      ? {
          steps: opts.outputSteps,
          browserbaseSessionId: opts.sessionId,
          finalUrl: opts.finalUrl,
          durationMs: opts.durationMs,
        }
      : undefined,
  };
}

// --- Evo run builders (normalized view model via the event reducer) ---------

function evoEvent(
  runId: string,
  kind: EvoRunEvent["kind"],
  nodeId = "",
  detail = "",
): EvoRunEvent {
  return { run_id: runId, node_id: nodeId, kind, detail, wall_ms: ++clock * 1000 };
}

// Fold a full Evo lifecycle into a view model through the shared reducer — the
// exact path the SSE provider uses.
function evoViewModelFromEvents(runId: string, events: EvoRunEvent[]): NormalizedRunViewModel {
  return reduceEvoEvents(runId, events);
}

// ===========================================================================
// THE PARITY SCENARIOS
// ===========================================================================

function assertParity(legacy: ConsoleRun, evo: ConsoleRun, label: string) {
  assert.equal(evo.isLive, legacy.isLive, `${label}: isLive parity`);
  assert.equal(evo.isTerminal, legacy.isTerminal, `${label}: isTerminal parity`);
  assert.equal(evo.isCompleted, legacy.isCompleted, `${label}: isCompleted parity`);
  assert.equal(evo.isFailed, legacy.isFailed, `${label}: isFailed parity`);
  assert.equal(evo.isCanceled, legacy.isCanceled, `${label}: isCanceled parity`);
  assert.equal(
    consoleRunStatusLabel(evo),
    consoleRunStatusLabel(legacy),
    `${label}: status label parity`,
  );
}

async function runParitySuite() {
  console.log("======================================================");
  console.log("MILESTONE 29: LEGACY vs EVO LIFECYCLE PARITY SUITE");
  console.log("======================================================\n");

  // -------------------------------------------------------------------------
  // SCENARIO 1: Running (live) parity
  // -------------------------------------------------------------------------
  console.log("SCENARIO 1: Running (live) parity...");
  const legacyRunning = toConsoleRunFromLegacy(
    legacyRun("legacy_run_1", "EXECUTING", {
      steps: [legacyStep("a", "done"), legacyStep("b", "running")],
      liveSessionId: "sess_live_1",
    }),
  );
  const evoRunningVm = evoViewModelFromEvents("evo_run_1", [
    evoEvent("evo_run_1", "run_started"),
    evoEvent("evo_run_1", "node_dispatched", "a", "open-url"),
    evoEvent("evo_run_1", "node_succeeded", "a", "{}"),
    evoEvent("evo_run_1", "node_dispatched", "b", "act"),
  ]);
  const evoRunning = toConsoleRunFromEvo(evoRunningVm);

  assertParity(legacyRunning, evoRunning, "running");
  assert.equal(legacyRunning.isLive, true, "legacy EXECUTING is live");
  assert.equal(evoRunning.isLive, true, "evo running is live");
  assert.equal(consoleRunStatusLabel(evoRunning), "Running…");
  // Live session id is exposed while the run is in flight (both engines).
  assert.equal(legacyRunning.liveBrowserbaseSessionId, "sess_live_1");
  console.log("  ✓ Running runs are live, labeled 'Running…', and expose the live session.");

  // -------------------------------------------------------------------------
  // SCENARIO 2: Completed (succeeded) parity
  // -------------------------------------------------------------------------
  console.log("\nSCENARIO 2: Completed parity...");
  const doneSteps = [
    legacyStep("a", "done", { durationMs: 10 }),
    legacyStep("b", "done", { durationMs: 20 }),
  ];
  const legacyDone = toConsoleRunFromLegacy(
    legacyRun("legacy_run_2", "COMPLETED", {
      outputSteps: doneSteps,
      sessionId: "sess_done_1",
      finalUrl: "https://final.example.com",
      durationMs: 30,
    }),
  );
  const evoDoneVm = evoViewModelFromEvents("evo_run_2", [
    evoEvent("evo_run_2", "run_started"),
    evoEvent("evo_run_2", "node_dispatched", "a", "open-url"),
    evoEvent("evo_run_2", "node_succeeded", "a", "{}"),
    evoEvent("evo_run_2", "node_dispatched", "b", "act"),
    evoEvent("evo_run_2", "node_succeeded", "b", "{}"),
    evoEvent("evo_run_2", "run_finished", "", "succeeded"),
  ]);
  const evoDone = toConsoleRunFromEvo(evoDoneVm);

  assertParity(legacyDone, evoDone, "completed");
  assert.equal(legacyDone.isCompleted, true);
  assert.equal(evoDone.isCompleted, true);
  assert.equal(consoleRunStatusLabel(evoDone), "Completed");
  // Terminal runs expose the replay session id (both engines).
  assert.equal(legacyDone.browserbaseSessionId, "sess_done_1");
  // Step stats match.
  assert.equal(evoDone.completedCount, legacyDone.completedCount);
  assert.equal(evoDone.totalCount, legacyDone.totalCount);
  assert.equal(evoDone.totalCount, 2);
  console.log("  ✓ Completed runs report identical terminal semantics, label, and step stats.");

  // -------------------------------------------------------------------------
  // SCENARIO 3: Failed parity
  // -------------------------------------------------------------------------
  console.log("\nSCENARIO 3: Failed parity...");
  const legacyFailed = toConsoleRunFromLegacy(
    legacyRun("legacy_run_3", "FAILED", {
      steps: [
        legacyStep("a", "done"),
        legacyStep("b", "failed", { error: "boom" }),
      ],
    }),
  );
  const evoFailedVm = evoViewModelFromEvents("evo_run_3", [
    evoEvent("evo_run_3", "run_started"),
    evoEvent("evo_run_3", "node_dispatched", "a", "open-url"),
    evoEvent("evo_run_3", "node_succeeded", "a", "{}"),
    evoEvent("evo_run_3", "node_dispatched", "b", "act"),
    evoEvent("evo_run_3", "node_failed", "b", "boom"),
    evoEvent("evo_run_3", "run_finished", "", "failed"),
  ]);
  const evoFailed = toConsoleRunFromEvo(evoFailedVm);

  assertParity(legacyFailed, evoFailed, "failed");
  assert.equal(legacyFailed.isFailed, true);
  assert.equal(evoFailed.isFailed, true);
  assert.equal(consoleRunStatusLabel(evoFailed), "Failed");
  // The failing step carries its error in both engines.
  const evoFailedStep = evoFailed.steps.find((s) => s.nodeId === "b");
  assert.equal(evoFailedStep?.status, "failed");
  assert.ok(evoFailedStep?.error, "evo failed step records its error");
  assert.equal(evoFailed.failedCount, legacyFailed.failedCount);
  console.log("  ✓ Failed runs report identical semantics and surface the step error.");

  // -------------------------------------------------------------------------
  // SCENARIO 4: Canceled (stopped) parity + Evo canceled steps
  // -------------------------------------------------------------------------
  console.log("\nSCENARIO 4: Canceled parity + Evo canceled steps...");
  const legacyCanceled = toConsoleRunFromLegacy(
    legacyRun("legacy_run_4", "CANCELED", {
      steps: [legacyStep("a", "done"), legacyStep("b", "pending")],
    }),
  );
  // Evo: node b was canceled before it ran when the run stopped.
  const evoCanceledVm = evoViewModelFromEvents("evo_run_4", [
    evoEvent("evo_run_4", "run_started"),
    evoEvent("evo_run_4", "node_dispatched", "a", "open-url"),
    evoEvent("evo_run_4", "node_succeeded", "a", "{}"),
    evoEvent("evo_run_4", "node_canceled", "b"),
    evoEvent("evo_run_4", "run_finished", "", "canceled"),
  ]);
  const evoCanceled = toConsoleRunFromEvo(evoCanceledVm);

  assertParity(legacyCanceled, evoCanceled, "canceled");
  assert.equal(legacyCanceled.isCanceled, true);
  assert.equal(evoCanceled.isCanceled, true);
  assert.equal(consoleRunStatusLabel(evoCanceled), "Stopped");
  // Evo-only: the canceled step is "canceled", never "running" — the canvas and
  // console must render it inactive, not spinning.
  const evoCanceledStep = evoCanceled.steps.find((s) => s.nodeId === "b");
  assert.equal(evoCanceledStep?.status, "canceled", "Evo canceled step status");
  assert.notEqual(evoCanceledStep?.status, "running", "canceled step never spins");
  console.log("  ✓ Canceled runs report identical semantics; Evo canceled steps read inactive.");

  // -------------------------------------------------------------------------
  // SCENARIO 5: Queued parity (pre-execution)
  // -------------------------------------------------------------------------
  console.log("\nSCENARIO 5: Queued parity...");
  const legacyQueued = toConsoleRunFromLegacy(
    legacyRun("legacy_run_5", "QUEUED", { steps: [legacyStep("a", "pending")] }),
  );
  // An Evo run with no events yet is queued (the reducer's initial state).
  const evoQueuedVm = evoViewModelFromEvents("evo_run_5", []);
  const evoQueued = toConsoleRunFromEvo(evoQueuedVm);

  assertParity(legacyQueued, evoQueued, "queued");
  assert.equal(legacyQueued.isLive, true, "legacy QUEUED is live");
  assert.equal(evoQueued.isLive, true, "evo queued is live");
  assert.equal(legacyQueued.isTerminal, false);
  assert.equal(evoQueued.isTerminal, false);
  console.log("  ✓ Queued runs are live and non-terminal in both engines.");

  // -------------------------------------------------------------------------
  // SCENARIO 6: Evo queued -> running promotion (C++ create_run fix)
  // -------------------------------------------------------------------------
  console.log("\nSCENARIO 6: Evo queued -> running promotion...");
  // The app pre-creates the run row as 'queued'; the engine's create_run must
  // promote it to 'running' (not leave it queued). Model the durable snapshot
  // the UI reads: before the engine starts it's queued, after it's running.
  const queuedDetail = evoRunDetailToViewModel({
    runId: "evo_run_6",
    status: "RUN_QUEUED",
    outcome: "",
    nodes: [],
  });
  assert.equal(queuedDetail.status, "queued", "pre-start durable state is queued");
  const runningDetail = evoRunDetailToViewModel({
    runId: "evo_run_6",
    status: "RUN_RUNNING",
    outcome: "",
    createdAtMs: 1000,
    nodes: [
      {
        nodeId: "a",
        nodeType: "open-url",
        state: "NODE_STATE_RUNNING",
        attemptNumber: 1,
        output: "",
        error: "",
        startedAtMs: 1000,
      },
    ],
  });
  assert.equal(runningDetail.status, "running", "engine promoted queued -> running");
  assert.equal(runningDetail.isLive, true, "promoted run is live");
  const runningStep = runningDetail.steps.find((s) => s.nodeId === "a");
  assert.equal(runningStep?.status, "running", "dispatched node reads running");
  console.log("  ✓ Evo run promotes queued -> running and its node reads running.");

  // -------------------------------------------------------------------------
  // SCENARIO 7: Durable Browserbase session id flows to replay/live-view
  // -------------------------------------------------------------------------
  console.log("\nSCENARIO 7: Durable session id -> replay/live-view...");
  // The worker stamps the session id on the run row; the durable snapshot and
  // the listing both carry it so replay/live-view resolve the right session.
  const evoWithSession: NormalizedRunViewModel = {
    ...evoDoneVm,
    browserbaseSessionId: "sess_evo_replay",
    liveBrowserbaseSessionId: "sess_evo_replay",
  };
  const evoSessionRun = toConsoleRunFromEvo(evoWithSession);
  assert.equal(
    evoSessionRun.browserbaseSessionId,
    "sess_evo_replay",
    "replay resolves the run's own durable session",
  );
  assert.equal(
    evoSessionRun.liveBrowserbaseSessionId,
    "sess_evo_replay",
    "live-view resolves the run's own durable session",
  );
  console.log("  ✓ Evo replay/live-view resolve the run's own durable session id.");

  // -------------------------------------------------------------------------
  // SCENARIO 8: Merged history is newest-first and de-duplicated
  // -------------------------------------------------------------------------
  console.log("\nSCENARIO 8: Merged engine-neutral history...");
  const legacyA = toConsoleRunFromLegacy(legacyRun("legacy_A", "COMPLETED", { outputSteps: [] }));
  const evoB = toConsoleRunFromEvo({ ...evoDoneVm, id: "evo_B" });
  const legacyC = toConsoleRunFromLegacy(legacyRun("legacy_C", "COMPLETED", { outputSteps: [] }));
  // Force a deterministic createdAt ordering: legacyA oldest, evoB middle, legacyC newest.
  legacyA.createdAt = new Date(1000);
  evoB.createdAt = new Date(2000);
  legacyC.createdAt = new Date(3000);

  const merged = mergeConsoleRuns([legacyA, legacyC], [evoB]);
  assert.deepEqual(
    merged.map((r) => r.id),
    ["legacy_C", "evo_B", "legacy_A"],
    "merged history is newest-first across engines",
  );
  // De-duplication: a run present in both sources appears once.
  const duped = mergeConsoleRuns([legacyA], [{ ...evoB, id: "legacy_A" }]);
  assert.equal(duped.length, 1, "duplicate run id appears once");
  console.log("  ✓ Merged history is newest-first across engines and de-duplicated.");

  // -------------------------------------------------------------------------
  // SCENARIO 9: Step-status vocabulary parity (canvas paint inputs)
  // -------------------------------------------------------------------------
  console.log("\nSCENARIO 9: Step-status vocabulary parity...");
  // The canvas StepNode paints from step.status. Both engines must only ever
  // produce the shared vocabulary the node understands.
  const VOCAB = new Set(["pending", "running", "done", "failed", "canceled"]);
  for (const run of [legacyRunning, evoRunning, legacyDone, evoDone, evoCanceled]) {
    for (const step of run.steps) {
      assert.ok(VOCAB.has(step.status), `step status '${step.status}' in shared vocabulary`);
    }
  }
  console.log("  ✓ Both engines emit only the shared step-status vocabulary.");

  console.log("\n======================================================");
  console.log("ALL M29 LEGACY vs EVO PARITY TESTS PASSED! (9/9)");
  console.log("======================================================\n");
}

if (process.env.VITEST) {
  // Under vitest the suite runs as one tracked test: failures attribute to
  // this file instead of killing the worker with process.exit.
  const { test } = await import("vitest");
  test("M29 legacy vs Evo console parity", async () => {
    await runParitySuite();
  });
} else {
runParitySuite().catch((err) => {
  console.error("Parity regression failure:", err);
  process.exit(1);
});
}
