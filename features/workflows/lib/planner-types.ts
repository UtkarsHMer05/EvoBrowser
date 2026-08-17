// TypeScript interfaces defining the client/server boundary for the AI workflow planner.

export interface PlanWorkflowGoalInput {
  workflowId: string;
  goal: string;
}

export interface PlanWorkflowGoalResult {
  success: boolean;
  message?: string;
  error?: string;
}
