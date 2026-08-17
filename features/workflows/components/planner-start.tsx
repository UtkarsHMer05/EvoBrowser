"use client";

import { useCallback, useState } from "react";
import { Sparkles, Wrench } from "lucide-react";

import { Button } from "@/components/ui/button";
import { Spinner } from "@/components/ui/spinner";
import { Textarea } from "@/components/ui/textarea";

// Example prompts shown as light suggestions below the input so a blank screen
// never feels empty. Clicking one fills the textarea.
const EXAMPLE_PROMPTS = [
  "Scrape product prices from a competitor and email me a summary",
  "Log into my dashboard, extract weekly metrics, and send a Slack digest",
  "Open a job board, find new listings matching my criteria, and save them",
];

interface PlannerStartProps {
  /** Called when the user clicks "Build manually" to dismiss the planner. */
  onBuildManually: () => void;

  /**
   * Called when the user submits a prompt. The component enters a loading state
   * until the returned promise settles. Not wired to an AI API yet — Milestone 4
   * will supply the real implementation; for now callers can pass a no-op.
   */
  onGenerate?: (prompt: string) => Promise<void>;
}

export function PlannerStart({
  onBuildManually,
  onGenerate,
}: PlannerStartProps) {
  const [prompt, setPrompt] = useState("");
  const [isGenerating, setIsGenerating] = useState(false);

  const canGenerate = prompt.trim().length > 0 && !isGenerating;

  const handleGenerate = useCallback(async () => {
    if (!canGenerate || !onGenerate) return;
    setIsGenerating(true);
    try {
      await onGenerate(prompt.trim());
    } finally {
      setIsGenerating(false);
    }
  }, [canGenerate, onGenerate, prompt]);

  // Submit on Cmd/Ctrl + Enter when the textarea is focused.
  const handleKeyDown = (e: React.KeyboardEvent<HTMLTextAreaElement>) => {
    if (e.key === "Enter" && (e.metaKey || e.ctrlKey)) {
      e.preventDefault();
      handleGenerate();
    }
  };

  return (
    <div className="flex size-full items-center justify-center bg-background p-6">
      <div className="flex w-full max-w-xl flex-col items-center gap-8 text-center">
        {/* Icon + heading */}
        <div className="flex flex-col items-center gap-3">
          <div className="flex size-12 items-center justify-center rounded-2xl bg-primary/10 text-primary ring-1 ring-primary/20">
            <Sparkles className="size-6" />
          </div>
          <h1 className="text-lg font-semibold tracking-tight">
            What do you want to automate?
          </h1>
          <p className="max-w-sm text-sm text-muted-foreground">
            Describe your goal in plain language and the AI planner will generate
            a ready-to-run workflow.
          </p>
        </div>

        {/* Prompt input */}
        <div className="flex w-full flex-col gap-3">
          <Textarea
            value={prompt}
            onChange={(e) => setPrompt(e.target.value)}
            onKeyDown={handleKeyDown}
            placeholder="e.g. Go to Hacker News, extract the top 5 posts, and email me the titles and links…"
            disabled={isGenerating}
            rows={4}
            className="min-h-28 resize-none text-sm"
            autoFocus
          />

          {/* Example prompt chips */}
          <div className="flex flex-wrap justify-center gap-1.5">
            {EXAMPLE_PROMPTS.map((example) => (
              <button
                key={example}
                type="button"
                disabled={isGenerating}
                onClick={() => setPrompt(example)}
                className="rounded-md border border-border bg-card px-2 py-1 text-xs text-muted-foreground transition-colors hover:bg-accent hover:text-accent-foreground disabled:pointer-events-none disabled:opacity-50"
              >
                {example.length > 50
                  ? example.slice(0, 47) + "…"
                  : example}
              </button>
            ))}
          </div>
        </div>

        {/* Actions */}
        <div className="flex items-center gap-3">
          <Button
            variant="outline"
            size="sm"
            onClick={onBuildManually}
            disabled={isGenerating}
            className="gap-2"
          >
            <Wrench className="size-3.5" />
            Build manually
          </Button>

          <Button
            size="sm"
            disabled={!canGenerate}
            onClick={handleGenerate}
            className="gap-2"
          >
            {isGenerating ? (
              <Spinner className="size-3.5" />
            ) : (
              <Sparkles className="size-3.5" />
            )}
            {isGenerating ? "Generating…" : "Generate Workflow"}
          </Button>
        </div>

        {/* Keyboard hint */}
        <p className="text-xs text-muted-foreground/60">
          <kbd className="rounded border border-border bg-muted px-1 py-0.5 font-mono text-[0.65rem]">
            ⌘
          </kbd>{" "}
          +{" "}
          <kbd className="rounded border border-border bg-muted px-1 py-0.5 font-mono text-[0.65rem]">
            Enter
          </kbd>{" "}
          to generate
        </p>
      </div>
    </div>
  );
}
