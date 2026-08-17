import type { Stagehand } from "@browserbasehq/stagehand";

import { showStatusChip } from "@/features/workflows/lib/highlight-element";

export async function openUrl({
  stagehand,
  url,
}: {
  stagehand: Stagehand;
  url: string;
}) {
  const page = stagehand.context.pages()[0];
  await page.goto(url, { waitUntil: "load", timeoutMs: 30_000 });

  // Any overlay injected before goto is wiped by the navigation, so confirm
  // arrival after the page loads. Cosmetic only — never fails the step.
  let host = url;
  try {
    host = new URL(url).hostname;
  } catch {
    /* keep the raw url as the label */
  }
  await showStatusChip(page, `Navigated to ${host}`, {
    color: "green",
    durationMs: 2500,
  });

  return { url: page.url(), title: await page.title() };
}
