// Phase 2 — legacy Trigger.dev engine adapter (Milestone 27).
//
// Wraps the EXISTING Trigger.dev execution path behind the engine-neutral
// adapter interface WITHOUT changing its behavior (M27 step 2). The Run
// action still saves the graph and snapshots the workflow version before
// calling startRun; this adapter only triggers the task and maps the handle.
//
// The engine-neutral run id for the legacy engine IS the Trigger.dev run id,
// so every existing consumer (live-view handshake, cancel, replay, results
// popup) keeps working unchanged.
//
// Testability: the Trigger.dev trigger/cancel calls are injectable so the
// legacy-adapter regression test can assert the exact call shape without a
// live Trigger.dev account. Defaults are the real SDK calls — behavior is
// identical to the pre-M27 runWorkflowAction.

import { runs, tasks } from "@trigger.dev/sdk";

import type { runWorkflowTask } from "@/features/workflows/tasks/run-workflow";

import type {
  EngineRunHandle,
  EngineRunStatus,
  ExecutionEngineAdapter,
  StartRunArgs,
} from "./execution-engine";

export interface LegacyEngineAdapterOptions {
  /** Injectable trigger (tests). Defaults to the real Trigger.dev call. */
  trigger?: (args: {
    workflowId: string;
    orgId: string;
  }) => Promise<{ id: string }>;
  /** Injectable cancel (tests). Defaults to the real Trigger.dev call. */
  cancel?: (runId: string) => Promise<void>;
}

export function createLegacyEngineAdapter(
  options: LegacyEngineAdapterOptions = {},
): ExecutionEngineAdapter {
  const trigger =
    options.trigger ??
    (async (args: { workflowId: string; orgId: string }) => {
      // Identical trigger call to the pre-M27 runWorkflowAction.
      const handle = await tasks.trigger<typeof runWorkflowTask>(
        "run-workflow",
        { workflowId: args.workflowId, orgId: args.orgId },
        { tags: [`workflow:${args.workflowId}`] },
      );
      return { id: handle.id };
    });
  const cancel = options.cancel ?? ((runId: string) => runs.cancel(runId));

  return {
    engine: "legacy",

    async startRun(args: StartRunArgs): Promise<EngineRunHandle> {
      const handle = await trigger({
        workflowId: args.workflowId,
        orgId: args.orgId,
      });
      return {
        engine: "legacy",
        runId: handle.id,
        providerRunId: handle.id,
      };
    },

    async cancelRun(runId: string): Promise<void> {
      await cancel(runId);
    },

    async getRunStatus(runId: string): Promise<EngineRunStatus> {
      // Best-effort: Trigger.dev run retrieval is not part of the Phase-1
      // surface, so report unknown rather than adding a new dependency here.
      // M28 introduces engine-neutral status fan-out for both engines.
      return { runId, engine: "legacy", status: "unknown" };
    },
  };
}
