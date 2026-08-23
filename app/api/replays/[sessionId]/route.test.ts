// Unit tests for GET /api/replays/[sessionId] — the Browserbase HLS replay
// proxy. Replay is the most sensitive artifact route (it grew a cross-org
// hole once before authorizeRunAccess existed), so this suite pins every
// layer: auth → Pro gate → run/session ownership → not-ready (202) vs
// playlist (200) mapping.
//
// vitest-only (vi.mock); every dependency mocked, no local infra.

import { describe, expect, it, vi, beforeEach } from "vitest";

const readAuthWithRetryMock = vi.hoisted(() => vi.fn());
const resolveActiveOrgIdMock = vi.hoisted(() => vi.fn());
const authorizeMock = vi.hoisted(() => vi.fn());
const retrieveReplayMock = vi.hoisted(() => vi.fn());
const retrievePageMock = vi.hoisted(() => vi.fn());

// The route does `error instanceof NotFoundError`, so the mocked SDK module
// must export the SAME class the tests construct. vi.hoisted keeps the class
// available to the hoisted vi.mock factory.
const { NotFoundError } = vi.hoisted(() => ({
  NotFoundError: class NotFoundError extends Error {},
}));

vi.mock("@sentry/nextjs", () => ({
  getIsolationScope: () => ({ setAttributes: vi.fn() }),
  logger: { warn: vi.fn(), info: vi.fn(), error: vi.fn() },
}));

vi.mock("@browserbasehq/sdk", () => ({ NotFoundError }));

vi.mock("@/lib/auth", () => ({
  readAuthWithRetry: readAuthWithRetryMock,
  resolveActiveOrgId: resolveActiveOrgIdMock,
}));

vi.mock("@/features/workflows/lib/run-authorization", () => ({
  authorizeRunAccess: authorizeMock,
}));

vi.mock("@/lib/browserbase", () => ({
  getBrowserbaseClient: () => ({
    sessions: {
      replays: {
        retrieve: retrieveReplayMock,
        retrievePage: retrievePageMock,
      },
    },
  }),
}));

import { GET } from "./route";

const ORG = "org_pro";
const RUN = "run_replay1";
const SESSION = "sess_replay01";

function call() {
  const request = new Request(
    `http://localhost/api/replays/${SESSION}?runId=${RUN}`,
  );
  return GET(request, { params: Promise.resolve({ sessionId: SESSION }) });
}

beforeEach(() => {
  vi.clearAllMocks();
  // Pro caller by default; individual tests downgrade.
  readAuthWithRetryMock.mockResolvedValue({
    userId: "user_1",
    has: () => true,
  });
  resolveActiveOrgIdMock.mockResolvedValue(ORG);
});

describe("GET /api/replays/[sessionId]", () => {
  it("returns 401 for an unauthenticated caller", async () => {
    readAuthWithRetryMock.mockResolvedValue({ userId: undefined });

    const res = await call();

    expect(res.status).toBe(401);
    expect(retrieveReplayMock).not.toHaveBeenCalled();
  });

  it("denies a non-Pro org with 403 before any ownership or Browserbase work", async () => {
    readAuthWithRetryMock.mockResolvedValue({ userId: "user_1", has: () => false });

    const res = await call();

    expect(res.status).toBe(403);
    expect(await res.text()).toContain("Pro plan required");
    expect(authorizeMock).not.toHaveBeenCalled();
    expect(retrieveReplayMock).not.toHaveBeenCalled();
  });

  it("fails closed when the plan claim is absent", async () => {
    readAuthWithRetryMock.mockResolvedValue({ userId: "user_1", has: undefined });

    const res = await call();

    expect(res.status).toBe(403);
  });

  it("returns 400 when the runId query parameter is missing", async () => {
    const request = new Request(`http://localhost/api/replays/${SESSION}`);
    const res = await GET(request, {
      params: Promise.resolve({ sessionId: SESSION }),
    });

    expect(res.status).toBe(400);
  });

  it("maps denied ownership onto 404/403 text responses", async () => {
    authorizeMock.mockResolvedValue({
      ok: false,
      status: 403,
      reason: "session does not belong to run",
    });

    const forbidden = await call();

    expect(forbidden.status).toBe(403);
    expect(await forbidden.text()).toContain("Forbidden");

    authorizeMock.mockResolvedValue({ ok: false, status: 404 });
    const missing = await call();

    expect(missing.status).toBe(404);
    expect(await missing.text()).toContain("Run not found");
  });

  it("checks that the run drove this session before touching Browserbase", async () => {
    authorizeMock.mockResolvedValue({ ok: true, engine: "legacy" });
    retrieveReplayMock.mockResolvedValue({ pages: [{ pageId: "p1" }] });
    retrievePageMock.mockResolvedValue({ text: async () => "#EXTM3U\n" });

    await call();

    expect(authorizeMock).toHaveBeenCalledWith({
      orgId: ORG,
      runId: RUN,
      sessionId: SESSION,
    });
  });

  it("returns 202 while the recording has no pages yet", async () => {
    authorizeMock.mockResolvedValue({ ok: true, engine: "evo" });
    retrieveReplayMock.mockResolvedValue({ pages: [] });

    const res = await call();

    expect(res.status).toBe(202);
    expect(retrievePageMock).not.toHaveBeenCalled();
  });

  it("maps Browserbase's pre-registration 404 onto 202 so clients keep polling", async () => {
    authorizeMock.mockResolvedValue({ ok: true, engine: "legacy" });
    retrieveReplayMock.mockRejectedValue(new NotFoundError("no replay yet"));

    const res = await call();

    expect(res.status).toBe(202);
  });

  it("serves the m3u8 playlist with no-store caching once ready", async () => {
    authorizeMock.mockResolvedValue({ ok: true, engine: "legacy" });
    retrieveReplayMock.mockResolvedValue({
      pages: [{ pageId: "p1" }, { pageId: "p2" }],
    });
    retrievePageMock.mockResolvedValue({
      text: async () => "#EXTM3U\n#EXT-X-VERSION:3\n",
    });

    const res = await call();

    expect(res.status).toBe(200);
    expect(res.headers.get("Content-Type")).toBe(
      "application/vnd.apple.mpegurl",
    );
    expect(res.headers.get("Cache-Control")).toBe("no-store");
    expect(await res.text()).toContain("#EXTM3U");
    expect(retrievePageMock).toHaveBeenCalledWith(SESSION, "p1");
  });

  it("rethrows unexpected Browserbase failures instead of masking them as 202", async () => {
    authorizeMock.mockResolvedValue({ ok: true, engine: "legacy" });
    retrieveReplayMock.mockRejectedValue(new Error("upstream 500"));

    await expect(call()).rejects.toThrow("upstream 500");
  });
});
