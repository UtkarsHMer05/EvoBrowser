// Phase 2 — gRPC client for the C++ Evo scheduler service (Milestone 27).
//
// Loads the shared evo.execution.v1 contract (engine/proto/evo/execution.proto)
// with @grpc/proto-loader and wraps the ControlService RPCs in promises. This
// is the only module that imports @grpc/grpc-js; it is loaded lazily by the
// Evo adapter so the legacy path never pulls in the gRPC stack.
//
// Server-side only. The scheduler listens on a loopback address by default
// (EVO_SCHEDULER_ADDR, 127.0.0.1:50051) with insecure credentials — the M17
// no-go forbids unauthenticated non-local exposure, so this client uses an
// insecure channel to match.

import path from "node:path";
import { fileURLToPath } from "node:url";

import * as grpc from "@grpc/grpc-js";
import * as protoLoader from "@grpc/proto-loader";

import type { EvoSchedulerClient } from "./evo-engine-adapter";

const HERE = path.dirname(fileURLToPath(import.meta.url));
const REPO_ROOT = path.resolve(HERE, "..", "..", "..");
const PROTO_PATH = path.join(REPO_ROOT, "engine/proto/evo/execution.proto");

// Resolve google.protobuf.Timestamp imports. proto-loader searches include
// dirs; Homebrew's protobuf headers carry the well-known types.
const INCLUDE_DIRS = [
  "/opt/homebrew/include",
  "/usr/local/include",
  path.join(REPO_ROOT, "engine/proto"),
];

interface ControlServiceClient {
  SubmitRun(
    req: Record<string, unknown>,
    cb: grpc.requestCallback<Record<string, unknown>>,
  ): grpc.ClientUnaryCall;
  CancelRun(
    req: Record<string, unknown>,
    cb: grpc.requestCallback<Record<string, unknown>>,
  ): grpc.ClientUnaryCall;
  GetRun(
    req: Record<string, unknown>,
    cb: grpc.requestCallback<Record<string, unknown>>,
  ): grpc.ClientUnaryCall;
  Health(
    req: Record<string, unknown>,
    cb: grpc.requestCallback<Record<string, unknown>>,
  ): grpc.ClientUnaryCall;
}

let cachedClient: ControlServiceClient | null = null;

function loadServiceClient(addr: string): ControlServiceClient {
  const packageDefinition = protoLoader.loadSync(PROTO_PATH, {
    keepCase: false,
    longs: String,
    enums: String,
    defaults: true,
    oneofs: true,
    includeDirs: INCLUDE_DIRS,
  });
  const loaded = grpc.loadPackageDefinition(packageDefinition) as unknown as {
    evo: {
      execution: {
        v1: { ControlService: grpc.ServiceClientConstructor };
      };
    };
  };
  const ServiceCtor = loaded.evo.execution.v1.ControlService;
  const client = new ServiceCtor(
    addr,
    grpc.credentials.createInsecure(),
  ) as unknown as ControlServiceClient;
  return client;
}

function unary<T>(
  call: (
    req: Record<string, unknown>,
    cb: grpc.requestCallback<Record<string, unknown>>,
  ) => grpc.ClientUnaryCall,
  req: Record<string, unknown>,
): Promise<T> {
  return new Promise((resolve, reject) => {
    call(req, (err, resp) => {
      if (err) {
        reject(err);
        return;
      }
      resolve(resp as T);
    });
  });
}

/**
 * Create a gRPC-backed EvoSchedulerClient for the scheduler at `addr`.
 * The channel is created lazily on first RPC and cached per address.
 */
export function createGrpcEvoSchedulerClient(addr: string): EvoSchedulerClient {
  const getClient = (): ControlServiceClient => {
    if (!cachedClient) {
      cachedClient = loadServiceClient(addr);
    }
    return cachedClient;
  };

  return {
    async submitRun(args) {
      const resp = await unary<Record<string, unknown>>(
        (req, cb) => getClient().SubmitRun(req, cb),
        {
          orgId: args.orgId,
          workflowVersionId: args.workflowVersionId,
          runId: args.runId,
          dagJson: args.dagJson,
          traceId: args.traceId,
        },
      );
      return {
        runId: String(resp.runId ?? args.runId),
        accepted: Boolean(resp.accepted),
        message: String(resp.message ?? ""),
      };
    },

    async cancelRun(args) {
      const resp = await unary<Record<string, unknown>>(
        (req, cb) => getClient().CancelRun(req, cb),
        { runId: args.runId, reason: args.reason, traceId: args.traceId },
      );
      return { ok: Boolean(resp.ok) };
    },

    async getRun(runId) {
      const resp = await unary<Record<string, unknown>>(
        (req, cb) => getClient().GetRun(req, cb),
        { runId },
      );
      return {
        runId: String(resp.runId ?? runId),
        status: String(resp.status ?? "RUN_STATUS_UNSPECIFIED"),
        outcome: String(resp.outcome ?? "OUTCOME_UNSPECIFIED"),
      };
    },

    async health() {
      const resp = await unary<Record<string, unknown>>(
        (req, cb) => getClient().Health(req, cb),
        {},
      );
      return { ok: Boolean(resp.ok), detail: String(resp.detail ?? "") };
    },
  };
}

/** Close the cached channel (tests / graceful shutdown). */
export function closeGrpcEvoSchedulerClient(): void {
  if (cachedClient) {
    (cachedClient as unknown as grpc.Client).close();
    cachedClient = null;
  }
}
