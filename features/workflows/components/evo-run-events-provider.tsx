"use client";

// Phase 2 — shared Evo run events provider (Milestone 28).
//
// One EventSource per run, shared by every component that needs the run's
// live state (M28 step 6: no independent unbounded subscription per tiny UI
// component). The browser never sees Redis — it talks only to the authorized
// SSE route /api/runs/[runId]/events, which enforces Clerk org ownership.
//
// Reconnect semantics: EventSource reconnects automatically and sends
// Last-Event-ID; the route replays every event after that id, and the reducer
// (reduceEvoEvents) is idempotent per (kind, node_id), so duplicate deliveries
// never corrupt the view. A `snapshot` frame (durable-state fallback) replaces
// the accumulated view wholesale.

import {
  createContext,
  useContext,
  useEffect,
  useMemo,
  useRef,
  useState,
} from "react";

import {
  reduceEvoEvents,
  type EvoRunEvent,
  type NormalizedRunViewModel,
} from "@/features/workflows/lib/run-view-model";

interface EvoRunEventsContextValue {
  /** The current normalized view, or undefined before the first frame. */
  view: NormalizedRunViewModel | undefined;
  /** Transport error (e.g. 404/401 surfaced as EventSource error). */
  error?: Error;
}

const EvoRunEventsContext = createContext<EvoRunEventsContextValue | null>(
  null,
);

interface EvoRunEventsProviderProps {
  runId: string;
  children: React.ReactNode;
}

export function EvoRunEventsProvider({
  runId,
  children,
}: EvoRunEventsProviderProps) {
  // Keyed remount per run resets all state without synchronous setState in
  // an effect (the lint rule react-hooks/set-state-in-effect guards against
  // cascading renders from that pattern).
  return (
    <EvoRunEventsProviderInner key={runId} runId={runId}>
      {children}
    </EvoRunEventsProviderInner>
  );
}

function EvoRunEventsProviderInner({
  runId,
  children,
}: EvoRunEventsProviderProps) {
  const [events, setEvents] = useState<EvoRunEvent[]>([]);
  const [snapshot, setSnapshot] = useState<NormalizedRunViewModel | undefined>(
    undefined,
  );
  const [error, setError] = useState<Error | undefined>(undefined);
  const [connected, setConnected] = useState(false);
  // Track whether the stream ever delivered a terminal event so we can stop
  // reconnecting once the run is finished.
  const terminalRef = useRef(false);

  useEffect(() => {
    const source = new EventSource(`/api/runs/${runId}/events`);

    source.addEventListener("run-event", (e: MessageEvent) => {
      try {
        const event = JSON.parse(e.data) as EvoRunEvent;
        if (event.kind === "run_finished") terminalRef.current = true;
        setEvents((prev) => [...prev, event]);
      } catch {
        // Malformed frame — skip; the reducer tolerates gaps via replay.
      }
    });

    source.addEventListener("snapshot", (e: MessageEvent) => {
      try {
        const view = JSON.parse(e.data) as NormalizedRunViewModel & {
          createdAt?: string;
        };
        // Dates don't survive JSON; revive createdAt for the UI.
        setSnapshot({
          ...view,
          createdAt: view.createdAt ? new Date(view.createdAt) : undefined,
        });
        if (view.isTerminal) terminalRef.current = true;
      } catch {
        // ignore malformed snapshot
      }
    });

    source.onerror = () => {
      // EventSource auto-reconnects on transient errors. If the run already
      // finished, close for good; otherwise surface a soft error state.
      if (terminalRef.current) {
        source.close();
        return;
      }
      setError(new Error("Live run connection interrupted — reconnecting…"));
    };

    source.onopen = () => {
      setConnected(true);
      setError(undefined);
    };

    return () => {
      source.close();
    };
  }, [runId]);

  const view = useMemo<NormalizedRunViewModel | undefined>(() => {
    if (events.length > 0) return reduceEvoEvents(runId, events);
    return snapshot;
  }, [runId, events, snapshot]);

  const value = useMemo<EvoRunEventsContextValue>(
    () => ({ view, error: connected ? undefined : error }),
    [view, error, connected],
  );

  return (
    <EvoRunEventsContext.Provider value={value}>
      {children}
    </EvoRunEventsContext.Provider>
  );
}

/** The normalized live view of the run this provider subscribes to. */
export function useEvoRunView(): NormalizedRunViewModel | undefined {
  const ctx = useContext(EvoRunEventsContext);
  if (!ctx) {
    throw new Error(
      "useEvoRunView must be used within an EvoRunEventsProvider",
    );
  }
  return ctx.view;
}

/** Transport error for the run's event stream, if any. */
export function useEvoRunEventsError(): Error | undefined {
  const ctx = useContext(EvoRunEventsContext);
  if (!ctx) {
    throw new Error(
      "useEvoRunEventsError must be used within an EvoRunEventsProvider",
    );
  }
  return ctx.error;
}
