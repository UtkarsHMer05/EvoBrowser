// Unit tests for the server-action boundary (features/workflows/actions.ts).
//
// Server Actions are public endpoints, so this suite pins the checks that run
// before any store or engine is touched:
//   shape/bounds    - parseActionInput rejects malformed arguments uniformly
//   Pro gate        - an Agent node without the Clerk pro claim never reaches
//                     an adapter (checked in the action, not the task)
//   graph gate      - saveWorkflowGraph failures propagate (run blocked)
//   engine routing  - legacy fail-open version snapshot vs evo fail-closed,
//                     run-record-before-submit ordering on the evo path
//   cancel routing  - resolveRunEngine picks the owning engine's adapter
//
// vitest-only (vi.mock): every store/provider dependency is mocked; zod
// schemas and parseActionInput stay REAL because they are the contract under
// test. No local infra needed.

import { describe, expect, it, vi, beforeEach } from "vitest";
import { randomUUID } from "node:crypto";

const authMock = vi.hoisted(() => vi.fn());
const resolveActiveOrgIdMock = vi.hoisted(() => vi.fn());
const redirectMock = vi.hoisted(() => vi.fn());
const revalidatePathMock = vi.hoisted(() => vi.fn());
const createWorkflowMock = vi.hoisted(() => vi.fn());
const deleteWorkflowMock = vi.hoisted(() => vi.fn());
const getWorkflowMock = vi.hoisted(() => vi.fn());
const saveWorkflowGraphMock = vi.hoisted(() => vi.fn());
const deleteRoomMock = vi.hoisted(() => vi.fn());
const createWorkflowVersionMock = vi.hoisted(() => vi.fn());
const getExecutionEngineMock = vi.hoisted(() => vi.fn(() => "legacy"));
const startRunMock = vi.hoisted(() => vi.fn());
const cancelRunMock = vi.hoisted(() => vi.fn());
const createWorkflowRunRecordMock = vi.hoisted(() => vi.fn());
const ensurePhase2WorkflowMock = vi.hoisted(() => vi.fn());
const resolveRunEngineMock = vi.hoisted(() => vi.fn());
const listEvoRunsForWorkflowMock = vi.hoisted(() => vi.fn());
const generateWorkflowPlanMock = vi.hoisted(() => vi.fn());

vi.mock("@sentry/nextjs", () => ({
  getIsolationScope: () => ({ setAttributes: vi.fn() }),
  logger: { warn: vi.fn(), info: vi.fn(), error: vi.fn() },
}));

vi.mock("@clerk/nextjs/server", () => ({ auth: authMock }));

vi.mock("next/cache", () => ({ revalidatePath: revalidatePathMock }));

vi.mock("next/navigation", () => ({ redirect: redirectMock }));

vi.mock("@/lib/auth", () => ({
  resolveActiveOrgId: resolveActiveOrgIdMock,
}));

vi.mock("@/lib/liveblocks", () => ({
  getLiveblocksClient: () => ({ deleteRoom: deleteRoomMock }),
}));

vi.mock("@/lib/db/phase2", () => ({ getPhase2Db: () => ({ __phase2: true }) }));

vi.mock("@/features/workflows/data", () => ({
  createWorkflow: createWorkflowMock,
  deleteWorkflow: deleteWorkflowMock,
  getWorkflow: getWorkflowMock,
  saveWorkflowGraph: saveWorkflowGraphMock,
}));

vi.mock("@/features/workflows/lib/workflow-versions", () => ({
  createWorkflowVersion: createWorkflowVersionMock,
}));

vi.mock("@/features/workflows/lib/execution-engine", () => ({
  getExecutionEngine: getExecutionEngineMock,
  getExecutionEngineAdapter: () => ({
    engine: getExecutionEngineMock(),
    startRun: startRunMock,
    cancelRun: cancelRunMock,
  }),
  resetExecutionEngineAdapterForTests: () => {},
}));

