import { z } from "zod";

export const WorkflowPlanNodeSchema = z.object({
  id: z.string().min(1),
  type: z.string().min(1),
  title: z.string().min(1),
  values: z.record(z.string(), z.string()).default({}),
});

export const WorkflowPlanEdgeSchema = z.object({
  id: z.string().min(1),
  source: z.string().min(1),
  target: z.string().min(1),
});

export const WorkflowPlanSchema = z.object({
  version: z.string().default("1.0"),
  name: z.string().min(1),
  canBuild: z.boolean(),
  unsupportedReason: z.string().optional(),
  nodes: z.array(WorkflowPlanNodeSchema).default([]),
  edges: z.array(WorkflowPlanEdgeSchema).default([]),
});

export type WorkflowPlanNode = z.infer<typeof WorkflowPlanNodeSchema>;
export type WorkflowPlanEdge = z.infer<typeof WorkflowPlanEdgeSchema>;
export type WorkflowPlan = z.infer<typeof WorkflowPlanSchema>;

export interface PlanWorkflowGoalInput {
  workflowId: string;
  goal: string;
}

export interface PlanWorkflowGoalResult {
  success: boolean;
  message?: string;
  error?: string;
  plan?: WorkflowPlan;
}

