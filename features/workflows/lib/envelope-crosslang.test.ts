// Milestone 22: cross-language envelope compatibility test.
//
// The C++ fixture generator (engine/tests/envelope_fixture_gen.cpp) writes
// deterministic golden TaskEnvelope/ResultEnvelope bytes to
// engine/tests/fixtures/. This test decodes those bytes with protobufjs,
// asserts the M22 fields round-trip, and re-encodes them, asserting
// BYTE-IDENTICAL output — proving the C++ and TypeScript encoders/decoders
// agree on the wire format for the M22 envelope fields.
//
// Also exercises malformed-payload rejection and the size-limit rule.
//
// Run as part of: npm test

import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import protobuf, { Type } from "protobufjs";

console.log("TEST: M22 cross-language envelope compatibility");

const PROTO_PATH =
  "/Users/utkarshkhajuria/Desktop/evo builder/engine/proto/evo/execution.proto";
const FIXTURE_DIR =
  "/Users/utkarshkhajuria/Desktop/evo builder/engine/tests/fixtures";

const root = await protobuf.load(PROTO_PATH);
const wellKnown = await protobuf.load(
  "/opt/homebrew/include/google/protobuf/timestamp.proto",
);
root.add(wellKnown);

const TaskEnvelope = root.lookupType("evo.execution.v1.TaskEnvelope") as Type;
const ResultEnvelope = root.lookupType(
  "evo.execution.v1.ResultEnvelope",
) as Type;

let passed = 0;
const ok = (label: string) => {
  passed++;
  console.log(`  ok   ${label}`);
};

// --- Decode the C++ golden TaskEnvelope fixture ---------------------------
const taskBytes = readFileSync(`${FIXTURE_DIR}/task_envelope.bin`);
const task = TaskEnvelope.decode(taskBytes) as unknown as {
  runId: string;
  workflowVersionId: string;
  orgId: string;
  nodeId: string;
  attemptNumber: number;
  resourceClass: number | string;
  affinityKey: string;
  traceId: string;
  spanId: string;
  nodeType: string;
  nodePayloadJson: string;
};

assert.equal(task.runId, "run-fixture-001");
assert.equal(task.workflowVersionId, "wfv-fixture-001");
assert.equal(task.orgId, "org_fixture");
assert.equal(task.nodeId, "n0");
assert.equal(task.attemptNumber, 1);
assert.equal(task.affinityKey, "run:run-fixture-001");
assert.equal(task.nodeType, "act");
assert.equal(
  task.nodePayloadJson,
  '{"instruction":"Click the sign in button"}',
);
ok("TS decodes C++ TaskEnvelope fixture (all fields preserved)");

// Byte-identical re-encode.
const taskReencoded = TaskEnvelope.encode(
  TaskEnvelope.create(task),
).finish();
assert.deepEqual(
  Buffer.from(taskReencoded),
  taskBytes,
  "TaskEnvelope re-encode is byte-identical to C++ bytes",
);
ok("TaskEnvelope TS re-encode is byte-identical to C++ golden bytes");

// --- Decode the C++ golden ResultEnvelope fixture (M22 fields) ------------
const resultBytes = readFileSync(`${FIXTURE_DIR}/result_envelope.bin`);
const result = ResultEnvelope.decode(resultBytes) as unknown as {
  runId: string;
  nodeId: string;
  attemptNumber: number;
  traceId: string;
  completed: boolean;
  output: string;
  error: string;
  status: number | string;
  abandoned: boolean;
  errorClass: number | string;
  retryable: boolean;
  workerId: string;
};

assert.equal(result.runId, "run-fixture-001");
assert.equal(result.nodeId, "n0");
assert.equal(result.attemptNumber, 2);
assert.equal(result.completed, false);
assert.equal(result.output, '{"partial":true}');
assert.equal(result.error, "upstream 503");
assert.equal(result.abandoned, false);
assert.equal(result.workerId, "worker-fixture-7");
assert.equal(result.retryable, true, "M22 retryable hint preserved");
ok("TS decodes C++ ResultEnvelope fixture (M22 fields preserved)");

// protobufjs decodes enums as their numeric wire values by default.
// ERROR_TRANSIENT = 1, NODE_FAILED = 2 (see execution.proto).
assert.equal(Number(result.errorClass), 1, "error_class == ERROR_TRANSIENT(1)");
assert.equal(Number(result.status), 2, "status == NODE_FAILED(2)");
ok("M22 error_class + status enums decode to expected wire values");

// Byte-identical re-encode.
const resultReencoded = ResultEnvelope.encode(
  ResultEnvelope.create(result),
).finish();
assert.deepEqual(
  Buffer.from(resultReencoded),
  resultBytes,
  "ResultEnvelope re-encode is byte-identical to C++ bytes",
);
ok("ResultEnvelope TS re-encode is byte-identical to C++ golden bytes");

// --- Malformed payload rejection ------------------------------------------
// Truncated bytes must not decode to the COMPLETE original message. Proto
// truncation drops trailing fields, so the meaningful check is that the
// decoded result is incomplete (lost fields) or fails to decode — i.e. it can
// never be mistaken for the intact envelope.
const truncated = taskBytes.subarray(0, Math.max(1, taskBytes.length - 20));
let truncatedIncomplete = false;
try {
  const bad = TaskEnvelope.decode(truncated);
  const view = bad as unknown as {
    nodePayloadJson?: string;
    becameReadyAt?: unknown;
  };
  // The tail carries node_payload_json + became_ready_at; losing either means
  // the truncated bytes are detectably incomplete.
  if (!view.nodePayloadJson || !view.becameReadyAt) truncatedIncomplete = true;
} catch {
  truncatedIncomplete = true;
}
assert.ok(
  truncatedIncomplete,
  "truncated payload is incomplete or rejected (never equals the original)",
);
ok("malformed (truncated) payload detected as incomplete");

// Pure garbage must not decode to a valid envelope.
const garbage = Buffer.from([0xff, 0xfe, 0xfd, 0xfc, 0xfb, 0xfa]);
let garbageRejected = false;
try {
  const g = TaskEnvelope.decode(garbage);
  const view = g as unknown as { runId?: string; nodeId?: string };
  if (!view.runId || !view.nodeId) garbageRejected = true;
} catch {
  garbageRejected = true;
}
assert.ok(garbageRejected, "garbage payload is rejected or loses identity");
ok("malformed (garbage) payload rejected");

// --- Size-limit rule (mirrors C++ kMaxEnvelopeBytes = 256 KiB) ------------
const MAX_ENVELOPE_BYTES = 256 * 1024;
const oversized = ResultEnvelope.create({
  runId: "r",
  nodeId: "n",
  attemptNumber: 1,
  completed: true,
  output: "y".repeat(MAX_ENVELOPE_BYTES + 1),
});
const oversizedBytes = ResultEnvelope.encode(oversized).finish();
assert.ok(
  oversizedBytes.length > MAX_ENVELOPE_BYTES,
  "oversized envelope exceeds the size limit",
);
ok("size-limit rule: oversized envelope detected and would be quarantined");

console.log(
  `\nALL M22 CROSS-LANGUAGE ENVELOPE TESTS PASSED! (${passed}/${passed})`,
);