vi.mock("@/features/workflows/lib/run-records", () => ({
  createWorkflowRunRecord: createWorkflowRunRecordMock,
  ensurePhase2Workflow: ensurePhase2WorkflowMock,
  resolveRunEngine: resolveRunEngineMock,
}));

vi.mock("@/features/workflows/lib/evo-runs", () => ({
  listEvoRunsForWorkflow: listEvoRunsForWorkflowMock,
}));

vi.mock("@/features/workflows/lib/planner-service", () => ({
  generateWorkflowPlan: generateWorkflowPlanMock,
}));

import {
  cancelWorkflowRunAction,
  createWorkflowAction,
  deleteWorkflowAction,
  listEvoRunsAction,
  planWorkflowAction,
  runWorkflowAction,
} from "./actions";

import type { NodeType } from "@/features/workflows/nodes/node-registry";
import type { WorkflowGraph } from "@/lib/db/schema";

const ORG = "org_actions_test";
const WORKFLOW_ID = randomUUID();
const RUN_ID_EVO = "evo_serialized_run";

function graphOf(types: Array<{ id: string; type: NodeType }>, edges: Array<[string, string]> = []): WorkflowGraph {
  const nodes = types.map((n) => ({
    id: n.id,
    type: "step" as const,
    position: { x: 0, y: 0 },
    data: { type: n.type, kind: n.type === "start" ? ("trigger" as const) : ("action" as const), title: n.type, values: {} },
  }));
  return {
    nodes,
    edges: edges.map(([source, target], i) => ({
      id: `e${i}`,
      source,
      target,
    })),
  };
}

beforeEach(() => {
  vi.clearAllMocks();
  authMock.mockResolvedValue({
    userId: "user_1",
    has: ({ plan }: { plan: string }) => plan === "pro",
  });
  resolveActiveOrgIdMock.mockResolvedValue(ORG);
  createWorkflowVersionMock.mockResolvedValue({
    id: "ver_new",
    versionNumber: 1,
  });
  createWorkflowRunRecordMock.mockResolvedValue({ id: "row" });
  startRunMock.mockImplementation(async (args: { runId?: string }) => ({
    engine: getExecutionEngineMock(),
    runId: args.runId ?? "run_trigger123",
  }));
});

describe("createWorkflowAction", () => {
  it("rejects an empty name before touching the store", async () => {
    await expect(createWorkflowAction("   ")).rejects.toThrow(
      /Invalid workflow name/,
    );
    expect(createWorkflowMock).not.toHaveBeenCalled();
  });

  it("creates, revalidates, and redirects to the editor", async () => {
    createWorkflowMock.mockResolvedValue({ id: WORKFLOW_ID, orgId: ORG });

    await createWorkflowAction("  My workflow ");

    expect(createWorkflowMock).toHaveBeenCalledWith(ORG, "My workflow");
    expect(revalidatePathMock).toHaveBeenCalledWith("/", "layout");
    expect(redirectMock).toHaveBeenCalledWith(
      `/workflows/${WORKFLOW_ID}?new=true`,
    );
  });
});

describe("deleteWorkflowAction", () => {
  it("rejects a malformed workflow id", async () => {
    await expect(deleteWorkflowAction("not-a-uuid")).rejects.toThrow(
      /Invalid workflow id/,
    );
  });

  it("throws when the workflow does not exist in this org", async () => {
    deleteWorkflowMock.mockResolvedValue(undefined);

    await expect(deleteWorkflowAction(WORKFLOW_ID)).rejects.toThrow(
      "Workflow not found",
    );
    expect(deleteRoomMock).not.toHaveBeenCalled();
  });

  it("deletes the Liveblocks room and redirects home on success", async () => {
    deleteWorkflowMock.mockResolvedValue({ id: WORKFLOW_ID });

    await deleteWorkflowAction(WORKFLOW_ID);

    expect(deleteRoomMock).toHaveBeenCalledWith(WORKFLOW_ID);
    expect(redirectMock).toHaveBeenCalledWith("/");
  });
});

