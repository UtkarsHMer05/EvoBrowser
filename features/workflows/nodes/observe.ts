import type { Stagehand } from "@browserbasehq/stagehand";

import { highlightElements } from "@/features/workflows/lib/highlight-element";

export async function observe({
  stagehand,
  instruction,
}: {
  stagehand: Stagehand;
  instruction: string;
}) {
  const results = await stagehand.observe(instruction);
  const page = stagehand.context.pages()[0];

  const matches = results.map(({ selector, description }) => ({
    selector,
    description,
  }));

  // Draw a box around every matched element in the live view so the user can
  // see what the agent found. Cosmetic only — never fails the step.
  if (matches.length > 0) {
    await highlightElements(
      page,
      matches.map((m) => m.selector),
      {
        label: `Found ${matches.length} match${matches.length === 1 ? "" : "es"}`,
        color: "amber",
        durationMs: 3500,
      },
    );
  }

  return { matches };
}
