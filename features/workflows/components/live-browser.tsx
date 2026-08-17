"use client";

import { useEffect, useState, useCallback } from "react";
import { Globe, Loader2, MonitorOff, WifiOff } from "lucide-react";

// How often to retry fetching the live-view URL while we're waiting for the
// Browserbase session to become available for debugging.
const RETRY_INTERVAL_MS = 2500;
const MAX_RETRIES = 8;

type LiveBrowserStatus =
  | "waiting" // No session ID yet; browser step hasn't started
  | "connecting" // Fetching the debug URL from the server
  | "live" // iframe is loaded with the debug URL
  | "unavailable" // Session exists but debug URL couldn't be fetched
  | "ended"; // Session was live but is no longer (run finished)

interface LiveBrowserProps {
  /** The Browserbase session ID to show a live view for, or undefined if not yet available. */
  sessionId: string | undefined;
  /** Whether the parent run is still executing. When false and we had a session, transition to "ended". */
  isRunLive: boolean;
}

export function LiveBrowser({ sessionId, isRunLive }: LiveBrowserProps) {
  const [debugUrl, setDebugUrl] = useState<string | null>(null);
  const [status, setStatus] = useState<LiveBrowserStatus>("waiting");
  // Track the session ID that we last connected to, so we can reset on change.
  const [trackedSessionId, setTrackedSessionId] = useState<string | undefined>(
    undefined,
  );

  // When sessionId prop changes, reset everything for the new session.
  if (sessionId !== trackedSessionId) {
    setTrackedSessionId(sessionId);
    setDebugUrl(null);
    if (!sessionId) {
      setStatus("waiting");
    } else {
      setStatus("connecting");
    }
  }

  // When run ends and we were live or connecting, transition to "ended".
  if (
    !isRunLive &&
    (status === "live" || status === "connecting") &&
    trackedSessionId
  ) {
    setStatus("ended");
  }

  const fetchDebugUrl = useCallback(async (sid: string) => {
    try {
      const res = await fetch(`/api/live-view/${sid}`);
      if (!res.ok) return null;
      const data = await res.json();
      return data.debuggerFullscreenUrl as string | null;
    } catch {
      return null;
    }
  }, []);

  useEffect(() => {
    // Only try connecting when we have a session, no URL yet, and run is live.
    if (!sessionId || debugUrl || !isRunLive || status === "unavailable") return;

    let cancelled = false;
    let retries = 0;

    const tryConnect = async () => {
      const url = await fetchDebugUrl(sessionId);
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
  }, [sessionId, debugUrl, isRunLive, status, fetchDebugUrl]);

  return (
    <div className="relative flex size-full flex-col overflow-hidden bg-black/95">
      {/* Header bar */}
      <div className="flex shrink-0 items-center gap-2 border-b border-border bg-background px-3 py-1.5">
        <Globe className="size-3.5 text-muted-foreground" />
        <span className="text-xs font-semibold text-foreground">
          Live Browser
        </span>
        {status === "live" && (
          <span className="ml-auto flex items-center gap-1.5 text-[10px] text-emerald-500">
            <span className="relative flex size-1.5">
              <span className="absolute inline-flex size-full animate-ping rounded-full bg-emerald-400 opacity-75" />
              <span className="relative inline-flex size-1.5 rounded-full bg-emerald-500" />
            </span>
            LIVE
          </span>
        )}
        {status === "connecting" && (
          <span className="ml-auto text-[10px] text-muted-foreground">
            Connecting…
          </span>
        )}
        {status === "ended" && (
          <span className="ml-auto text-[10px] text-muted-foreground">
            Session ended
          </span>
        )}
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
          <p className="text-xs text-muted-foreground">
            Connecting to live session…
          </p>
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
