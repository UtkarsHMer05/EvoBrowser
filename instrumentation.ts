import * as Sentry from "@sentry/nextjs";

export async function register() {
  if (process.env.NEXT_RUNTIME === "nodejs") {
    await import("./sentry.server.config");
    warmLiveblocksAuthRoutes();
  }

  if (process.env.NEXT_RUNTIME === "edge") {
    await import("./sentry.edge.config");
  }
}

// Dev-only warmup: Next.js compiles routes on demand, and a cold compile of
// the Liveblocks auth endpoint can take tens of seconds on a loaded machine.
// The Liveblocks client has a hard-coded 10s auth timeout (AUTH_TIMEOUT in
// @liveblocks/core), so any cold compile makes room joins fail with "Timed out
// during auth". A one-time boot warmup isn't enough because file changes
// re-trigger on-demand compilation and leave the route cold again. So ping the
// auth routes shortly after boot and then periodically, keeping them compiled
// so the next room join always hits a warm route. Never runs in production;
// failures are swallowed.
function warmLiveblocksAuthRoutes() {
  if (process.env.NODE_ENV !== "development") {
    return;
  }

  const port = process.env.PORT ?? "3000";
  const base = `http://localhost:${port}`;
  const paths = ["/api/liveblocks/auth", "/api/liveblocks/users"];

  const warm = async (): Promise<boolean> => {
    let anyOk = false;
    for (const path of paths) {
      try {
        await fetch(`${base}${path}`, { method: "POST" });
        anyOk = true;
      } catch {
        // Server not listening yet or transient — the next tick retries.
      }
    }
    return anyOk;
  };

  // register() runs before the listener is bound, so retry quickly until the
  // first successful warmup (which also triggers the route compile). Once the
  // routes are warm, keep them warm on a slower interval — file changes can
  // re-trigger on-demand compilation and leave a route cold again.
  const boot = async () => {
    let warmed = await warm();
    while (!warmed) {
      await new Promise((r) => setTimeout(r, 2000));
      warmed = await warm();
    }
    const timer = setInterval(() => void warm(), 30_000);
    // Don't let the timer keep the process alive or block shutdown.
    timer.unref?.();
  };

  setTimeout(() => void boot(), 1500);
}

// Automatically captures all unhandled server-side request errors
export const onRequestError = Sentry.captureRequestError;
