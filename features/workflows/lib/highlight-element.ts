/**
 * Live-view element highlighting.
 *
 * The Live Browser panel shows a cross-origin Browserbase video stream, so the
 * app cannot draw on top of it from the outside. Instead we inject a real DOM
 * overlay node *into the page the agent is driving* via `page.evaluate()`.
 * Because it is a genuine DOM element in the actual browser, it is composited
 * into the streamed viewport and shows up in the live view — giving the user a
 * visible box around whatever the AI is clicking, matching, or extracting.
 *
 * NOTE: Stagehand's built-in `Locator.highlight()` uses the CDP Overlay domain,
 * which DevTools renders but the video stream does NOT capture — so it would be
 * invisible in the live view. DOM injection is the correct approach here.
 *
 * Highlighting is purely cosmetic: every entry point swallows its own errors so
 * it can never fail a workflow step.
 */

// The executors only need `evaluate`, so depend on that narrow structural
// shape rather than importing Stagehand's internal Page type. Both Stagehand's
// understudy Page and a Playwright page satisfy it.
type EvaluatablePage = {
  evaluate<R = unknown, Arg = unknown>(
    pageFunctionOrExpression: string | ((arg: Arg) => R | Promise<R>),
    arg?: Arg,
  ): Promise<R>;
};

export type HighlightColor = "blue" | "amber" | "green";

export interface HighlightOptions {
  /** Small chip label rendered at the top of the viewport, e.g. "Clicking". */
  label?: string;
  /** How long the overlay stays before auto-removing. Defaults to 2500ms. */
  durationMs?: number;
  /** Color scheme. Defaults to blue. */
  color?: HighlightColor;
}

const PALETTES: Record<
  HighlightColor,
  { border: string; fill: string; glow: string }
> = {
  blue: {
    border: "#3b82f6",
    fill: "rgba(59, 130, 246, 0.16)",
    glow: "rgba(59, 130, 246, 0.28)",
  },
  amber: {
    border: "#f59e0b",
    fill: "rgba(245, 158, 11, 0.14)",
    glow: "rgba(245, 158, 11, 0.25)",
  },
  green: {
    border: "#22c55e",
    fill: "rgba(34, 197, 94, 0.14)",
    glow: "rgba(34, 197, 94, 0.25)",
  },
};

interface HighlightPayload {
  selectors: string[];
  durationMs: number;
  label: string;
  border: string;
  fill: string;
  glow: string;
}

// Runs inside the driven browser page. Must be fully self-contained (only its
// parameter and browser globals) because `page.evaluate` serializes it. Type
// annotations are erased at compile time, so the serialized source is clean JS.
const highlightInPage = (payload: HighlightPayload) => {
  const { selectors, durationMs, label, border, fill, glow } = payload;
  try {
    // Resolve a selector to an element. Stagehand emits XPath (e.g.
    // "/html/body/div[2]/a[1]"); fall back to CSS for anything else.
    const resolve = (sel: string): Element | null => {
      if (!sel) return null;
      if (/^(\(|\.?\/)/.test(sel)) {
        try {
          // 9 === XPathResult.FIRST_ORDERED_NODE_TYPE
          const r = document.evaluate(sel, document, null, 9, null);
          if (r.singleNodeValue instanceof Element) return r.singleNodeValue;
        } catch {
          /* fall through to CSS */
        }
      }
      try {
        return document.querySelector(sel);
      } catch {
        return null;
      }
    };

    const els: Element[] = [];
    for (const sel of selectors) {
      const el = resolve(sel);
      if (el) els.push(el);
    }

    // Nothing to draw at all — no elements resolved and no status label.
    if (!els.length && !label) return { highlighted: 0 };

    // Drop any previous overlay so highlights never stack up.
    const old = document.getElementById("__evo_hl_root__");
    if (old) old.remove();

    // Bring the primary target into view before measuring so the box lands on
    // screen (instant scroll, so the rects read below are already settled).
    if (els.length) {
      try {
        els[0].scrollIntoView({ block: "center", inline: "center" });
      } catch {
        /* keep going with whatever is visible */
      }
    }

    const root = document.createElement("div");
    root.id = "__evo_hl_root__";
    root.style.cssText =
      "position:fixed;inset:0;z-index:2147483647;pointer-events:none;";
    (document.documentElement || document.body).appendChild(root);

    let count = 0;
    for (const el of els) {
      const rect = el.getBoundingClientRect();
      if (rect.width <= 0 && rect.height <= 0) continue;
      const box = document.createElement("div");
      box.style.cssText =
        `position:absolute;left:${rect.left}px;top:${rect.top}px;` +
        `width:${rect.width}px;height:${rect.height}px;` +
        `border:2px solid ${border};background:${fill};border-radius:6px;` +
        `box-shadow:0 0 0 3px ${glow};box-sizing:border-box;pointer-events:none;`;
      root.appendChild(box);
      count++;
    }

    // The status chip draws even when no element boxes resolved (e.g. an
    // extract step that reads the whole page), so the user still sees what the
    // agent is doing.
    if (label) {
      const chip = document.createElement("div");
      chip.textContent = label;
      chip.style.cssText =
        `position:absolute;top:10px;left:50%;transform:translateX(-50%);` +
        `background:${border};color:#fff;font:600 12px/1.2 system-ui,sans-serif;` +
        `padding:5px 12px;border-radius:999px;box-shadow:0 2px 8px rgba(0,0,0,.35);` +
        `pointer-events:none;white-space:nowrap;max-width:80vw;overflow:hidden;text-overflow:ellipsis;`;
      root.appendChild(chip);
    }

    // Auto-remove so the overlay never lingers into later steps.
    setTimeout(() => {
      try {
        root.remove();
      } catch {
        /* noop */
      }
    }, durationMs);

    return { highlighted: count };
  } catch {
    return { highlighted: 0 };
  }
};

/** Highlight one or more elements in the driven page's live view. */
export async function highlightElements(
  page: EvaluatablePage,
  selectors: string[],
  options: HighlightOptions = {},
): Promise<void> {
  const clean = selectors.filter(Boolean);
  const { label = "", durationMs = 2500, color = "blue" } = options;
  // A label alone still draws a status chip; bail only when there's nothing.
  if (!clean.length && !label) return;

  try {
    await page.evaluate(highlightInPage, {
      selectors: clean,
      durationMs,
      label,
      ...PALETTES[color],
    });
  } catch {
    // Cosmetic only — never fail a step because a highlight didn't draw.
  }
}

/** Highlight a single element. */
export function highlightElement(
  page: EvaluatablePage,
  selector: string,
  options: HighlightOptions = {},
): Promise<void> {
  return highlightElements(page, [selector], options);
}

/**
 * Show a status chip without any element box — for steps like extract that
 * read the whole page rather than targeting one element.
 */
export function showStatusChip(
  page: EvaluatablePage,
  label: string,
  options: Omit<HighlightOptions, "label"> = {},
): Promise<void> {
  return highlightElements(page, [], { ...options, label });
}

/** Remove the overlay immediately (e.g. right after the action completes). */
export async function clearHighlight(page: EvaluatablePage): Promise<void> {
  try {
    await page.evaluate(() => {
      const el = document.getElementById("__evo_hl_root__");
      if (el) el.remove();
      return true;
    });
  } catch {
    // Cosmetic only.
  }
}
