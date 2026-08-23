// Unit tests for the shared engine-aware run authorization gate
// (features/workflows/lib/run-authorization.ts).
//
// This helper is the ONE definition of "this caller owns this run" enforced by
// every per-run artifact route (live view, handshake, screenshot, replay), so
// every branch is pinned here:
//   legacy path  — Trigger.dev payload orgId + defense-in-depth workflow row +
//                  session match (output first, metadata fallback)
//   evo path     — Phase-2 Postgres run row orgId + stamped session id
//   fallthrough  — retrieve throwing resolves to the evo lookup; unknown runs
//                  deny with 404 so cross-tenant existence is not leaked
//
// Unlike the scenario suites in this directory, these are vitest-only unit
// tests: vi.mock requires the runner, so the file cannot run standalone via
// tsx. Every dependency is mocked — no local infra needed.

import { describe, expect, it, vi, beforeEach, afterEach } from "vitest";

const retrieveMock = vi.hoisted(() => vi.fn());
const getWorkflowMock = vi.hoisted(() => vi.fn());
const getEvoRunOrgIdMock = vi.hoisted(() => vi.fn());
const getEvoRunBrowserbaseSessionIdMock = vi.hoisted(() => vi.fn());
const sentryWarnMock = vi.hoisted(() => vi.fn());

vi.mock("@sentry/nextjs", () => ({
  logger: { warn: sentryWarnMock, info: vi.fn(), error: vi.fn() },
}));

vi.mock("@trigger.dev/sdk", () => ({
  runs: { retrieve: retrieveMock },
}));

vi.mock("@/features/workflows/data", () => ({
  getWorkflow: getWorkflowMock,
}));

vi.mock("@/features/workflows/lib/evo-run-data", () => ({
  getEvoRunOrgId: getEvoRunOrgIdMock,
  getEvoRunBrowserbaseSessionId: getEvoRunBrowserbaseSessionIdMock,
}));

import { authorizeRunAccess } from "./run-authorization";

const ORG = "org_2abcDEF123";
const OTHER_ORG = "org_other999";
const RUN = "run_abcdef123456";
const SESSION = "sess_browserbase001";

function legacyRun(overrides: {
  payload?: Record<string, unknown>;
  output?: Record<string, unknown>;
  metadata?: Record<string, unknown>;
} = {}) {
  return {
    payload: overrides.payload ?? { orgId: ORG, workflowId: "wf-uuid" },
    output: overrides.output ?? {},
    metadata: overrides.metadata ?? {},
  };
}

beforeEach(() => {
  vi.clearAllMocks();
});

afterEach(() => {
  vi.restoreAllMocks();
});

describe("authorizeRunAccess — legacy engine", () => {
  it("allows an org-owned run with no session check", async () => {
    retrieveMock.mockResolvedValue(legacyRun());
    getWorkflowMock.mockResolvedValue({ id: "wf-uuid", orgId: ORG });

    const result = await authorizeRunAccess({ orgId: ORG, runId: RUN });

    expect(result).toEqual({ ok: true, engine: "legacy" });
    expect(getWorkflowMock).toHaveBeenCalledWith(ORG, "wf-uuid");
    expect(getEvoRunOrgIdMock).not.toHaveBeenCalled();
  });

  it("denies when the run payload belongs to another org (403)", async () => {
    retrieveMock.mockResolvedValue(
      legacyRun({ payload: { orgId: OTHER_ORG, workflowId: "wf-uuid" } }),
    );

    const result = await authorizeRunAccess({ orgId: ORG, runId: RUN });

    expect(result).toEqual({
      ok: false,
      status: 403,
      reason: "run belongs to another org",
    });
    // Cross-org probe must stop before any workflow lookup.
    expect(getWorkflowMock).not.toHaveBeenCalled();
  });

  it("denies when the workflow no longer exists in the org (403)", async () => {
    retrieveMock.mockResolvedValue(legacyRun());
    getWorkflowMock.mockResolvedValue(undefined);

    const result = await authorizeRunAccess({ orgId: ORG, runId: RUN });

    expect(result).toMatchObject({ ok: false, status: 403 });
  });

  it("denies when a requested session does not match the run's output session (403)", async () => {
    retrieveMock.mockResolvedValue(
      legacyRun({ output: { browserbaseSessionId: "sess_actual" } }),
    );
    getWorkflowMock.mockResolvedValue({ id: "wf-uuid" });

    const result = await authorizeRunAccess({
      orgId: ORG,
      runId: RUN,
      sessionId: SESSION,
    });

    expect(result).toMatchObject({
      ok: false,
      status: 403,
      reason: "session does not belong to run",
    });
  });

  it("falls back to live metadata for the session match while executing", async () => {
    retrieveMock.mockResolvedValue(
      legacyRun({
        output: {}, // still running: final output has no session yet
        metadata: { browserbaseSessionId: SESSION },
      }),
    );
    getWorkflowMock.mockResolvedValue({ id: "wf-uuid" });

    const result = await authorizeRunAccess({
      orgId: ORG,
      runId: RUN,
      sessionId: SESSION,
    });

    expect(result).toEqual({ ok: true, engine: "legacy" });
  });

  it("skips the session check when no sessionId is supplied", async () => {
    retrieveMock.mockResolvedValue(legacyRun({ output: {} }));
    getWorkflowMock.mockResolvedValue({ id: "wf-uuid" });

    const result = await authorizeRunAccess({
      orgId: ORG,
      runId: RUN,
      sessionId: undefined,
    });

    expect(result).toEqual({ ok: true, engine: "legacy" });
  });
});

