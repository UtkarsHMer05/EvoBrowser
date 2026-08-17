"use client";

import { useEffect, useState, useCallback } from "react";
import { Globe, Loader2, MonitorOff, WifiOff } from "lucide-react";
import { useLatestRunSteps } from "@/features/workflows/components/workflow-runs-provider";

// How often to retry fetching the live-view URL while we're waiting for the
// Browserbase session to become available for debugging. Browserbase returns
// 404 until the session is RUNNING, so we poll tightly — a 1s interval means
// the view connects within ~1s of the session becoming ready.
const RETRY_INTERVAL_MS = 1000;
const MAX_RETRIES = 30;

type LiveBrowserStatus =
  | "waiting" // No session ID yet; browser step hasn't started
  | "connecting" // Fetching the debug URL from the server
  | "live" // iframe is loaded with the debug URL
  | "unavailable" // Session exists but debug URL couldn't be fetched
  | "ended"; // Session was live but is no longer (run finished)

interface LiveBrowserProps {
  /** The Browserbase session ID to show a live view for, or undefined if not yet available. */
  sessionId: string | undefined;
  /** The Trigger.dev run id that owns the session — the live-view route uses it
   *  to verify the caller is allowed to watch this session. */
  runId: string | undefined;
  /** Whether the parent run is still executing. When false and we had a session, transition to "ended". */
  isRunLive: boolean;
}

