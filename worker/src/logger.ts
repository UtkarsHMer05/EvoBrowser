// Phase 2 — structured JSON logging for the TypeScript worker (Milestone 38).
//
// Emits one JSON object per line to stdout so worker logs are machine-parseable
// and correlate with the C++ scheduler's structured logs (engine/core log.hpp).
// Every record carries a wall-clock UTC millisecond timestamp, a level, the
// service name, and an `event`/`msg` field.
//
// The worker's existing log call sites pass plain strings of the form
// `[<workerId>] <text> key=value key=value ...`. This logger:
//   1. extracts the bracketed prefix as `worker_id`,
//   2. promotes `key=value` tokens to top-level JSON fields (correlation:
//      run_id, node_id, attempt, org_id, trace_id, ...),
//   3. REDACTS any field whose key looks secret-like (password/token/api_key/
//      credential/authorization/private), and redacts Bearer tokens and
//      embedded credentials in URLs, as defense in depth.
//
// The result is structured, secret-free JSON without rewriting every call
// site. `logEvent` is available for new call sites that want explicit fields.

/** Log levels mirror the C++ engine logger (evo/log.hpp). */
export type LogLevel = "debug" | "info" | "warn" | "error";

const SECRET_KEY_PATTERNS = [
  "password",
  "secret",
  "token",
  "credential",
  "authorization",
  "api_key",
  "apikey",
  "private_key",
];

/** True when `key` names a secret-like field whose value must be redacted. */
export function isSecretKey(key: string): boolean {
  const k = key.toLowerCase();
  return SECRET_KEY_PATTERNS.some((p) => k.includes(p));
}

export const REDACTED = "[REDACTED]";

/**
 * Redact secret-looking content inside a free-text message:
 *   - `key=value` pairs whose key is secret-like,
 *   - `Bearer <token>` authorization headers,
 *   - credentials embedded in URLs (scheme://user:pass@host).
 * Identifiers (run_id/org_id/...) pass through untouched.
 */
export function redactSecrets(text: string): string {
  let out = text;
  // key=value (value = until whitespace or end); redact when key is secret-like.
  out = out.replace(/([A-Za-z0-9_.-]+)=("[^"]*"|\S+)/g, (m, key, value) =>
    isSecretKey(String(key)) ? `${key}=${REDACTED}` : `${key}=${value}`,
  );
  // Bearer tokens.
  out = out.replace(/Bearer\s+[A-Za-z0-9._~+/=-]+/gi, `Bearer ${REDACTED}`);
  // URL-embedded credentials: postgres://user:pass@host -> postgres://user:[REDACTED]@host
  out = out.replace(/([a-z][a-z0-9+.-]*:\/\/[^:/\s@]+:)([^@\s]+)(@)/gi, `$1${REDACTED}$3`);
  return out;
}

export interface StructuredLoggerOptions {
  /** Service name stamped on every line (e.g. "evo-worker"). */
  service: string;
  /** Where to write; defaults to process.stdout.write. Tests may override. */
  write?: (line: string) => void;
}

export interface StructuredLogger {
  /** Drop-in `(msg: string) => void` for WorkerConfig.log and friends. */
  log: (msg: string) => void;
  /** Emit an explicit structured event with named fields (values redacted). */
  logEvent: (
    level: LogLevel,
    event: string,
    fields?: Record<string, string | number | boolean>,
  ) => void;
}

/**
 * Parse a plain worker log line into structured fields.
 * `[<prefix>] text key=value ...` -> { worker_id?, msg, ...fields }.
 */
export function parseWorkerLine(
  msg: string,
): { workerId?: string; msg: string; fields: Record<string, string> } {
  let rest = msg;
  let workerId: string | undefined;
  const bracket = rest.match(/^\[([^\]]+)\]\s*/);
  if (bracket) {
    // "[worker]" is the generic main.ts prefix, not an identity.
    if (bracket[1] !== "worker") workerId = bracket[1];
    rest = rest.slice(bracket[0].length);
  }
  const fields: Record<string, string> = {};
  // Promote key=value tokens; leave everything else as the message text.
  const parts: string[] = [];
  for (const tok of rest.split(/\s+/)) {
    const eq = tok.indexOf("=");
    if (eq > 0 && /^[A-Za-z0-9_.-]+$/.test(tok.slice(0, eq))) {
      let value = tok.slice(eq + 1);
      if (value.startsWith('"') && value.endsWith('"') && value.length >= 2) {
        value = value.slice(1, -1);
      }
      fields[tok.slice(0, eq)] = value;
    } else if (tok.length > 0) {
      parts.push(tok);
    }
  }
  return { workerId, msg: parts.join(" "), fields };
}

/** Create a structured JSON logger for the worker process. */
export function createWorkerLogger(
  opts: StructuredLoggerOptions,
): StructuredLogger {
  const write = opts.write ?? ((line: string) => process.stdout.write(line));

  const emit = (
    level: LogLevel,
    fields: Record<string, string | number | boolean>,
  ): void => {
    const record: Record<string, unknown> = {
      ts_ms: Date.now(),
      level,
      service: opts.service,
    };
    for (const [k, v] of Object.entries(fields)) {
      record[k] = typeof v === "string" && isSecretKey(k) ? REDACTED : v;
    }
    write(JSON.stringify(record) + "\n");
  };

  return {
    log: (msg: string) => {
      const { workerId, msg: text, fields } = parseWorkerLine(msg);
      const merged: Record<string, string | number | boolean> = { ...fields };
      if (workerId) merged.worker_id = workerId;
      // Redact secrets inside the free text too (defense in depth).
      merged.msg = redactSecrets(text);
      emit("info", merged);
    },
    logEvent: (level, event, fields = {}) => {
      emit(level, { event, ...fields });
    },
  };
}