describe("authorizeRunAccess — evo engine", () => {
  it("resolves ownership from the Phase-2 store when Trigger.dev does not know the run", async () => {
    retrieveMock.mockRejectedValue(new Error("not found in trigger"));
    getEvoRunOrgIdMock.mockResolvedValue(ORG);
    getEvoRunBrowserbaseSessionIdMock.mockResolvedValue(SESSION);

    const result = await authorizeRunAccess({
      orgId: ORG,
      runId: RUN,
      sessionId: SESSION,
    });

    expect(result).toEqual({ ok: true, engine: "evo" });
    expect(getWorkflowMock).not.toHaveBeenCalled();
  });

  it("returns 404 for a completely unknown run", async () => {
    retrieveMock.mockRejectedValue(new Error("not found"));
    getEvoRunOrgIdMock.mockResolvedValue(undefined);

    const result = await authorizeRunAccess({
      orgId: ORG,
      runId: "run_ghost",
      sessionId: SESSION,
    });

    expect(result).toMatchObject({ ok: false, status: 404 });
    // Existence of other-org runs must not leak through the session lookup.
    expect(getEvoRunBrowserbaseSessionIdMock).not.toHaveBeenCalled();
  });

  it("denies an evo run owned by another org (403)", async () => {
    retrieveMock.mockRejectedValue(new Error("not found"));
    getEvoRunOrgIdMock.mockResolvedValue(OTHER_ORG);

    const result = await authorizeRunAccess({ orgId: ORG, runId: RUN });

    expect(result).toMatchObject({
      ok: false,
      status: 403,
      reason: "run belongs to another org",
    });
  });

  it("denies when the evo run's stamped session differs (403)", async () => {
    retrieveMock.mockRejectedValue(new Error("not found"));
    getEvoRunOrgIdMock.mockResolvedValue(ORG);
    getEvoRunBrowserbaseSessionIdMock.mockResolvedValue("sess_other_run");

    const result = await authorizeRunAccess({
      orgId: ORG,
      runId: RUN,
      sessionId: SESSION,
    });

    expect(result).toMatchObject({
      ok: false,
      status: 403,
      reason: "session does not belong to run",
    });
  });

  it("treats an unreachable Phase-2 store like an unknown run (404)", async () => {
    retrieveMock.mockRejectedValue(new Error("not found"));
    // The real evo-run-data helpers are fail-open: a store outage resolves to
    // undefined rather than rejecting, so the gate sees "run not found".
    getEvoRunOrgIdMock.mockResolvedValue(undefined);

    const result = await authorizeRunAccess({ orgId: ORG, runId: RUN });

    expect(result).toMatchObject({ ok: false, status: 404 });
  });
});

describe("authorizeRunAccess — audit trail", () => {
  it("logs a warning on every denial", async () => {
    retrieveMock.mockResolvedValue(
      legacyRun({ payload: { orgId: OTHER_ORG, workflowId: "wf-uuid" } }),
    );

    await authorizeRunAccess({ orgId: ORG, runId: RUN });

    expect(sentryWarnMock).toHaveBeenCalledTimes(1);
    expect(sentryWarnMock).toHaveBeenCalledWith(
      expect.stringContaining("Run access denied"),
      expect.objectContaining({
        runId: RUN,
        orgId: ORG,
        reason: "run belongs to another org",
      }),
    );
  });

  it("denies a malformed legacy run whose payload carries no orgId", async () => {
    retrieveMock.mockResolvedValue(legacyRun({ payload: { workflowId: "wf" } }));

    const result = await authorizeRunAccess({ orgId: ORG, runId: RUN });

    expect(result).toMatchObject({ ok: false, status: 403 });
    expect(getWorkflowMock).not.toHaveBeenCalled();
  });
});
