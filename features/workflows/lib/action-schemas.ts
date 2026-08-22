// Input contracts for the workflow server actions.
//
// Server Actions are public endpoints: the browser can call them with any
// shape, so every argument is validated here before it reaches a database
// query or an engine adapter. Semantic rules (one Start node, no cycles,
// registered node types, Agent Pro gating) stay where they were — this layer
// guarantees shape and bounds, uniformly, instead of ad-hoc checks scattered
// through actions.ts.

import { z } from "zod";

// Trigger.dev run ids ("run_…"), Evo run ids ("evo_<uuid>") and Phase-2
// scheduler ids all share a bounded identifier charset. Rejecting everything
// else at the boundary keeps provider calls and URL construction safe.
export const runIdSchema = z
  .string()
  .min(1)
  .max(200)
  .regex(/^[A-Za-z0-9._-]+$/, "must contain only letters, digits, dot, dash");

export const workflowIdSchema = z.uuid("must be a workflow id (uuid)");

export const workflowNameSchema = z.string().trim().min(1).max(120);

// A single-line planner goal, bounded like the planner prompt itself.
export const planGoalSchema = z.object({
  workflowId: workflowIdSchema,
  goal: z.string().trim().min(1, "Goal prompt cannot be empty.").max(2000),
});

// Structural envelope for a submitted graph. Deliberately loose (looseObject):
// React Flow decorates nodes/edges with runtime fields (measured, selected,
// …) that the canvas round-trips, and stripping them would change the stored
// snapshot shape. What matters at this boundary is that ids exist and node
// data carries the registry discriminator every consumer relies on. Semantic
// validation stays in validateGraph, which runs right after this in the action.
export const graphNodeSchema = z.looseObject({
  id: z.string().min(1),
  data: z.looseObject({
    type: z.string().min(1),
    title: z.string().min(1),
    values: z.record(z.string(), z.string()).optional(),
  }),
});

export const graphEdgeSchema = z.looseObject({
  id: z.string().min(1),
  source: z.string().min(1),
  target: z.string().min(1),
});

export const workflowGraphSchema = z.object({
  nodes: z.array(graphNodeSchema).min(1),
  edges: z.array(graphEdgeSchema),
});

export const createWorkflowInputSchema = z.object({ name: workflowNameSchema });

export const deleteWorkflowInputSchema = z.object({ id: workflowIdSchema });

export const runWorkflowInputSchema = z.object({
  id: workflowIdSchema,
  graph: workflowGraphSchema,
});

export const cancelRunInputSchema = z.object({ runId: runIdSchema });

export const listEvoRunsInputSchema = z.object({
  workflowId: workflowIdSchema,
});

/**
 * Parse an untrusted action argument, converting a Zod failure into the plain
 * Error surface the UI already renders as a toast.
 */
export function parseActionInput<T>(
  schema: z.ZodType<T>,
  value: unknown,
  label: string,
): T {
  const result = schema.safeParse(value);
  if (!result.success) {
    const issue = result.error.issues[0];
    const path = issue?.path.join(".") ?? "";
    throw new Error(
      `Invalid ${label}${path ? ` (${path})` : ""}: ${issue?.message ?? "malformed input"}`,
    );
  }
  return result.data;
}
