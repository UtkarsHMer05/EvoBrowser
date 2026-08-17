import type { Stagehand } from "@browserbasehq/stagehand";

import { highlightElement } from "@/features/workflows/lib/highlight-element";

export async function act({
  stagehand,
  instruction,
}: {
  stagehand: Stagehand;
  instruction: string;
}) {
  const result = await stagehand.act(instruction);
  const page = stagehand.context.pages()[0];

  // Show the user what the agent just acted on. `act` returns the concrete
  // selector(s) it used; drawing a box around the first one in the live view
  // makes the click/typing target visible. The overlay auto-clears, and this
  // is cosmetic — it never fails the step.
  const target = result.actions?.[0]?.selector;
  if (target) {
    await highlightElement(page, target, {
      label: "Acted here",
      color: "blue",
      durationMs: 3000,
    });
  }

  return { success: result.success, message: result.message, url: page.url() };
}
