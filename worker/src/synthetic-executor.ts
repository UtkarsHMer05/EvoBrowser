// Phase 2 — synthetic task executor for the distributed worker (Milestone 23).
//
// A deterministic, dependency-free executor used for integration tests and
// benchmarks. It interprets the TaskEnvelope's node_payload_json as a small
// synthetic spec and simulates work WITHOUT touching any real external
// service or browser. It is explicitly NOT a production executor — real node
// executors are wired in M24 behind the same TaskExecutor interface.
//
// Supported synthetic node types (never added to the TS product node registry):
//   "bench:sleep"  -> sleep for `ms` (default 5), honoring the AbortSignal
//   "bench:burn"   -> spin CPU for ~`ms` (default 5), checking the signal
//   "bench:fail"   -> always fail with a configurable error class
//   "bench:echo"   -> succeed, echoing the payload as output JSON
//
// Payload shape: { "ms"?: number, "errorClass"?: number, "retryable"?: bool }

import type { TaskExecutor } from "./worker";
import { ErrorClass } from "./envelope-codec";
import type { TaskEnvelopeView } from "./envelope-codec";

interface SyntheticPayload {
  ms?: number;
  errorClass?: number;
  retryable?: boolean;
}

function parsePayload(task: TaskEnvelopeView): SyntheticPayload {
  if (!task.nodePayloadJson) return {};
  try {
    const parsed = JSON.parse(task.nodePayloadJson) as SyntheticPayload;
    return typeof parsed === "object" && parsed !== null ? parsed : {};
  } catch {
    return {};
  }
}

function sleep(ms: number, signal: AbortSignal): Promise<void> {
  return new Promise((resolve, reject) => {
    if (signal.aborted) {
      reject(new Error("aborted"));
      return;
    }
    const t = setTimeout(() => {
      signal.removeEventListener("abort", onAbort);
      resolve();
    }, ms);
    const onAbort = () => {
      clearTimeout(t);
      reject(new Error("aborted"));
    };
    signal.addEventListener("abort", onAbort, { once: true });
  });
}

export const syntheticExecutor: TaskExecutor = async (task, signal) => {
  const payload = parsePayload(task);
  const ms = Math.max(0, payload.ms ?? 5);

  switch (task.nodeType) {
    case "bench:sleep": {
      await sleep(ms, signal);
      return { completed: true, output: JSON.stringify({ sleptMs: ms }) };
    }
    case "bench:burn": {
      const end = Date.now() + ms;
      while (Date.now() < end) {
        if (signal.aborted) throw new Error("aborted");
        // Busy-work that yields to the event loop periodically so the worker
        // stays responsive (not a tight spin).
        Math.sqrt(Math.random() * 1e9);
        await sleep(0, signal);
      }
      return { completed: true, output: JSON.stringify({ burnedMs: ms }) };
    }
    case "bench:fail": {
      const errorClass = payload.errorClass ?? ErrorClass.ERROR_TRANSIENT;
      return {
        completed: false,
        error: `synthetic failure (class=${errorClass})`,
        errorClass,
        retryable: payload.retryable ?? errorClass === ErrorClass.ERROR_TRANSIENT,
      };
    }
    case "bench:echo": {
      return {
        completed: true,
        output: JSON.stringify({
          echo: true,
          runId: task.runId,
          nodeId: task.nodeId,
          attempt: task.attemptNumber,
        }),
      };
    }
    default: {
      // Unknown synthetic type: permanent failure (no retry).
      return {
        completed: false,
        error: `unknown synthetic node type: ${task.nodeType}`,
        errorClass: ErrorClass.ERROR_PERMANENT,
        retryable: false,
      };
    }
  }
};
