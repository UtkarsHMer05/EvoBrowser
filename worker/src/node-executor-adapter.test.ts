// Milestone 24: worker-executor output compatibility test.
//
// Proves the worker-side execution adapter (node-executor-adapter.ts) produces
// IDENTICAL output to the legacy Phase-1 execution path for mocked
// deterministic nodes. Both paths reuse the same interpolate() implementation
// and the same nodeExecutors registry; this test pins that they agree.
//
// Pure unit test — no Redis, no DB, no network. Always runs in `npm test`.
//
// The legacy path mirrors run-workflow.ts's per-node loop:
//   values = interpolate(node.data.values, outputs); output = executor({values})
// The worker path runs the same node through createNodeExecutorAdapter with
// in-memory loaders. We assert the outputs are byte-identical (as JSON).

import assert from "node:assert/strict";

import {
  interpolate,
  type NodeOutputs,
} from "@/features/workflows/lib/interpolate";
import type {
  NodeType,
  StepNodeType,
} from "@/features/workflows/nodes/node-registry";
import type { WorkflowGraph } from "@/lib/db/schema";

import { createEmailTestSink } from "./email-test-sink";
import {
  createNodeExecutorAdapter,
  type PredecessorOutputsLoader,
  type WorkflowVersionLoader,
} from "./node-executor-adapter";
import type { TaskEnvelopeView } from "./envelope-codec";

let passed = 0;
const ok = (label: string) => {
  passed++;
  console.log(`  ok   ${label}`);
};

function stepNode(
  id: string,
  type: NodeType,
  values: Record<string, string>,
): StepNodeType {
  return {
    id,
    type: "step",
    position: { x: 0, y: 0 },
    data: {
      type,
      kind: type === "start" ? "trigger" : "action",
      title: id,
      values,
    },
  };
}

// A deterministic mock "extract" executor: returns a fixed object derived from
// the instruction, so both paths produce the same bytes. Registered as an
// override so no real Stagehand/browser is touched.
const deterministicExtract = async ({
  values,
}: {
  values: Record<string, string>;
}) => ({ extraction: `extracted:${values.instruction}`, count: 3 });

