// Unit tests for GET /api/runs/[runId]/events — the authorized SSE delegator.
// The stream construction lives in buildEvoRunEventsResponse (covered by its
// own suite); this file pins the auth shell around it: 401 without a session
// or active org, Last-Event-ID forwarding, and request-signal propagation for
// client disconnects.
//
// vitest-only (vi.mock); every dependency mocked, no local infra.

import { describe, expect, it, vi, beforeEach } from "vitest";

const readAuthWithRetryMock = vi.hoisted(() => vi.fn());
const resolveActiveOrgIdMock = vi.hoisted(() => vi.fn());
const buildResponseMock = vi.hoisted(() => vi.fn());

vi.mock("@/lib/auth", () => ({
  readAuthWithRetry: readAuthWithRetryMock,
  resolveActiveOrgId: resolveActiveOrgIdMock,
}));

vi.mock("@/features/workflows/lib/evo-run-events-route", () => ({
  buildEvoRunEventsResponse: buildResponseMock,
}));

import { GET } from "./route";

const ORG = "org_events";
const RUN = "evo_99999999-8888-7777-6666-555555555555";

function call(headers: Record<string, string> = {}) {
  const request = new Request(`http://localhost/api/runs/${RUN}/events`, {
    headers,
  });
  return GET(request, { params: Promise.resolve({ runId: RUN }) });
}

beforeEach(() => {
  vi.clearAllMocks();
  readAuthWithRetryMock.mockResolvedValue({ userId: "user_1" });
  resolveActiveOrgIdMock.mockResolvedValue(ORG);
  const stub = new Response(null, { status: 200 });
  buildResponseMock.mockResolvedValue(stub);
});

describe("GET /api/runs/[runId]/events", () => {
  it("returns 401 for an unauthenticated caller without building a stream", async () => {
    readAuthWithRetryMock.mockResolvedValue({ userId: undefined });

    const res = await call();

    expect(res.status).toBe(401);
    expect(buildResponseMock).not.toHaveBeenCalled();
  });

  it("returns 401 when there is no active organization", async () => {
    resolveActiveOrgIdMock.mockRejectedValue(new Error("No active organization"));

    const res = await call();

    expect(res.status).toBe(401);
    expect(buildResponseMock).not.toHaveBeenCalled();
  });

  it("delegates with run/org identity and forwards Last-Event-ID", async () => {
    await call({ "last-event-id": "42-7" });

    expect(buildResponseMock).toHaveBeenCalledWith({
      runId: RUN,
      orgId: ORG,
      lastEventId: "42-7",
      signal: expect.any(AbortSignal),
    });
  });

  it("passes lastEventId undefined on a first connect", async () => {
    await call();

    expect(buildResponseMock).toHaveBeenCalledWith({
      runId: RUN,
      orgId: ORG,
      lastEventId: undefined,
      signal: expect.any(AbortSignal),
    });
  });
});
