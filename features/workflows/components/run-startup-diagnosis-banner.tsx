"use client";

import { useEffect, useMemo, useState } from "react";
import { TriangleAlert } from "lucide-react";

import {
  diagnoseRunStartup,
  STARTUP_STUCK_AFTER_MS,
} from "@/features/workflows/lib/run-startup-diagnosis";
import {
  useConsoleRuns,
  useRunsConnectionError,
} from "@/features/workflows/components/workflow-runs-provider";

// A ticking 1s clock so elapsed-time diagnoses re-render without every run
// row subscribing to timers.
function useNowTicker(enabled: boolean) {
  const [now, setNow] = useState(() => Date.now());
  useEffect(() => {
    if (!enabled) return;
    const timer = setInterval(() => setNow(Date.now()), 1000);
    return () => clearInterval(timer);
  }, [enabled]);
  return now;
}

const COPY = {
  legacy: {
    title: "No worker has picked up this run",
    causes: [
      "Production: the run task must be DEPLOYED to the same Trigger.dev environment your app\u2019s TRIGGER_SECRET_KEY targets \u2014 run npx trigger.dev deploy --env prod from the repo.",
      "Production: the deployed task needs its own env vars in the Trigger.dev dashboard (Prod environment): DATABASE_URL, BROWSERBASE_API_KEY, RESEND_API_KEY. Railway variables do not reach task execution.",
      "Dev: make sure npx trigger.dev dev is running in another terminal.",
    ],
  },
  evo: {
    title: "The Evo engine is not executing this run",
    causes: [
      "Check EXECUTION_ENGINE=evo is intended and EVO_SCHEDULER_ADDR points at a healthy scheduler (scripts/phase2/up.sh + health.sh locally).",
      "Verify workers are running (npx tsx worker/src/main.ts) and share EVO_PHASE2_REDIS/PG settings with the scheduler.",
      "Confirm the run row exists in the Phase-2 store and the scheduler logs show submission.",
    ],
  },
} as const;

// Explains the worst silent failure in the product: a run that shows
// "Running… 0/0 steps" forever because nothing ever executed it. Renders an
// amber banner above the console once the newest live run has produced zero
// step activity past the grace window — or when the realtime connection
// itself errors. Dismissible per run.
export function RunStartupDiagnosisBanner() {
  const runs = useConsoleRuns();
  const connectionError = useRunsConnectionError();

  const latest = runs[0];
  const potentiallyStuck = Boolean(
    latest?.isLive && latest.steps.length === 0,
  );
  const now = useNowTicker(potentiallyStuck || Boolean(connectionError));

  const diagnosis = useMemo(
    () =>
      diagnoseRunStartup({
        latest: latest
          ? {
              id: latest.id,
              engine: latest.engine,
              isLive: latest.isLive,
              stepCount: latest.steps.length,
              createdAtMs: latest.createdAt.getTime(),
            }
          : undefined,
        connectionErrorMessage: connectionError,
        nowMs: now,
      }),
    [latest, connectionError, now],
  );

  const [dismissedRunId, setDismissedRunId] = useState<string | null>(null);
  if (
    diagnosis.kind === "none" ||
    (diagnosis.kind === "not-picked-up" &&
      dismissedRunId === runs[0]?.id)
  ) {
    return null;
  }

  if (diagnosis.kind === "connection-error") {
    return (
      <div className="flex items-start gap-2 rounded-md border border-amber-500/30 bg-amber-500/10 px-3 py-2 text-xs text-amber-700 dark:text-amber-400">
        <TriangleAlert className="mt-0.5 size-3.5 shrink-0" />
        <div className="space-y-0.5">
          <p className="font-semibold">Can&apos;t reach live run updates</p>
          <p className="text-[11px] opacity-80">
            The realtime connection to Trigger.dev failed ({diagnosis.message}
            ). Run status below may be stale \u2014 check your network and the
            TRIGGER_SECRET_KEY environment.
          </p>
        </div>
      </div>
    );
  }

  const copy = COPY[diagnosis.engine];
  return (
    <div className="flex items-start gap-2 rounded-md border border-amber-500/30 bg-amber-500/10 px-3 py-2 text-xs text-amber-700 dark:text-amber-400">
      <TriangleAlert className="mt-0.5 size-3.5 shrink-0" />
      <div className="min-w-0 flex-1 space-y-1">
        <p className="font-semibold">
          {copy.title} ({diagnosis.seconds}s and counting)
        </p>
        <ul className="list-disc space-y-0.5 pl-4 text-[11px] leading-relaxed opacity-90">
          {copy.causes.map((c) => (
            <li key={c}>{c}</li>
          ))}
        </ul>
        <button
          type="button"
          onClick={() => setDismissedRunId(runs[0]?.id ?? null)}
          className="text-[11px] font-medium underline underline-offset-2 hover:opacity-80"
        >
          Dismiss for this run
        </button>
      </div>
    </div>
  );
}

// Exported so tests/consumers can reference the same threshold.
export { STARTUP_STUCK_AFTER_MS };
