// Phase 2 — TypeScript envelope codec for the distributed worker (Milestone 23).
//
// Encodes/decodes TaskEnvelope/ResultEnvelope using protobufjs against the
// shared proto (engine/proto/evo/execution.proto). This is the TS side of the
// M22 cross-language wire contract; the C++ scheduler encodes the same bytes.
//
// Identity semantics (M22): logical task id = (runId, nodeId); attempt id =
// (runId, nodeId, attemptNumber). Transport message ids are NOT identity.

import path from "node:path";
import { fileURLToPath } from "node:url";
import protobuf, { Root, Type } from "protobufjs";

// Resolve the repo root from this file (worker/src/envelope-codec.ts).
const HERE = path.dirname(fileURLToPath(import.meta.url));
const REPO_ROOT = path.resolve(HERE, "..", "..");
const PROTO_PATH = path.join(REPO_ROOT, "engine/proto/evo/execution.proto");

// Well-known google.protobuf.Timestamp. Try Homebrew prefixes; fall back to a
// bundled minimal definition if not found (keeps the worker self-contained).
const WELL_KNOWN_CANDIDATES = [
  "/opt/homebrew/include/google/protobuf/timestamp.proto",
  "/usr/local/include/google/protobuf/timestamp.proto",
];

let cachedRoot: Root | null = null;

async function loadRoot(): Promise<Root> {
  if (cachedRoot) return cachedRoot;
  const root = await protobuf.load(PROTO_PATH);
  let loadedWellKnown = false;
  for (const candidate of WELL_KNOWN_CANDIDATES) {
    try {
      const wk = await protobuf.load(candidate);
      root.add(wk);
      loadedWellKnown = true;
      break;
    } catch {
      // try next candidate
    }
  }
  if (!loadedWellKnown) {
    // Minimal inline Timestamp so the codec works without Homebrew headers.
    const parsed = protobuf.parse(
      'syntax = "proto3"; package google.protobuf; message Timestamp { int64 seconds = 1; int32 nanos = 2; }',
      { keepCase: false },
    );
    root.add(parsed.root);
  }
  cachedRoot = root;
  return root;
}

export interface TaskEnvelopeView {
  runId: string;
  workflowVersionId: string;
  orgId: string;
  nodeId: string;
  attemptNumber: number;
  resourceClass: number;
  affinityKey: string;
  traceId: string;
  spanId: string;
  nodeType: string;
  nodePayloadJson: string;
}

export interface ResultEnvelopeInput {
  runId: string;
  nodeId: string;
  attemptNumber: number;
  traceId?: string;
  completed: boolean;
  output?: string;
  error?: string;
  status?: number;
  abandoned?: boolean;
  errorClass?: number;
  retryable?: boolean;
  workerId?: string;
  startedAtWallMs?: number;
  finishedAtWallMs?: number;
}

// ResultEnvelope.StatusCode numeric values (execution.proto).
export const ResultStatus = {
  STATUS_UNSPECIFIED: 0,
  OK: 1,
  NODE_FAILED: 2,
  CANCELED: 3,
  TIMEOUT: 4,
  DEADLINE_EXCEEDED: 5,
} as const;

// ErrorClass numeric values (execution.proto, M22).
export const ErrorClass = {
  ERROR_CLASS_UNSPECIFIED: 0,
  ERROR_TRANSIENT: 1,
  ERROR_PERMANENT: 2,
  ERROR_RESOURCE_EXHAUSTED: 3,
  ERROR_CANCELED: 4,
} as const;

// ControlEnvelope.Kind numeric values (execution.proto, M30).
export const ControlKind = {
  CONTROL_KIND_UNSPECIFIED: 0,
  CANCEL_RUN: 1,
} as const;

export interface ControlEnvelopeView {
  kind: number;
  runId: string;
  reason: string;
  requestedAtWallMs?: number;
}

