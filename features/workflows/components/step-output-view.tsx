"use client";

import {
  CheckCircle2,
  ExternalLink,
  Mail,
  MousePointerClick,
  ScanSearch,
  Sparkles,
  XCircle,
} from "lucide-react";

import type { RunStep } from "@/features/workflows/tasks/run-workflow";

// Renders a step's raw output as something a person can actually read, keyed by
// node type. The results popup and the inspector's step view both use it, so
// nobody has to stare at JSON.stringify to learn what the automation did.

function isRecord(value: unknown): value is Record<string, unknown> {
  return typeof value === "object" && value !== null && !Array.isArray(value);
}

function asString(value: unknown): string | undefined {
  return typeof value === "string" && value.length > 0 ? value : undefined;
}

function asBoolean(value: unknown): boolean | undefined {
  return typeof value === "boolean" ? value : undefined;
}

// A clickable external link row.
function LinkRow({ url, label }: { url: string; label?: string }) {
  return (
    <a
      href={url}
      target="_blank"
      rel="noreferrer"
      className="flex items-center gap-1.5 text-xs text-blue-500 hover:underline dark:text-blue-400"
    >
      <span className="truncate font-mono">{label ?? url}</span>
      <ExternalLink className="size-3 shrink-0" />
    </a>
  );
}

// A small labeled field.
function Field({ label, children }: { label: string; children: React.ReactNode }) {
  return (
    <div className="space-y-1">
      <div className="text-[10px] font-medium uppercase tracking-wide text-muted-foreground">
        {label}
      </div>
      {children}
    </div>
  );
}

// Success/failure badge for boolean outcomes.
function OutcomeBadge({ ok, yes, no }: { ok: boolean; yes: string; no: string }) {
  return ok ? (
    <span className="inline-flex items-center gap-1 rounded-full bg-emerald-500/10 px-2 py-0.5 text-[11px] font-medium text-emerald-600 dark:text-emerald-400">
      <CheckCircle2 className="size-3" />
      {yes}
    </span>
  ) : (
    <span className="inline-flex items-center gap-1 rounded-full bg-destructive/10 px-2 py-0.5 text-[11px] font-medium text-destructive">
      <XCircle className="size-3" />
      {no}
    </span>
  );
}

function OpenUrlOutput({ output }: { output: unknown }) {
  const url = isRecord(output) ? asString(output.url) : undefined;
  const title = isRecord(output) ? asString(output.title) : undefined;
  return (
    <div className="space-y-2.5">
      {title && (
        <Field label="Page title">
          <p className="text-xs font-medium">{title}</p>
        </Field>
      )}
      {url && (
        <Field label="Opened URL">
          <LinkRow url={url} />
        </Field>
      )}
    </div>
  );
}

function ActOutput({ output }: { output: unknown }) {
  const success = isRecord(output) ? asBoolean(output.success) : undefined;
  const message = isRecord(output) ? asString(output.message) : undefined;
  const url = isRecord(output) ? asString(output.url) : undefined;
  return (
    <div className="space-y-2.5">
      {success !== undefined && (
        <OutcomeBadge ok={success} yes="Action succeeded" no="Action failed" />
      )}
      {message && (
        <Field label="What happened">
          <p className="text-xs leading-relaxed">{message}</p>
        </Field>
      )}
      {url && (
        <Field label="Page after action">
          <LinkRow url={url} />
        </Field>
      )}
    </div>
  );
}

function ExtractOutput({ output }: { output: unknown }) {
  const extraction = isRecord(output) ? output.extraction : undefined;
  return (
    <div className="space-y-2.5">
      <Field label="Extracted data">
        {typeof extraction === "string" ? (
          <p className="max-h-72 overflow-y-auto whitespace-pre-wrap break-words rounded-md border border-border/60 bg-muted/30 p-2.5 text-xs leading-relaxed">
            {extraction}
          </p>
        ) : extraction !== undefined ? (
          // Structured extractions (arrays/objects) get a readable list or a
          // pretty-printed block rather than a single-line JSON blob.
          <div className="max-h-72 space-y-1.5 overflow-y-auto">
            {Array.isArray(extraction) ? (
              extraction.map((item, i) => (
                <div
                  key={i}
                  className="rounded-md border border-border/60 bg-muted/30 p-2 text-xs"
                >
                  {isRecord(item)
                    ? Object.entries(item).map(([k, v]) => (
                        <div key={k} className="flex gap-2 py-0.5">
                          <span className="shrink-0 font-medium text-muted-foreground">
                            {k}:
                          </span>
                          <span className="break-words">
                            {typeof v === "string" ? v : JSON.stringify(v)}
                          </span>
                        </div>
                      ))
                    : String(item)}
                </div>
              ))
            ) : (
              <pre className="overflow-x-auto whitespace-pre-wrap break-words rounded-md border border-border/60 bg-muted/30 p-2.5 font-mono text-[11px]">
                {JSON.stringify(extraction, null, 2)}
              </pre>
            )}
          </div>
        ) : (
          <p className="text-xs text-muted-foreground">Nothing was extracted.</p>
        )}
      </Field>
    </div>
  );
}

