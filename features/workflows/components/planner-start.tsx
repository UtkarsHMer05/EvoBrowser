"use client";

import { useCallback, useState } from "react";
import { Sparkles, Wrench } from "lucide-react";

import { Button } from "@/components/ui/button";
import { Spinner } from "@/components/ui/spinner";
import { Textarea } from "@/components/ui/textarea";
import { cn } from "@/lib/utils";

// Maximum allowed characters for the automation goal prompt.
export const MAX_PROMPT_LENGTH = 2000;

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
   * until the returned promise settles. Receives the clean, trimmed goal text.
   */
  onGenerate?: (goal: string) => Promise<void>;
}

export function PlannerStart({
  onBuildManually,
  onGenerate,
}: PlannerStartProps) {
  const [prompt, setPrompt] = useState("");
  const [isGenerating, setIsGenerating] = useState(false);
  const [error, setError] = useState<string | null>(null);

  const trimmedPrompt = prompt.trim();
  const isOverLimit = prompt.length > MAX_PROMPT_LENGTH;
  const isEmpty = trimmedPrompt.length === 0;
  const canGenerate = !isEmpty && !isOverLimit && !isGenerating;

  const handleGenerate = useCallback(async () => {
    if (!canGenerate || !onGenerate) return;

    setIsGenerating(true);
    setError(null);

    try {
      await onGenerate(trimmedPrompt);
    } catch (err) {
      // Preserve prompt in state and display the error feedback so user can edit & retry.
      const message =
        err instanceof Error
          ? err.message
          : "Failed to process automation goal. Please try again.";
      setError(message);
    } finally {
      setIsGenerating(false);
    }
  }, [canGenerate, onGenerate, trimmedPrompt]);

  // Submit on Cmd/Ctrl + Enter only. Plain Enter creates a regular multiline newline.
  const handleKeyDown = (e: React.KeyboardEvent<HTMLTextAreaElement>) => {
    if (e.key === "Enter" && (e.metaKey || e.ctrlKey)) {
      e.preventDefault();
      handleGenerate();
    }
  };

  const handlePromptChange = (e: React.ChangeEvent<HTMLTextAreaElement>) => {
    setPrompt(e.target.value);
    if (error) setError(null);
  };

  const handleSelectExample = (example: string) => {
    setPrompt(example);
    if (error) setError(null);
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

        {/* Prompt input + validation feedback */}
        <div className="flex w-full flex-col gap-2">
          <div className="relative flex flex-col gap-1.5 text-left">
            <Textarea
              value={prompt}
              onChange={handlePromptChange}
              onKeyDown={handleKeyDown}
              placeholder="e.g. Go to Hacker News, extract the top 5 posts, and email me the titles and links…"
              disabled={isGenerating}
              rows={4}
              aria-invalid={isOverLimit || Boolean(error)}
              aria-describedby="prompt-feedback prompt-counter"
              className={cn(
                "min-h-28 resize-none text-sm",
                isOverLimit && "border-destructive focus-visible:ring-destructive/20",
              )}
              autoFocus
            />

            {/* Validation & Character count row */}
            <div className="flex items-center justify-between px-1 text-xs text-muted-foreground">
              <div id="prompt-feedback" className="truncate pr-2">
                {isOverLimit ? (
                  <span className="text-destructive">
                    Prompt exceeds maximum limit of {MAX_PROMPT_LENGTH.toLocaleString()} characters.
                  </span>
                ) : error ? (
                  <span role="alert" className="text-destructive">
                    {error}
                  </span>
                ) : null}
              </div>
              <span
                id="prompt-counter"
                className={cn(
                  "shrink-0 tabular-nums",
                  isOverLimit ? "font-semibold text-destructive" : "text-muted-foreground/80",
                )}
              >
                {prompt.length.toLocaleString()} / {MAX_PROMPT_LENGTH.toLocaleString()}
              </span>
            </div>
          </div>

          {/* Example prompt chips */}
          <div className="flex flex-wrap justify-center gap-1.5 pt-1">
            {EXAMPLE_PROMPTS.map((example) => (
              <button
                key={example}
                type="button"
                disabled={isGenerating}
                onClick={() => handleSelectExample(example)}
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