function wallToTimestamp(ms: number): { seconds: number; nanos: number } {
  const seconds = Math.floor(ms / 1000);
  const nanos = (ms % 1000) * 1_000_000;
  return { seconds, nanos };
}

export async function decodeTaskEnvelope(
  bytes: Buffer,
): Promise<TaskEnvelopeView> {
  const root = await loadRoot();
  const TaskEnvelope = root.lookupType("evo.execution.v1.TaskEnvelope") as Type;
  const msg = TaskEnvelope.decode(bytes) as unknown as TaskEnvelopeView;
  return {
    runId: msg.runId ?? "",
    workflowVersionId: msg.workflowVersionId ?? "",
    orgId: msg.orgId ?? "",
    nodeId: msg.nodeId ?? "",
    attemptNumber: Number(msg.attemptNumber ?? 0),
    resourceClass: Number(msg.resourceClass ?? 0),
    affinityKey: msg.affinityKey ?? "",
    traceId: msg.traceId ?? "",
    spanId: msg.spanId ?? "",
    nodeType: msg.nodeType ?? "",
    nodePayloadJson: msg.nodePayloadJson ?? "",
  };
}

/**
 * Decode a ControlEnvelope (M30). Throws on malformed bytes — the caller
 * quarantines + acks, same policy as task envelopes.
 */
export async function decodeControlEnvelope(
  bytes: Buffer,
): Promise<ControlEnvelopeView> {
  const root = await loadRoot();
  const ControlEnvelope = root.lookupType(
    "evo.execution.v1.ControlEnvelope",
  ) as Type;
  const msg = ControlEnvelope.decode(bytes) as unknown as {
    kind?: number;
    runId?: string;
    reason?: string;
    requestedAt?: { seconds?: number; nanos?: number };
  };
  const requestedAt = msg.requestedAt;
  const requestedAtWallMs =
    requestedAt && (requestedAt.seconds || requestedAt.nanos)
      ? Number(requestedAt.seconds ?? 0) * 1000 +
        Math.floor(Number(requestedAt.nanos ?? 0) / 1_000_000)
      : undefined;
  return {
    kind: Number(msg.kind ?? 0),
    runId: msg.runId ?? "",
    reason: msg.reason ?? "",
    requestedAtWallMs,
  };
}

export interface ControlEnvelopeInput {
  kind: number;
  runId: string;
  reason?: string;
  requestedAtWallMs?: number;
}

/** Encode a ControlEnvelope (used by tests + any scheduler-side publisher). */
export async function encodeControlEnvelope(
  input: ControlEnvelopeInput,
): Promise<Buffer> {
  const root = await loadRoot();
  const ControlEnvelope = root.lookupType(
    "evo.execution.v1.ControlEnvelope",
  ) as Type;
  const payload: Record<string, unknown> = {
    kind: input.kind,
    runId: input.runId,
  };
  if (input.reason) payload.reason = input.reason;
  if (input.requestedAtWallMs !== undefined) {
    payload.requestedAt = wallToTimestamp(input.requestedAtWallMs);
  }
  const err = ControlEnvelope.verify(payload);
  if (err) throw new Error(`ControlEnvelope invalid: ${err}`);
  const msg = ControlEnvelope.create(payload);
  return Buffer.from(ControlEnvelope.encode(msg).finish());
}

export interface ResultEnvelopeView {
  runId: string;
  nodeId: string;
  attemptNumber: number;
  traceId: string;
  completed: boolean;
  output: string;
  error: string;
  status: number;
  abandoned: boolean;
  errorClass: number;
  retryable?: boolean;
  workerId: string;
}

