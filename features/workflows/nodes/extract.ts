import type { Stagehand } from "@browserbasehq/stagehand";

import { showStatusChip } from "@/features/workflows/lib/highlight-element";

export async function extract({
  stagehand,
  instruction,
}: {
  stagehand: Stagehand;
  instruction: string;
}) {
  const page = stagehand.context.pages()[0];

  // Extract reads the whole page rather than one element, so instead of a box
  // we show a status chip in the live view while it works. Long duration so it
  // stays up through the extraction; the "done" chip below replaces it.
  await showStatusChip(page, "Extracting data…", {
    color: "amber",
    durationMs: 20000,
  });

  const { extraction } = await stagehand.extract(instruction);

  // Confirm completion in the live view.
  await showStatusChip(page, "Extraction complete", {
    color: "green",
    durationMs: 2500,
  });

  return { extraction };
}
