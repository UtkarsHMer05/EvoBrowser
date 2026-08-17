import type { Stagehand } from "@browserbasehq/stagehand";

import { showStatusChip } from "@/features/workflows/lib/highlight-element";

export async function agent({
  stagehand,
  instruction,
}: {
  stagehand: Stagehand;
  instruction: string;
}) {
  const page = stagehand.context.pages()[0];

  // The agent drives itself through many internal steps (and navigations), so
  // per-element boxes aren't practical here. Show a status chip so the user
  // knows the autonomous agent is working. Cosmetic only — never fails the
  // step, and any chip is naturally wiped by the agent's own navigations.
  await showStatusChip(page, "Agent working…", {
    color: "blue",
    durationMs: 20000,
  });

  const result = await stagehand.agent().execute(instruction);

  return {
    success: result.success,
    message: result.message,
    completed: result.completed,
  };
}