describe("runWorkflowAction — boundary gates", () => {
  it("rejects a graph missing node titles at the schema layer", async () => {
    // Deliberately malformed: cast past WorkflowGraph because the schema
    // boundary under test is what must reject it before any consumer runs.
    const bad = {
      id: WORKFLOW_ID,
      graph: { nodes: [{ id: "n1", data: { type: "act" } }], edges: [] },
    } as unknown as Parameters<typeof runWorkflowAction>[0];

    await expect(runWorkflowAction(bad)).rejects.toThrow(/Invalid run input/);
    expect(saveWorkflowGraphMock).not.toHaveBeenCalled();
  });

  it("denies an Agent node without the Pro plan before saving or starting", async () => {
    authMock.mockResolvedValue({ userId: "user_1", has: () => false });

    const input = {
      id: WORKFLOW_ID,
      graph: graphOf(
        [
          { id: "n1", type: "start" },
          { id: "n2", type: "agent" },
        ],
        [["n1", "n2"]],
      ),
    };

    await expect(runWorkflowAction(input)).rejects.toThrow(
      "The Agent node requires the Pro plan.",
    );
    expect(saveWorkflowGraphMock).not.toHaveBeenCalled();
    expect(startRunMock).not.toHaveBeenCalled();
  });

  it("propagates graph-validation failures from saveWorkflowGraph", async () => {
    saveWorkflowGraphMock.mockRejectedValue(new Error("cycle detected"));

    const input = {
      id: WORKFLOW_ID,
      graph: graphOf(
        [
          { id: "n1", type: "start" },
          { id: "n2", type: "act" },
        ],
        [["n1", "n2"]],
      ),
    };

    await expect(runWorkflowAction(input)).rejects.toThrow("cycle detected");
    expect(startRunMock).not.toHaveBeenCalled();
  });
});

describe("runWorkflowAction — legacy engine", () => {
  it("snapshots best-effort, starts the run, and records it fail-open", async () => {
    saveWorkflowGraphMock.mockResolvedValue(undefined);
    createWorkflowVersionMock.mockRejectedValue(
      new Error("phase-2 tables absent"),
    );

    const input = {
      id: WORKFLOW_ID,
      graph: graphOf(
        [
          { id: "n1", type: "start" },
          { id: "n2", type: "open-url" },
        ],
        [["n1", "n2"]],
      ),
    };

    const handle = await runWorkflowAction(input);

    expect(handle).toMatchObject({ engine: "legacy", runId: "run_trigger123" });
    expect(saveWorkflowGraphMock).toHaveBeenCalledTimes(1);
    // Fail-open: the snapshot miss never blocked the run...
    expect(startRunMock).toHaveBeenCalledWith(
      expect.objectContaining({ orgId: ORG, workflowId: WORKFLOW_ID }),
    );
    // ...and the audit row write was attempted with the Trigger.dev run id.
    expect(createWorkflowRunRecordMock).toHaveBeenCalledWith(
      expect.objectContaining({ runId: "run_trigger123", engine: "legacy" }),
    );
  });
});

describe("runWorkflowAction — evo engine", () => {
  const evoInput = {
    id: WORKFLOW_ID,
    graph: graphOf(
      [
        { id: "n1", type: "start" },
        { id: "n2", type: "extract" },
      ],
      [["n1", "n2"]],
    ),
  };

  beforeEach(() => {
    getExecutionEngineMock.mockReturnValue("evo");
    saveWorkflowGraphMock.mockResolvedValue(undefined);
    getWorkflowMock.mockResolvedValue({ id: WORKFLOW_ID, name: "Wf" });
  });

  it("fails closed when the immutable version snapshot cannot be written", async () => {
    createWorkflowVersionMock.mockRejectedValue(new Error("pg down"));

    await expect(runWorkflowAction(evoInput)).rejects.toThrow(
      "Could not snapshot the workflow for the Evo engine.",
    );
    expect(startRunMock).not.toHaveBeenCalled();
  });

  it("ensures the Phase-2 workflow row, snapshots, records, then submits", async () => {
    const handle = await runWorkflowAction(evoInput);

    expect(handle.engine).toBe("evo");
    expect(handle.runId).toMatch(/^evo_[0-9a-f-]{36}$/);

    expect(ensurePhase2WorkflowMock).toHaveBeenCalledWith(
      expect.anything(),
      expect.objectContaining({ id: WORKFLOW_ID, orgId: ORG }),
    );
    expect(createWorkflowVersionMock).toHaveBeenCalledWith(
      expect.objectContaining({ db: expect.objectContaining({ __phase2: true }) }),
    );

    // The durable run row exists BEFORE the scheduler submission.
    const recordOrder = createWorkflowRunRecordMock.mock.invocationCallOrder[0];
    const submitOrder = startRunMock.mock.invocationCallOrder[0];
    expect(recordOrder).toBeLessThan(submitOrder);
    expect(startRunMock).toHaveBeenCalledWith(
      expect.objectContaining({ runId: handle.runId }),
    );
  });
});