async function main() {
  // Graph: start -> extract_1 -> send_email_1 (email body interpolates extract).
  const graph: WorkflowGraph = {
    nodes: [
      stepNode("start", "start", {}),
      stepNode("extract_1", "extract", { instruction: "top stories" }),
      stepNode("send_email_1", "send-email", {
        to: "ops@example.com",
        subject: "Digest",
        body: "Found {{ extract_1.count }} items: {{ extract_1.extraction }}",
      }),
    ],
    edges: [
      { id: "e1", source: "start", target: "extract_1" },
      { id: "e2", source: "extract_1", target: "send_email_1" },
    ],
  };

  const emailSink = createEmailTestSink();
  const overrides = {
    extract: deterministicExtract,
    "send-email": emailSink.executor,
  } as Partial<Record<NodeType, typeof deterministicExtract>>;

  // In-memory loaders for the worker adapter.
  const loadVersion: WorkflowVersionLoader = async () => ({
    workflowVersionId: "wfv-test",
    graph,
  });
  // Predecessor outputs: extract_1 has run and produced its output; send_email
  // sees it. start produces nothing.
  const extractOutput = await deterministicExtract({
    values: { instruction: "top stories" },
  });
  const outputsByNode: Record<string, NodeOutputs> = {
    extract_1: {}, // extract_1 has no predecessors
    send_email_1: { extract_1: extractOutput },
  };
  const loadPredecessorOutputs: PredecessorOutputsLoader = async ({
    nodeId,
  }) => outputsByNode[nodeId] ?? {};

  const adapter = createNodeExecutorAdapter({
    loadVersion,
    loadPredecessorOutputs,
    executorOverrides: overrides,
  });

  const abort = new AbortController().signal;

  // --- Node: extract_1 ------------------------------------------------------
  {
    // Legacy path (run-workflow.ts semantics).
    const node = graph.nodes.find((n) => n.id === "extract_1")!;
    const legacyValues = Object.fromEntries(
      Object.entries(node.data.values).map(([k, t]) => [
        k,
        interpolate({ text: t, outputs: outputsByNode.extract_1 }),
      ]),
    );
    const legacyOutput = await overrides.extract!({
      values: legacyValues,
    });

    // Worker path.
    const task: TaskEnvelopeView = {
      runId: "run-compat",
      workflowVersionId: "wfv-test",
      orgId: "org",
      nodeId: "extract_1",
      attemptNumber: 1,
      resourceClass: 0,
      affinityKey: "",
      traceId: "",
      spanId: "",
      nodeType: "extract",
      nodePayloadJson: "",
    };
    const workerResult = await adapter(task, abort);
    assert.equal(workerResult.completed, true, "worker extract completed");
    assert.equal(
      workerResult.output,
      JSON.stringify(legacyOutput),
      "extract output identical (legacy vs worker)",
    );
    ok("extract_1: worker output === legacy output");
  }

  // --- Node: send_email_1 (interpolation + email sink) ---------------------
  {
    const node = graph.nodes.find((n) => n.id === "send_email_1")!;
    const legacyValues = Object.fromEntries(
      Object.entries(node.data.values).map(([k, t]) => [
        k,
        interpolate({ text: t, outputs: outputsByNode.send_email_1 }),
      ]),
    );
    emailSink.reset();
    // Legacy path also uses the email test sink (M24 step 7): automated
    // distributed tests must never send real email. Parity is about the
    // adapter + interpolation, with the side effect mocked identically.
    const legacyOutput = await emailSink.executor({
      values: legacyValues,
      getStagehand: async () => {
        throw new Error("no browser in test");
      },
    });
    const legacyEmail = emailSink.sent[0];

    emailSink.reset();
    const task: TaskEnvelopeView = {
      runId: "run-compat",
      workflowVersionId: "wfv-test",
      orgId: "org",
      nodeId: "send_email_1",
      attemptNumber: 1,
      resourceClass: 0,
      affinityKey: "",
      traceId: "",
      spanId: "",
      nodeType: "send-email",
      nodePayloadJson: "",
    };
    const workerResult = await adapter(task, abort);
    assert.equal(workerResult.completed, true, "worker email completed");
    assert.equal(
      workerResult.output,
      JSON.stringify(legacyOutput),
      "email output identical (legacy vs worker)",
    );
    // The interpolated body must match what the legacy path produced.
    assert.equal(emailSink.sent[0].body, legacyEmail.body, "email body parity");
    assert.equal(
      emailSink.sent[0].body,
      "Found 3 items: extracted:top stories",
      "interpolation resolved upstream outputs",
    );
    ok("send_email_1: worker output === legacy output (interpolated body parity)");
  }

  // --- Node: start (trigger, no executor) ----------------------------------
  {
    const task: TaskEnvelopeView = {
      runId: "run-compat",
      workflowVersionId: "wfv-test",
      orgId: "org",
      nodeId: "start",
      attemptNumber: 1,
      resourceClass: 0,
      affinityKey: "",
      traceId: "",
      spanId: "",
      nodeType: "start",
      nodePayloadJson: "",
    };
    const workerResult = await adapter(task, abort);
    assert.equal(workerResult.completed, true, "trigger node completes");
    assert.equal(
      workerResult.output,
      JSON.stringify({ skipped: true }),
      "trigger node produces skipped output (matches legacy done-no-work)",
    );
    ok("start: trigger node completes with no work (legacy parity)");
  }

  // --- Error path: unknown node id -> permanent failure --------------------
  {
    const task: TaskEnvelopeView = {
      runId: "run-compat",
      workflowVersionId: "wfv-test",
      orgId: "org",
      nodeId: "does_not_exist",
      attemptNumber: 1,
      resourceClass: 0,
      affinityKey: "",
      traceId: "",
      spanId: "",
      nodeType: "extract",
      nodePayloadJson: "",
    };
    const workerResult = await adapter(task, abort);
    assert.equal(workerResult.completed, false, "unknown node fails");
    assert.equal(workerResult.retryable, false, "unknown node is permanent");
    ok("unknown node id -> permanent failure (no retry)");
  }

  console.log(
    `\nALL M24 WORKER-EXECUTOR COMPATIBILITY TESTS PASSED! (${passed}/${passed})`,
  );
}

main().catch((err) => {
  console.error("M24 compatibility test FAILED:", err);
  process.exit(1);
});
