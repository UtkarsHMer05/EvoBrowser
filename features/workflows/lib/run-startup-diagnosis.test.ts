import assert from "node:assert/strict";
import { test } from "vitest";

import {
  diagnoseRunStartup,
  STARTUP_STUCK_AFTER_MS,
} from "./run-startup-diagnosis";

const NOW = 1_000_000;

function liveRun(overrides: Partial<Parameters<typeof diagnoseRunStartup>[0]["latest"]> = {}) {
  return {
    id: "run_test",
    engine: "legacy" as const,
    isLive: true,
    stepCount: 0,
    createdAtMs: NOW - STARTUP_STUCK_AFTER_MS, // exactly at the threshold
    ...overrides,
  };
}

test("healthy states produce no diagnosis", () => {
  // No runs at all.
  assert.equal(diagnoseRunStartup({ nowMs: NOW }).kind, "none");

  // Live run with steps already publishing.
  assert.equal(
    diagnoseRunStartup({
      latest: liveRun({ stepCount: 3 }),
      nowMs: NOW,
    }).kind,
    "none",
  );

  // Live run still inside the grace window.
  assert.equal(
    diagnoseRunStartup({
      latest: liveRun({ createdAtMs: NOW - 5_000 }),
      nowMs: NOW,
    }).kind,
    "none",
  );

  // Finished runs are never "stuck".
  assert.equal(
    diagnoseRunStartup({
      latest: liveRun({ isLive: false, createdAtMs: NOW - 999_999 }),
      nowMs: NOW,
    }).kind,
    "none",
  );
});

test("a live run with zero steps past the grace window is diagnosed", () => {
  const d = diagnoseRunStartup({
    latest: liveRun({ createdAtMs: NOW - (STARTUP_STUCK_AFTER_MS + 10_000) }),
    nowMs: NOW,
  });
  assert.equal(d.kind, "not-picked-up");
  if (d.kind === "not-picked-up") {
    assert.equal(d.engine, "legacy");
    assert.ok(d.seconds >= 55);
  }

  const evo = diagnoseRunStartup({
    latest: liveRun({ engine: "evo", createdAtMs: NOW - (STARTUP_STUCK_AFTER_MS + 1) }),
    nowMs: NOW,
  });
  if (evo.kind === "not-picked-up") assert.equal(evo.engine, "evo");
  else assert.fail("expected not-picked-up for evo");
});

test("connection errors take precedence over the stuck-run diagnosis", () => {
  const d = diagnoseRunStartup({
    latest: liveRun(),
    connectionErrorMessage: "socket closed",
    nowMs: NOW,
  });
  assert.deepEqual(d, { kind: "connection-error", message: "socket closed" });

  // A connection error diagnoses even without any runs.
  assert.equal(
    diagnoseRunStartup({ connectionErrorMessage: "boom", nowMs: NOW }).kind,
    "connection-error",
  );
});
