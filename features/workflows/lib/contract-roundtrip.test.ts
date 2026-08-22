// Milestone 16: TypeScript golden round-trip test for the protobuf execution
// contract (engine/proto/evo/execution.proto).
//
// Uses the already-installed `protobufjs` library (7.6.5) to load the schema,
// encode/decode messages, and assert field-value preservation. This is the TS
// side of "golden serialization test" validation for M16; typed bindings remain
// via protobufjs's runtime typing (pbts codegen is deferred to M17 tooling work).
//
// Run as part of: npm test  (this file is picked up by tsx).

import assert from "node:assert/strict";
import { fileURLToPath } from "node:url";
import protobuf, { Service, Type } from "protobufjs";

console.log("TEST: TS protobuf contract round-trip");

const PROTO_PATH = fileURLToPath(
  new URL("../../../engine/proto/evo/execution.proto", import.meta.url),
);

const root = await protobuf.load(PROTO_PATH);

const SubmitRunRequest = root.lookupType(
  "evo.execution.v1.SubmitRunRequest",
) as Type;
const TaskEnvelope = root.lookupType("evo.execution.v1.TaskEnvelope") as Type;
const ResultEnvelope = root.lookupType(
  "evo.execution.v1.ResultEnvelope",
) as Type;
const ControlServiceRaw = root.lookup(
  "evo.execution.v1.ControlService",
) as Service;
const ControlService = ControlServiceRaw as Service;

interface TimestampLike {
  seconds: { toNumber(): number } | number;
  nanos: number;
}

interface SubmitRunView {
  orgId: string;
  workflowVersionId: string;
  runId: string;
  dagJson: string;
  traceId: string;
  requestedAt: TimestampLike;
}

interface TaskEnvelopeView {
  runId: string;
  nodeId: string;
  attemptNumber: number;
  resourceClass: string;
  affinityKey: string;
  traceId: string;
  nodeType: string;
  nodePayloadJson: string;
  becameReadyAt: TimestampLike;
}

interface ResultEnvelopeView {
  runId: string;
  nodeId: string;
  attemptNumber: number;
  completed: boolean;
  output: string;
  status: string;
  abandoned: boolean;
}

// --- SubmitRunRequest round-trip ---
{
  const orgId = "org_abc";
  const runId = "run_42";
  const dagJson = JSON.stringify({ nodes: [], edges: [] });
  const traceId = "trace-1";
  const expectedSeconds = 1704164645;

  const msg = SubmitRunRequest.fromObject({
    orgId,
    workflowVersionId: "wf_1",
    runId,
    dagJson,
    traceId,
    requestedAt: { seconds: expectedSeconds, nanos: 123000000 },
  });
  const wire = SubmitRunRequest.encode(msg).finish();
  const decoded = SubmitRunRequest.decode(wire);
  const back = SubmitRunRequest.toObject(decoded, {
    defaults: true,
    enums: String,
  }) as unknown as SubmitRunView;

  assert.equal(back.orgId, orgId, "SubmitRun org_id round-trips");
  assert.equal(back.runId, runId, "SubmitRun run_id round-trips");
  assert.equal(back.dagJson, dagJson, "SubmitRun dag_json round-trips");
  assert.equal(back.traceId, traceId, "SubmitRun trace_id round-trips");
  assert.equal(
    typeof back.requestedAt.seconds === "number"
      ? back.requestedAt.seconds
      : back.requestedAt.seconds.toNumber(),
    expectedSeconds,
    "SubmitRun requested_at (wall-clock) round-trips",
  );
  console.log(
    "  ok   SubmitRunRequest round-trip preserved (incl. wall-clock timestamp)",
  );
}

// --- TaskEnvelope round-trip (browser resource class + affinity key) ---
{
  const msg = TaskEnvelope.fromObject({
    runId: "run_X",
    nodeId: "node_1",
    attemptNumber: 1,
    resourceClass: "BROWSER",
    affinityKey: "run:run_X",
    traceId: "trace-Y",
    nodeType: "act",
    nodePayloadJson: JSON.stringify({ selector: "#btn" }),
    becameReadyAt: { seconds: 1234567, nanos: 890 },
  });
  const wire = TaskEnvelope.encode(msg).finish();
  const decoded = TaskEnvelope.decode(wire);
  const back = TaskEnvelope.toObject(decoded, {
    defaults: true,
    enums: String,
  }) as unknown as TaskEnvelopeView;

  assert.equal(
    back.resourceClass,
    "BROWSER",
    "TaskEnvelope resource_class BROWSER",
  );
  assert.equal(back.affinityKey, "run:run_X", "TaskEnvelope affinity_key");
  assert.equal(back.attemptNumber, 1, "TaskEnvelope attempt_number");
  assert.equal(
    back.nodePayloadJson,
    JSON.stringify({ selector: "#btn" }),
    "TaskEnvelope payload round-trips (no secrets embedded by contract)",
  );
  assert.equal(
    typeof back.becameReadyAt.seconds === "number"
      ? back.becameReadyAt.seconds
      : back.becameReadyAt.seconds.toNumber(),
    1234567,
    "TaskEnvelope became_ready_at (wall-clock) round-trips",
  );
  console.log(
    "  ok   TaskEnvelope round-trip preserved (resource class + affinity)",
  );
}

// --- ResultEnvelope round-trip ---
{
  const msg = ResultEnvelope.fromObject({
    runId: "run_X",
    nodeId: "node_1",
    attemptNumber: 1,
    traceId: "trace-Y",
    completed: true,
    output: "clicked",
    status: "OK",
  });
  const wire = ResultEnvelope.encode(msg).finish();
  const decoded = ResultEnvelope.decode(wire);
  const back = ResultEnvelope.toObject(decoded, {
    defaults: true,
    enums: String,
  }) as unknown as ResultEnvelopeView;

  assert.equal(back.completed, true, "ResultEnvelope completed");
  assert.equal(back.status, "OK", "ResultEnvelope status OK");
  assert.equal(
    back.abandoned,
    false,
    "ResultEnvelope abandoned defaults false",
  );
  console.log("  ok   ResultEnvelope round-trip preserved");
}

// --- Service surface (ControlService RPCs) ---
{
  const methods = ControlService.methods as Record<string, unknown> | undefined;
  const methodNames = methods ? Object.keys(methods) : [];
  assert.ok(methodNames.includes("SubmitRun"), "ControlService has SubmitRun");
  assert.ok(methodNames.includes("CancelRun"), "ControlService has CancelRun");
  assert.ok(methodNames.includes("GetRun"), "ControlService has GetRun");
  assert.ok(methodNames.includes("Health"), "ControlService has Health");
  console.log(
    "  ok   ControlService exposes 4 RPCs (Submit/Cancel/Get/Health)",
  );
}

console.log("\nALL PROTOBUF CONTRACT ROUND-TRIP TESTS PASSED! (4/4)");

if (process.env.VITEST) {
  // Registration only: every assertion in this linear script already ran at
  // module load (collection). A failure above marks this file red; this entry
  // gives vitest a named test to track on success.
  const { test } = await import("vitest");
  test("M21 Protobuf contract round-trip", () => {});
}