export function LiveBrowser({ sessionId, runId, isRunLive }: LiveBrowserProps) {
  const [debugUrl, setDebugUrl] = useState<string | null>(null);
  const [status, setStatus] = useState<LiveBrowserStatus>("waiting");
  // Seconds spent in the "connecting" state, so the user sees the wait is
  // progressing rather than looking frozen.
  const [connectElapsed, setConnectElapsed] = useState(0);
  // Track the session ID that we last connected to, so we can reset on change.
  const [trackedSessionId, setTrackedSessionId] = useState<string | undefined>(
    undefined,
  );

  // Reflect the currently running step from realtime Trigger.dev metadata
  const { steps } = useLatestRunSteps();
  const activeStep = isRunLive
    ? steps.find((s) => s.status === "running")
    : undefined;

  // When sessionId prop changes, reset everything for the new session.
  if (sessionId !== trackedSessionId) {
    setTrackedSessionId(sessionId);
    setDebugUrl(null);
    setConnectElapsed(0);
    if (!sessionId) {
      setStatus("waiting");
    } else {
      setStatus("connecting");
    }
  }

  // Tick a visible "connecting for Ns" counter so the wait reads as progress.
  useEffect(() => {
    if (status !== "connecting") return;
    const timer = setInterval(() => setConnectElapsed((s) => s + 1), 1000);
    return () => clearInterval(timer);
  }, [status]);

  // When run ends and we were live or connecting, transition to "ended".
  if (
    !isRunLive &&
    (status === "live" || status === "connecting") &&
    trackedSessionId
  ) {
    setStatus("ended");
  }

  const fetchDebugUrl = useCallback(async (sid: string, rid: string) => {
    try {
      const res = await fetch(
        `/api/live-view/${sid}?runId=${encodeURIComponent(rid)}`,
      );
      if (!res.ok) return null;
      const data = await res.json();
      return data.debuggerFullscreenUrl as string | null;
    } catch {
      return null;
    }
  }, []);

  useEffect(() => {
    // Only try connecting when we have a session and its run, no URL yet, and
    // the run is live.
    if (!sessionId || !runId || debugUrl || !isRunLive || status === "unavailable") return;

    let cancelled = false;
    let retries = 0;

    const tryConnect = async () => {
      const url = await fetchDebugUrl(sessionId, runId);
      if (cancelled) return;

      if (url) {
        setDebugUrl(url);
        setStatus("live");
      } else if (retries < MAX_RETRIES) {
        retries++;
        setTimeout(() => {
          if (!cancelled) tryConnect();
        }, RETRY_INTERVAL_MS);
      } else {
        setStatus("unavailable");
      }
    };

    tryConnect();

    return () => {
      cancelled = true;
    };
  }, [sessionId, runId, debugUrl, isRunLive, status, fetchDebugUrl]);

  return (
    <div className="relative flex size-full flex-col overflow-hidden bg-black/95">
      {/* Header bar */}
      <div className="flex shrink-0 items-center justify-between gap-2 border-b border-border bg-background px-3 py-1.5">
        <div className="flex items-center gap-2 min-w-0">
          <Globe className="size-3.5 text-muted-foreground shrink-0" />
          <span className="text-xs font-semibold text-foreground shrink-0">
            Live Browser
          </span>
          {activeStep && isRunLive && (
            <span className="flex items-center gap-1.5 rounded-full bg-blue-500/10 border border-blue-500/20 px-2 py-0.5 text-[10px] font-medium text-blue-500 dark:text-blue-400 truncate max-w-[220px]">
              <span className="relative flex size-1.5 shrink-0">
                <span className="absolute inline-flex size-full animate-ping rounded-full bg-blue-400 opacity-75" />
                <span className="relative inline-flex size-1.5 rounded-full bg-blue-500" />
              </span>
              <span className="truncate">Running: {activeStep.title}</span>
            </span>
          )}
        </div>
        <div className="flex items-center gap-2 shrink-0">
          {status === "live" && (
            <span className="flex items-center gap-1.5 text-[10px] font-semibold text-emerald-500">
              <span className="relative flex size-1.5">
                <span className="absolute inline-flex size-full animate-ping rounded-full bg-emerald-400 opacity-75" />
                <span className="relative inline-flex size-1.5 rounded-full bg-emerald-500" />
              </span>
              LIVE
            </span>
          )}
          {status === "connecting" && (
            <span className="flex items-center gap-1 text-[10px] text-muted-foreground">
              <Loader2 className="size-2.5 animate-spin" />
              Connecting…
            </span>
          )}
          {status === "ended" && (
            <span className="text-[10px] text-muted-foreground">
              Session ended
            </span>
          )}
        </div>
      </div>

      {/* Content area */}
      {status === "live" && debugUrl && (
        <iframe
          src={debugUrl}
          title="Browserbase Live View"
          className="size-full flex-1 border-0"
          sandbox="allow-scripts allow-same-origin allow-popups"
          // Pointer events disabled for Phase 1 view-only mode to prevent
          // user input from conflicting with the agent&apos;s actions.
          style={{ pointerEvents: "none" }}
        />
      )}

      {status === "waiting" && (
        <div className="flex flex-1 flex-col items-center justify-center gap-3 p-4 text-center">
          <Globe className="size-8 text-muted-foreground/40" />
          <div className="space-y-1">
            <p className="text-xs font-medium text-muted-foreground">
              Waiting for browser session
            </p>
            <p className="text-[10px] text-muted-foreground/60">
              The live view will appear when a browser step starts running.
            </p>
          </div>
        </div>
      )}

      {status === "connecting" && (
        <div className="flex flex-1 flex-col items-center justify-center gap-3 p-4 text-center">
          <Loader2 className="size-6 animate-spin text-muted-foreground/50" />
          <div className="space-y-1">
            <p className="text-xs text-muted-foreground">
              Connecting to live session…{" "}
              <span className="tabular-nums">{connectElapsed}s</span>
            </p>
            <p className="text-[10px] text-muted-foreground/60">
              The cloud browser is starting up. The view appears as soon as it
              is ready.
            </p>
          </div>
        </div>
      )}

      {status === "unavailable" && (
        <div className="flex flex-1 flex-col items-center justify-center gap-3 p-4 text-center">
          <WifiOff className="size-8 text-muted-foreground/40" />
          <div className="space-y-1">
            <p className="text-xs font-medium text-muted-foreground">
              Live view unavailable
            </p>
            <p className="text-[10px] text-muted-foreground/60">
              The browser session couldn&apos;t be reached.
              <br />
              The session recording will be available after the run completes.
            </p>
          </div>
        </div>
      )}

      {status === "ended" && (
        <div className="flex flex-1 flex-col items-center justify-center gap-3 p-4 text-center">
          <MonitorOff className="size-8 text-muted-foreground/40" />
          <div className="space-y-1">
            <p className="text-xs font-medium text-muted-foreground">
              Browser session ended
            </p>
            <p className="text-[10px] text-muted-foreground/60">
              The recording will be available in the run&apos;s replay.
            </p>
          </div>
        </div>
      )}
    </div>
  );
}
