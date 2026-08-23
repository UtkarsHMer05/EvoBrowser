// Unit tests for POST /api/live-view/[sessionId]/connected — the live-view
// handshake write. The run task holds its first browser step until this lands,
// so the engine-specific write target matters: legacy runs poll Neon
// (markLiveViewConnected), Evo runs poll the Phase-2 store
// (markEvoLiveViewConnected). This suite pins that routing plus the same
// auth/ownership bar as every other per-run route.
//
// vitest-only (vi.mock); every dependency mocked, no local infra.

import { describe, expect, it, vi, beforeEach } from "vitest";

const readAuthWithRetryMock = vi.hoisted(() => vi.fn());
const resolveActiveOrgIdMock = vi.hoisted(() => vi.fn());
const authorizeMock = vi.hoisted(() => vi.fn());
const markLegacyMock = vi.hoisted(() => vi.fn());
const markEvoMock = vi.hoisted(() => vi.fn());

vi.mock("@sentry/nextjs", () => ({
  getIsolationScope: () => ({ setAttributes: vi.fn() }),
  logger: { warn: vi.fn(), info: vi.fn(), error: vi.fn() },
}));

vi.mock("@/lib/auth", () => ({
  readAuthWithRetry: readAuthWithRetryMock,
  resolveActiveOrgId: resolveActiveOrgIdMock,
}));

vi.mock("@/features/workflows/lib/run-authorization", () => ({
  authorizeRunAccess: authorizeMock,
}));

vi.mock("@/features/workflows/data", () => ({
  markLiveViewConnected: markLegacyMock,
}));

vi.mock("@/features/workflows/lib/evo-run-data", () => ({
  markEvoLiveViewConnected: markEvoMock,
}));

import { POST } from "./route";

const ORG = "org_handshake";
const RUN = "run_def456";
const SESSION = "sess_handshake1";

function call(sessionId = SESSION) {
  const request = new Request(
    `http://localhost/api/live-view/${sessionId}/connected?runId=${RUN}`,
    { method: "POST" },
  );
  return POST(request, { params: Promise.resolve({ sessionId }) });
}

beforeEach(() => {
  vi.clearAllMocks();
  readAuthWithRetryMock.mockResolvedValue({ userId: "user_1" });
  resolveActiveOrgIdMock.mockResolvedValue(ORG);
});

describe("POST /api/live-view/[sessionId]/connected", () => {
  it("returns 401 for an unauthenticated caller and writes nothing", async () => {
    readAuthWithRetryMock.mockResolvedValue({ userId: undefined });

    const res = await call();

    expect(res.status).toBe(401);
    expect(markLegacyMock).not.toHaveBeenCalled();
    expect(markEvoMock).not.toHaveBeenCalled();
  });

  it("returns 400 when the runId query parameter is missing", async () => {
    const request = new Request(
      `http://localhost/api/live-view/${SESSION}/connected`,
      { method: "POST" },
    );
    const res = await POST(request, {
      params: Promise.resolve({ sessionId: SESSION }),
    });

    expect(res.status).toBe(400);
  });

  it("maps a denied access outcome onto 403/404", async () => {
    authorizeMock.mockResolvedValue({
      ok: false,
      status: 403,
      reason: "session does not belong to run",
    });

    const denied = await call();

    expect(denied.status).toBe(403);

    authorizeMock.mockResolvedValue({ ok: false, status: 404 });
    const missing = await call();

    expect(missing.status).toBe(404);
    expect(markLegacyMock).not.toHaveBeenCalled();
    expect(markEvoMock).not.toHaveBeenCalled();
  });

  it("writes the Neon handshake for a legacy run", async () => {
    authorizeMock.mockResolvedValue({ ok: true, engine: "legacy" });

    const res = await call();

    expect(res.status).toBe(200);
    expect(await res.json()).toEqual({ connected: true });
    expect(markLegacyMock).toHaveBeenCalledWith(SESSION, RUN);
    expect(markEvoMock).not.toHaveBeenCalled();
  });

  it("writes the Phase-2 handshake for an evo run", async () => {
    authorizeMock.mockResolvedValue({ ok: true, engine: "evo" });

    const res = await call();

    expect(res.status).toBe(200);
    expect(await res.json()).toEqual({ connected: true });
    expect(markEvoMock).toHaveBeenCalledWith(SESSION, RUN);
    expect(markLegacyMock).not.toHaveBeenCalled();
  });
});