function ObserveOutput({ output }: { output: unknown }) {
  const matches = isRecord(output) && Array.isArray(output.matches)
    ? (output.matches as Array<{ selector?: string; description?: string }>)
    : [];
  return (
    <div className="space-y-2.5">
      <Field label={`Elements found (${matches.length})`}>
        {matches.length === 0 ? (
          <p className="text-xs text-muted-foreground">No matching elements.</p>
        ) : (
          <div className="max-h-72 space-y-1.5 overflow-y-auto">
            {matches.map((m, i) => (
              <div
                key={i}
                className="rounded-md border border-border/60 bg-muted/30 p-2"
              >
                {m.description && (
                  <p className="text-xs font-medium">{m.description}</p>
                )}
                {m.selector && (
                  <p className="mt-0.5 break-all font-mono text-[10px] text-muted-foreground">
                    {m.selector}
                  </p>
                )}
              </div>
            ))}
          </div>
        )}
      </Field>
    </div>
  );
}

function AgentOutput({ output }: { output: unknown }) {
  const success = isRecord(output) ? asBoolean(output.success) : undefined;
  const completed = isRecord(output) ? asBoolean(output.completed) : undefined;
  const message = isRecord(output) ? asString(output.message) : undefined;
  return (
    <div className="space-y-2.5">
      {success !== undefined && (
        <OutcomeBadge
          ok={success && completed !== false}
          yes="Agent finished"
          no="Agent did not finish"
        />
      )}
      {message && (
        <Field label="Agent summary">
          <p className="max-h-72 overflow-y-auto whitespace-pre-wrap break-words text-xs leading-relaxed">
            {message}
          </p>
        </Field>
      )}
    </div>
  );
}

function SendEmailOutput({ output }: { output: unknown }) {
  const id = isRecord(output) ? asString(output.id) : undefined;
  return (
    <div className="space-y-2.5">
      <OutcomeBadge ok yes="Email sent" no="Email failed" />
      {id && (
        <Field label="Delivery ID">
          <p className="break-all font-mono text-[10px] text-muted-foreground">
            {id}
          </p>
        </Field>
      )}
    </div>
  );
}

// Fallback for unknown node types or unexpected shapes: pretty JSON, still
// better than nothing.
function FallbackOutput({ output }: { output: unknown }) {
  return (
    <pre className="max-h-72 overflow-auto whitespace-pre-wrap break-words rounded-md border border-border/60 bg-muted/30 p-2.5 font-mono text-[11px]">
      {JSON.stringify(output, null, 2)}
    </pre>
  );
}

const TYPE_ICONS: Record<string, React.ComponentType<{ className?: string }>> = {
  "open-url": ExternalLink,
  act: MousePointerClick,
  extract: ScanSearch,
  observe: ScanSearch,
  agent: Sparkles,
  "send-email": Mail,
};

// The readable view of one step's result: icon + status, then the type-specific
// body. Errors are shown verbatim in red.
export function StepOutputView({ step }: { step: RunStep }) {
  if (step.error) {
    return (
      <div className="space-y-2">
        <OutcomeBadge ok={false} yes="" no="Step failed" />
        <Field label="Error">
          <p className="whitespace-pre-wrap break-words rounded-md border border-destructive/30 bg-destructive/5 p-2.5 text-xs leading-relaxed text-destructive">
            {step.error}
          </p>
        </Field>
      </div>
    );
  }

  if (step.output === undefined) {
    return (
      <p className="text-xs text-muted-foreground">This step produced no output.</p>
    );
  }

  const Icon = TYPE_ICONS[step.type];

  return (
    <div className="space-y-3">
      {Icon && (
        <div className="flex items-center gap-1.5 text-[11px] text-muted-foreground">
          <Icon className="size-3.5" />
          <span>
            {step.type === "open-url" && "Opened a page"}
            {step.type === "act" && "Performed an action"}
            {step.type === "extract" && "Extracted data from the page"}
            {step.type === "observe" && "Located elements on the page"}
            {step.type === "agent" && "Autonomous agent result"}
            {step.type === "send-email" && "Sent an email"}
          </span>
        </div>
      )}
      {step.type === "open-url" && <OpenUrlOutput output={step.output} />}
      {step.type === "act" && <ActOutput output={step.output} />}
      {step.type === "extract" && <ExtractOutput output={step.output} />}
      {step.type === "observe" && <ObserveOutput output={step.output} />}
      {step.type === "agent" && <AgentOutput output={step.output} />}
      {step.type === "send-email" && <SendEmailOutput output={step.output} />}
      {!TYPE_ICONS[step.type] && <FallbackOutput output={step.output} />}
    </div>
  );
}
