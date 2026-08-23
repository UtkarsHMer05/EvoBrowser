// Unit tests for GET /api/live-view/[sessionId] — the Browserbase live-view
// debug-URL proxy. The secret-key call happens server-side; this suite pins
// the response contract around the shared authorizeRunAccess gate:
//   401 unauthenticated / no active org · 400 missing runId ·
//   403/404 mapped from the access outcome · 200 with only the two debug URLs ·
//   404 when Browserbase cannot serve the session.
//
// vitest-only (vi.mock); every dependency mocked, no local infra.

import { describe, expect, it, vi, beforeEach } from "vitest";

const readAuthWithRetryMock = vi.hoisted(() => vi.fn());
const resolveActiveOrgIdMock = vi.hoisted(() => vi.fn());
const authorizeMock = vi.hoisted(() => vi.fn());
const debugMock = vi.hoisted(() => vi.fn());

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

vi.mock("@/lib/browserbase", () => ({
  getBrowserbaseClient: () => ({ sessions: { debug: debugMock } }),
}));

import { GET } from "./route";

const ORG = "org_liveview";
const RUN = "run_abc123";
const SESSION = "sess_xyz789";

function call(sessionId = SESSION) {
  const request = new Request(
    `http://localhost/api/live-view/${sessionId}?runId=${RUN}`,
  );
  return GET(request, { params: Promise.resolve({ sessionId }) });
}

beforeEach(() => {
  vi.clearAllMocks();
  readAuthWithRetryMock.mockResolvedValue({ userId: "user_1" });
  resolveActiveOrgIdMock.mockResolvedValue(ORG);
});

describe("GET /api/live-view/[sessionId]", () => {
  it("returns 401 for an unauthenticated caller", async () => {
    readAuthWithRetryMock.mockResolvedValue({ userId: undefined });

    const res = await call();

    expect(res.status).toBe(401);
    expect(authorizeMock).not.toHaveBeenCalled();
  });

  it("returns 401 when no active organization can be resolved", async () => {
    resolveActiveOrgIdMock.mockRejectedValue(new Error("No active organization"));

    const res = await call();

    expect(res.status).toBe(401);
  });

  it("returns 400 when the runId query parameter is missing", async () => {
    const request = new Request(
      `http://localhost/api/live-view/${SESSION}`,
    );
    const res = await GET(request, {
      params: Promise.resolve({ sessionId: SESSION }),
    });

    expect(res.status).toBe(400);
    expect(authorizeMock).not.toHaveBeenCalled();
  });

  it("maps a denied access outcome onto 404 for an unknown run", async () => {
    authorizeMock.mockResolvedValue({ ok: false, status: 404 });

    const res = await call();

    expect(res.status).toBe(404);
    expect(await res.json()).toEqual({ error: "Run not found." });
  });

  it("maps a denied access outcome onto 403 for another org's run", async () => {
    authorizeMock.mockResolvedValue({
      ok: false,
      status: 403,
      reason: "run belongs to another org",
    });

    const res = await call();

    expect(res.status).toBe(403);
    expect(await res.json()).toEqual({ error: "Forbidden" });
  });

  it("verifies the run owns the requested session before calling Browserbase", async () => {
    authorizeMock.mockResolvedValue({ ok: true, engine: "legacy" });
    debugMock.mockResolvedValue({
      debuggerFullscreenUrl: "https://bb/full",
      debuggerUrl: "https://bb/embed",
      pages: [],
    });

    await call();

    expect(authorizeMock).toHaveBeenCalledWith({
      orgId: ORG,
      runId: RUN,
      sessionId: SESSION,
    });
  });

  it("serves only the fullscreen + embedded debug URLs on success", async () => {
    authorizeMock.mockResolvedValue({ ok: true, engine: "legacy" });
    debugMock.mockResolvedValue({
      debuggerFullscreenUrl: "https://bb/full",
      debuggerUrl: "https://bb/embed",
      pages: [{ pageId: "p1" }],
    });

    const res = await call();

    expect(res.status).toBe(200);
    expect(await res.json()).toEqual({
      debuggerFullscreenUrl: "https://bb/full",
      debuggerUrl: "https://bb/embed",
    });
  });

  it("works for evo-engine runs too (engine-neutral proxy)", async () => {
    authorizeMock.mockResolvedValue({ ok: true, engine: "evo" });
    debugMock.mockResolvedValue({
      debuggerFullscreenUrl: "https://bb/full",
      debuggerUrl: "https://bb/embed",
      pages: [],
    });

    const res = await call();

    expect(res.status).toBe(200);
  });

  it("returns 404 when the session is gone from Browserbase", async () => {
    authorizeMock.mockResolvedValue({ ok: true, engine: "legacy" });
    debugMock.mockRejectedValue(new Error("Session not found"));

    const res = await call();

    expect(res.status).toBe(404);
    expect(await res.json()).toEqual({
      error: "Live view unavailable for this session.",
    });
  });
});
