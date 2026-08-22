// Run-startup diagnosis: turn "the console shows Running… 0/0 forever" into an
// actionable explanation instead of a silent hang.
//
// The failure mode this exists for: a run is created (so the UI shows it live)
// but NOTHING ever executes it. In dev that means `trigger.dev dev` isn't
// running; in production it almost always means the run task was never
// deployed to the Trigger.dev environment the app's TRIGGER_SECRET_KEY targets,
// or the deployed task is missing its runtime env vars (Railway's env vars do
// NOT reach the task — tasks execute on Trigger.dev infrastructure). The user
// sees "Running… 0/0 steps" indefinitely; this helper powers a banner that
// says exactly what to check.

export type RunEngineKind = "legacy" | "evo";

export type RunStartupDiagnosis =
  | { kind: "none" }
  // The realtime subscription itself failed — the console may look frozen even
  // though runs are executing.
  | { kind: "connection-error"; message: string }
  // A live run with zero published steps past the grace window: nothing picked
  // it up (or the executor died before its first metadata publish).
  | { kind: "not-picked-up"; seconds: number; engine: RunEngineKind };

// Generous grace period: queue wait on Trigger.dev free tier can take a few
// seconds, and Stagehand session open happens before any step publishes only
// AFTER the first step starts — 45s of literally zero step activity means
// something is structurally wrong, not slow.
export const STARTUP_STUCK_AFTER_MS = 45_000;

export interface RunStartupInput {
  /** The newest console run, if any (runs are sorted newest-first). */
  latest?: {
    id: string;
    engine: RunEngineKind;
    isLive: boolean;
    stepCount: number;
    createdAtMs: number;
  };
  /** Message from the realtime runs subscription, when it errored. */
  connectionErrorMessage?: string;
  nowMs: number;
}

export function diagnoseRunStartup(input: RunStartupInput): RunStartupDiagnosis {
  const { latest, connectionErrorMessage, nowMs } = input;

  if (connectionErrorMessage) {
    return { kind: "connection-error", message: connectionErrorMessage };
  }

  if (
    latest &&
    latest.isLive &&
    latest.stepCount === 0 &&
    nowMs - latest.createdAtMs >= STARTUP_STUCK_AFTER_MS
  ) {
    return {
      kind: "not-picked-up",
      seconds: Math.floor((nowMs - latest.createdAtMs) / 1000),
      engine: latest.engine,
    };
  }

  return { kind: "none" };
}
