import Browserbase from "@browserbasehq/sdk";

// Server-only Browserbase client for observability calls (session replays, logs).
// It carries the secret API key, so it must never be imported into client code.
let browserbaseClient: Browserbase | null = null;

export function getBrowserbaseClient() {
  const apiKey = process.env.BROWSERBASE_API_KEY;

  if (!apiKey) {
    throw new Error("BROWSERBASE_API_KEY is missing or empty");
  }

  browserbaseClient ??= new Browserbase({
    apiKey,
  });

  return browserbaseClient;
}