/** Decode a ResultEnvelope (tests + any result-stream consumer). */
export async function decodeResultEnvelope(
  bytes: Buffer,
): Promise<ResultEnvelopeView> {
  const root = await loadRoot();
  const ResultEnvelope = root.lookupType(
    "evo.execution.v1.ResultEnvelope",
  ) as Type;
  const msg = ResultEnvelope.decode(bytes) as unknown as Partial<
    ResultEnvelopeView
  > & { retryable?: boolean };
  return {
    runId: msg.runId ?? "",
    nodeId: msg.nodeId ?? "",
    attemptNumber: Number(msg.attemptNumber ?? 0),
    traceId: msg.traceId ?? "",
    completed: Boolean(msg.completed),
    output: msg.output ?? "",
    error: msg.error ?? "",
    status: Number(msg.status ?? 0),
    abandoned: Boolean(msg.abandoned),
    errorClass: Number(msg.errorClass ?? 0),
    retryable: msg.retryable,
    workerId: msg.workerId ?? "",
  };
}

export interface TaskEnvelopeInput {
  runId: string;
  workflowVersionId?: string;
  orgId: string;
  nodeId: string;
  attemptNumber: number;
  resourceClass?: number;
  affinityKey?: string;
  traceId?: string;
  spanId?: string;
  nodeType: string;
  nodePayloadJson?: string;
  becameReadyAtWallMs?: number;
}

/** Encode a TaskEnvelope (used by tests/scheduler-side publishers). */
export async function encodeTaskEnvelope(
  input: TaskEnvelopeInput,
): Promise<Buffer> {
  const root = await loadRoot();
  const TaskEnvelope = root.lookupType("evo.execution.v1.TaskEnvelope") as Type;
  const payload: Record<string, unknown> = {
    runId: input.runId,
    orgId: input.orgId,
    nodeId: input.nodeId,
    attemptNumber: input.attemptNumber,
    nodeType: input.nodeType,
  };
  if (input.workflowVersionId) payload.workflowVersionId = input.workflowVersionId;
  if (input.resourceClass !== undefined) payload.resourceClass = input.resourceClass;
  if (input.affinityKey) payload.affinityKey = input.affinityKey;
  if (input.traceId) payload.traceId = input.traceId;
  if (input.spanId) payload.spanId = input.spanId;
  if (input.nodePayloadJson !== undefined) payload.nodePayloadJson = input.nodePayloadJson;
  if (input.becameReadyAtWallMs !== undefined) {
    payload.becameReadyAt = wallToTimestamp(input.becameReadyAtWallMs);
  }
  const err = TaskEnvelope.verify(payload);
  if (err) throw new Error(`TaskEnvelope invalid: ${err}`);
  const msg = TaskEnvelope.create(payload);
  return Buffer.from(TaskEnvelope.encode(msg).finish());
}

export async function encodeResultEnvelope(
  input: ResultEnvelopeInput,
): Promise<Buffer> {
  const root = await loadRoot();
  const ResultEnvelope = root.lookupType(
    "evo.execution.v1.ResultEnvelope",
  ) as Type;
  const payload: Record<string, unknown> = {
    runId: input.runId,
    nodeId: input.nodeId,
    attemptNumber: input.attemptNumber,
    completed: input.completed,
  };
  if (input.traceId) payload.traceId = input.traceId;
  if (input.output !== undefined) payload.output = input.output;
  if (input.error !== undefined) payload.error = input.error;
  if (input.status !== undefined) payload.status = input.status;
  if (input.abandoned !== undefined) payload.abandoned = input.abandoned;
  if (input.errorClass !== undefined) payload.errorClass = input.errorClass;
  if (input.retryable !== undefined) payload.retryable = input.retryable;
  if (input.workerId !== undefined) payload.workerId = input.workerId;
  if (input.startedAtWallMs !== undefined) {
    payload.startedAt = wallToTimestamp(input.startedAtWallMs);
  }
  if (input.finishedAtWallMs !== undefined) {
    payload.finishedAt = wallToTimestamp(input.finishedAtWallMs);
  }
  const err = ResultEnvelope.verify(payload);
  if (err) throw new Error(`ResultEnvelope invalid: ${err}`);
  const msg = ResultEnvelope.create(payload);
  return Buffer.from(ResultEnvelope.encode(msg).finish());
}