describe("cancelWorkflowRunAction", () => {
  it("rejects a run id outside the bounded charset", async () => {
    await expect(cancelWorkflowRunAction("bad id!")).rejects.toThrow(
      /Invalid run id/,
    );
    expect(cancelRunMock).not.toHaveBeenCalled();
  });

  it("routes to the engine that owns the run", async () => {
    resolveRunEngineMock.mockResolvedValue("evo");

    await cancelWorkflowRunAction("evo_abc");

    expect(cancelRunMock).toHaveBeenCalledWith("evo_abc");
  });

  it("falls back to legacy when neither store knows the run", async () => {
    resolveRunEngineMock.mockResolvedValue(undefined);

    await cancelWorkflowRunAction("run_old123");

    expect(cancelRunMock).toHaveBeenCalledWith("run_old123");
  });
});

describe("planWorkflowAction", () => {
  const goal = { workflowId: WORKFLOW_ID, goal: "Scrape hacker news" };

  it("rejects an empty goal at the boundary", async () => {
    await expect(
      planWorkflowAction({ workflowId: WORKFLOW_ID, goal: "   " }),
    ).rejects.toThrow(/Invalid planner input.*empty/i);
    expect(generateWorkflowPlanMock).not.toHaveBeenCalled();
  });

  it("returns the plan with a success message", async () => {
    const plan = { canBuild: true, nodes: [{ id: "a" }], edges: [] };
    generateWorkflowPlanMock.mockResolvedValue(plan);

    const result = await planWorkflowAction(goal);

    expect(result.success).toBe(true);
    expect(result).toMatchObject({
      message: "Workflow plan generated successfully.",
      plan,
    });
  });

  it("surfaces the unsupported reason without throwing", async () => {
    generateWorkflowPlanMock.mockResolvedValue({
      canBuild: false,
      unsupportedReason: "That goal needs email access.",
      nodes: [],
      edges: [],
    });

    const result = await planWorkflowAction(goal);

    expect(result.success).toBe(true);
    expect(result.message).toBe("That goal needs email access.");
  });

  it("maps planner failures onto a failed result instead of throwing", async () => {
    generateWorkflowPlanMock.mockRejectedValue(new Error("provider down"));

    const result = await planWorkflowAction(goal);

    expect(result).toEqual({ success: false, error: "provider down" });
  });
});

describe("listEvoRunsAction", () => {
  it("serializes dates across the server-action boundary", async () => {
    const created = new Date("2026-08-22T12:00:00Z");
    listEvoRunsForWorkflowMock.mockResolvedValue([
      { id: RUN_ID_EVO, status: "succeeded", steps: [], createdAt: created },
    ]);

    const runs = await listEvoRunsAction(WORKFLOW_ID);

    expect(runs[0]).toMatchObject({
      id: RUN_ID_EVO,
      createdAt: "2026-08-22T12:00:00.000Z",
    });
  });

  it("rejects a non-uuid workflow id", async () => {
    await expect(listEvoRunsAction("nope")).rejects.toThrow(
      /Invalid workflow id/,
    );
  });
});
