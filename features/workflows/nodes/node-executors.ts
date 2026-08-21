import type { Stagehand } from "@browserbasehq/stagehand";

import type {
  ActionNodeType,
  NodeType,
} from "@/features/workflows/nodes/node-registry";
import { act } from "./act";
import { agent } from "./agent";
import { extract } from "./extract";
import { observe } from "./observe";
import { openUrl } from "./open-url";
import { sendEmail } from "./send-email";

export type NodeContext = {
  values: Record<string, string>;
  getStagehand: () => Promise<Stagehand>;
  /**
   * Milestone 33: a deterministic per-attempt idempotency key derived by the
   * distributed worker from the attempt identity (run + node + attempt). Only
   * side-effecting executors consume it (send-email forwards it to the
   * provider so a duplicate delivery cannot double-send). Undefined on the
   * legacy Trigger.dev path, which keeps its existing behavior unchanged.
   */
  idempotencyKey?: string;
};

export type NodeExecutor = (ctx: NodeContext) => Promise<unknown>;

export const nodeExecutors: Partial<Record<NodeType, NodeExecutor>> = {
  "open-url": async ({ values, getStagehand }) =>
    openUrl({ stagehand: await getStagehand(), url: values.url }),
  act: async ({ values, getStagehand }) =>
    act({ stagehand: await getStagehand(), instruction: values.instruction }),
  extract: async ({ values, getStagehand }) =>
    extract({
      stagehand: await getStagehand(),
      instruction: values.instruction,
    }),
  observe: async ({ values, getStagehand }) =>
    observe({
      stagehand: await getStagehand(),
      instruction: values.instruction,
    }),
  agent: async ({ values, getStagehand }) =>
    agent({ stagehand: await getStagehand(), instruction: values.instruction }),
  "send-email": async ({ values, idempotencyKey }) =>
    sendEmail({
      to: values.to,
      subject: values.subject,
      body: values.body,
      idempotencyKey,
    }),
} satisfies Record<ActionNodeType, NodeExecutor>;
