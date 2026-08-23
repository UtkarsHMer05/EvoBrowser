// Unit tests for GET /api/runs/[runId]/screenshot — the final-screenshot
// artifact route. Ownership is engine-aware via authorizeRunAccess; the
// artifact itself is then re-read org-scoped from whichever store owns the
// run (Neon for legacy, Phase-2 Postgres for evo). This suite pins that
// routing, the binary response contract, and the missing-artifact 404.
//
// vitest-only (vi.mock); every dependency mocked, no local infra.

import { describe, expect, it, vi, beforeEach } from "vitest";

const readAuthWithRetryMock = vi.hoisted(() => vi.fn());
const resolveActiveOrgIdMock = vi.hoisted(() => vi.fn());
const authorizeMock = vi.hoisted(() => vi.fn());
const getRunArtifactMock = vi.hoisted(() => vi.fn());
const getEvoRunArtifactMock = vi.hoisted(() => vi.fn());

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
  getRunArtifact: getRunArtifactMock,
}));

vi.mock("@/features/workflows/lib/evo-run-data", () => ({
  getEvoRunArtifact: getEvoRunArtifactMock,
}));

import { GET } from "./route";

const ORG = "org_shot";
const RUN = "run_shot001";
// A tiny valid JPEG header, base64-encoded, so the bytes round-trip.
const JPEG_B64 = Buffer.from([
  0xff, 0xd8, 0xff, 0xe0, 0x00, 0x10, 0x4a, 0x46,
]).toString("base64");

function call() {
  const request = new Request(`http://localhost/api/runs/${RUN}/screenshot`);
  return GET(request, { params: Promise.resolve({ runId: RUN }) });
}

beforeEach(() => {
  vi.clearAllMocks();
  readAuthWithRetryMock.mockResolvedValue({ userId: "user_1" });
  resolveActiveOrgIdMock.mockResolvedValue(ORG);
});

describe("GET /api/runs/[runId]/screenshot", () => {
  it("returns 401 for an unauthenticated caller", async () => {
    readAuthWithRetryMock.mockResolvedValue({ userId: undefined });

    const res = await call();

    expect(res.status).toBe(401);
    expect(getRunArtifactMock).not.toHaveBeenCalled();
    expect(getEvoRunArtifactMock).not.toHaveBeenCalled();
  });

  it("maps denied ownership onto 403/404 before reading any artifact", async () => {
    authorizeMock.mockResolvedValue({
      ok: false,
      status: 403,
      reason: "run belongs to another org",
    });

    const forbidden = await call();

    expect(forbidden.status).toBe(403);

    authorizeMock.mockResolvedValue({ ok: false, status: 404 });
    const missing = await call();

    expect(missing.status).toBe(404);
    expect(getRunArtifactMock).not.toHaveBeenCalled();
    expect(getEvoRunArtifactMock).not.toHaveBeenCalled();
  });

  it("reads a legacy run's screenshot org-scoped from Neon", async () => {
    authorizeMock.mockResolvedValue({ ok: true, engine: "legacy" });
    getRunArtifactMock.mockResolvedValue({ screenshotBase64: JPEG_B64 });

    const res = await call();

    expect(res.status).toBe(200);
    expect(getRunArtifactMock).toHaveBeenCalledWith(ORG, RUN);
    expect(getEvoRunArtifactMock).not.toHaveBeenCalled();
  });

  it("reads an evo run's screenshot org-scoped from the Phase-2 store", async () => {
    authorizeMock.mockResolvedValue({ ok: true, engine: "evo" });
    getEvoRunArtifactMock.mockResolvedValue({ screenshotBase64: JPEG_B64 });

    const res = await call();

    expect(res.status).toBe(200);
    expect(getEvoRunArtifactMock).toHaveBeenCalledWith(ORG, RUN);
    expect(getRunArtifactMock).not.toHaveBeenCalled();
  });

  it("serves immutable JPEG bytes with private caching", async () => {
    authorizeMock.mockResolvedValue({ ok: true, engine: "legacy" });
    getRunArtifactMock.mockResolvedValue({ screenshotBase64: JPEG_B64 });

    const res = await call();

    expect(res.headers.get("Content-Type")).toBe("image/jpeg");
    expect(res.headers.get("Cache-Control")).toContain("private");
    const bytes = Buffer.from(await res.arrayBuffer());
    expect(bytes.equals(Buffer.from(JPEG_B64, "base64"))).toBe(true);
  });

  it("returns 404 when no screenshot exists yet", async () => {
    authorizeMock.mockResolvedValue({ ok: true, engine: "legacy" });
    getRunArtifactMock.mockResolvedValue(undefined);

    const res = await call();

    expect(res.status).toBe(404);
    expect(await res.json()).toEqual({
      error: "No screenshot available for this run.",
    });
  });
});
