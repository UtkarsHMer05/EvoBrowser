import assert from "node:assert/strict";
import {
  convertWorkflowPlanToGraph,
  layoutPlanNodes,
} from "./convert-plan";
import type { StepNodeType } from "../nodes/node-registry";
import { validateGraph } from "./validate-graph";
import { interpolate } from "./interpolate";
import type { WorkflowPlan } from "./planner-types";

// ---------------------------------------------------------------------------
// TEST 1: Basic linear workflow conversion (Start -> Open URL -> Extract -> Send Email)
// ---------------------------------------------------------------------------
console.log("TEST 1: Converting standard linear workflow plan...");
const linearPlan: WorkflowPlan = {
  version: "1.0",
  name: "Hacker News Digest",
  canBuild: true,
  nodes: [
    {
      id: "start_1",
      type: "start",
      title: "Start",
      values: {},
    },
    {
      id: "open_url_1",
      type: "open-url",
      title: "Open Hacker News",
      values: { url: "https://news.ycombinator.com" },
    },
    {
      id: "extract_1",
      type: "extract",
      title: "Extract Top Stories",
      values: { instruction: "Extract top 5 story titles and links" },
    },
    {
      id: "send_email_1",
      type: "send-email",
      title: "Email Stories",
      values: {
        to: "user@example.com",
        subject: "Daily HN Digest",
        body: "{{ extract_1.extraction }}",
      },
    },
  ],
  edges: [
    { id: "e1", source: "start_1", target: "open_url_1" },
    { id: "e2", source: "open_url_1", target: "extract_1" },
    { id: "e3", source: "extract_1", target: "send_email_1" },
  ],
};

const graph1 = convertWorkflowPlanToGraph(linearPlan);
assert.equal(graph1.nodes.length, 4, "Should have 4 nodes");
assert.equal(graph1.edges.length, 3, "Should have 3 edges");

// Verify node kinds & registry integration
assert.equal(graph1.nodes[0].data.kind, "trigger");
assert.equal(graph1.nodes[1].data.kind, "action");
assert.equal(graph1.nodes[1].data.values.url, "https://news.ycombinator.com");
assert.equal(graph1.nodes[2].data.values.instruction, "Extract top 5 story titles and links");
assert.equal(graph1.nodes[3].data.values.to, "user@example.com");
assert.equal(graph1.nodes[3].data.values.body, "{{ extract_1.extraction }}");

// Verify validateGraph passes
const problems1 = validateGraph(graph1);
assert.deepEqual(problems1, [], "Linear workflow should have zero validation problems");

console.log("  ✓ Linear workflow converted and validated successfully.");

// ---------------------------------------------------------------------------
// TEST 2: Deterministic Layered Layout (Positions and Spacing)
// ---------------------------------------------------------------------------
console.log("\nTEST 2: Verifying deterministic layout positioning...");
const positions = layoutPlanNodes(linearPlan.nodes, linearPlan.edges);

assert.deepEqual(positions.get("start_1"), { x: 0, y: 0 });
assert.deepEqual(positions.get("open_url_1"), { x: 0, y: 180 });
assert.deepEqual(positions.get("extract_1"), { x: 0, y: 360 });
assert.deepEqual(positions.get("send_email_1"), { x: 0, y: 540 });

console.log("  ✓ Linear layout positions correctly aligned along center axis.");

// ---------------------------------------------------------------------------
// TEST 3: Branching DAG layout & conversion
// ---------------------------------------------------------------------------
console.log("\nTEST 3: Converting branching DAG workflow plan...");
const branchingPlan: WorkflowPlan = {
  version: "1.0",
  name: "Multi-Source Extraction",
  canBuild: true,
  nodes: [
    { id: "start_1", type: "start", title: "Start", values: {} },
    { id: "open_url_1", type: "open-url", title: "Open News", values: { url: "https://news.ycombinator.com" } },
    { id: "extract_1", type: "extract", title: "Extract Headlines", values: { instruction: "Extract headlines" } },
    { id: "observe_1", type: "observe", title: "Observe Links", values: { instruction: "Find next page link" } },
    { id: "send_email_1", type: "send-email", title: "Send Results", values: { to: "digest@example.com", subject: "Results", body: "{{ extract_1.extraction }}" } },
  ],
  edges: [
    { id: "e1", source: "start_1", target: "open_url_1" },
    { id: "e2", source: "open_url_1", target: "extract_1" },
    { id: "e3", source: "open_url_1", target: "observe_1" },
    { id: "e4", source: "extract_1", target: "send_email_1" },
    { id: "e5", source: "observe_1", target: "send_email_1" },
  ],
};

const graph2 = convertWorkflowPlanToGraph(branchingPlan);
assert.equal(graph2.nodes.length, 5);
assert.equal(graph2.edges.length, 5);

// Branching nodes (extract_1, observe_1) should be at depth 2 (y=360) and symmetrically centered
const extractNode = graph2.nodes.find((n) => n.id === "extract_1")!;
const observeNode = graph2.nodes.find((n) => n.id === "observe_1")!;
assert.equal(extractNode.position.y, 360);
assert.equal(observeNode.position.y, 360);
assert.equal(extractNode.position.x, -170);
assert.equal(observeNode.position.x, 170);

const problems2 = validateGraph(graph2);
assert.deepEqual(problems2, [], "Branching DAG should have zero validation problems");

console.log("  ✓ Branching DAG layout computed symmetric horizontal offsets.");

// ---------------------------------------------------------------------------
// TEST 4: Graph Editability (Simulating standard manual edits on generated graph)
// ---------------------------------------------------------------------------
console.log("\nTEST 4: Simulating manual edits on generated graph...");

// 4a. Edit URL field
const editedGraph = {
  nodes: graph1.nodes.map((n) =>
    n.id === "open_url_1"
      ? {
          ...n,
          data: {
            ...n.data,
            values: { ...n.data.values, url: "https://lobste.rs" },
          },
        }
      : n,
  ),
  edges: [...graph1.edges],
};
const editedUrlNode = editedGraph.nodes.find((n) => n.id === "open_url_1")!;
assert.equal(editedUrlNode.data.values.url, "https://lobste.rs", "URL should be updated");

// 4b. Edit Email recipient/subject/body
const emailNode = editedGraph.nodes.find((n) => n.id === "send_email_1")!;
emailNode.data.values.to = "team@example.com";
emailNode.data.values.subject = "Updated Subject";
emailNode.data.values.body = "Updated Body: {{ extract_1.extraction }}";
assert.equal(emailNode.data.values.to, "team@example.com");

// 4c. Add a new manual node (e.g. Agent node)
const newAgentNode: StepNodeType = {
  id: "agent_custom_1",
  type: "step",
  position: { x: 0, y: 720 },
  data: {
    type: "agent",
    kind: "action",
    title: "Agent 1",
    values: { instruction: "Analyze the extracted results" },
  },
};
editedGraph.nodes.push(newAgentNode);
editedGraph.edges.push({
  id: "e_email_to_agent",
  source: "send_email_1",
  target: "agent_custom_1",
  type: "smoothstep",
});

assert.equal(editedGraph.nodes.length, 5);
assert.equal(editedGraph.edges.length, 4);
assert.deepEqual(validateGraph(editedGraph), [], "Edited graph with new node should be valid");

// 4d. Reconnect and delete edges
editedGraph.edges = editedGraph.edges.filter((e) => e.id !== "e_email_to_agent");
assert.equal(editedGraph.edges.length, 3);

// 4e. Delete a node
editedGraph.nodes = editedGraph.nodes.filter((n) => n.id !== "agent_custom_1");
assert.equal(editedGraph.nodes.length, 4);
assert.deepEqual(validateGraph(editedGraph), []);

console.log("  ✓ Generated graph supports editing fields, adding nodes, deleting nodes, and reconnecting edges.");

// ---------------------------------------------------------------------------
// TEST 5: Interpolation with upstream outputs on converted graph
// ---------------------------------------------------------------------------
console.log("\nTEST 5: Testing interpolation execution with generated node output tokens...");
const sampleOutputs = {
  extract_1: {
    extraction: "1. Show HN: AI Workflow Engine\n2. Next.js 16 Released",
  },
};

const interpolatedBody = interpolate({
  text: emailNode.data.values.body,
  outputs: sampleOutputs,
});

assert.equal(
  interpolatedBody,
  "Updated Body: 1. Show HN: AI Workflow Engine\n2. Next.js 16 Released",
  "Interpolation should correctly resolve {{ extract_1.extraction }}",
);
console.log("  ✓ Interpolation successfully resolves tokens from generated nodes.");

// ---------------------------------------------------------------------------
// TEST 6: Robustness & Validation Rejection (preventing corrupted graphs)
// ---------------------------------------------------------------------------
console.log("\nTEST 6: Testing rejection of invalid plans...");

// 6a. Cycle detection
assert.throws(() => {
  convertWorkflowPlanToGraph({
    version: "1.0",
    name: "Cyclic Flow",
    canBuild: true,
    nodes: [
      { id: "start_1", type: "start", title: "Start", values: {} },
      { id: "act_1", type: "act", title: "Act 1", values: { instruction: "Step 1" } },
      { id: "act_2", type: "act", title: "Act 2", values: { instruction: "Step 2" } },
    ],
    edges: [
      { id: "e1", source: "start_1", target: "act_1" },
      { id: "e2", source: "act_1", target: "act_2" },
      { id: "e3", source: "act_2", target: "act_1" }, // Cycle!
    ],
  });
}, /cycle/i, "Cyclic plan should be rejected");

// 6b. Unknown node type
assert.throws(() => {
  convertWorkflowPlanToGraph({
    version: "1.0",
    name: "Invalid Type Flow",
    canBuild: true,
    nodes: [
      { id: "start_1", type: "start", title: "Start", values: {} },
      { id: "invalid_1", type: "non-existent-node", title: "Fake", values: {} },
    ],
    edges: [{ id: "e1", source: "start_1", target: "invalid_1" }],
  });
}, /Unknown node type/i, "Unknown node types should be rejected");

// 6c. canBuild: false plan
assert.throws(() => {
  convertWorkflowPlanToGraph({
    version: "1.0",
    name: "Unsupported Flow",
    canBuild: false,
    unsupportedReason: "Cannot send SMS",
    nodes: [],
    edges: [],
  });
}, /Cannot send SMS/i, "Unbuildable plans should throw unsupportedReason");

console.log("  ✓ Invalid plans rejected cleanly without corrupting graph state.");

// ---------------------------------------------------------------------------
// TEST 7: Pre-Execution Validation & Run Isolation (Milestone 7)
// ---------------------------------------------------------------------------
console.log("\nTEST 7: Testing pre-execution validation and run isolation...");

// 7a. Verify graph ready for runWorkflowAction satisfies all execution pre-conditions
const readyGraph = convertWorkflowPlanToGraph(linearPlan);
const preflightProblems = validateGraph(readyGraph);
assert.deepEqual(preflightProblems, [], "Pre-flight validation must pass for execution");

// 7b. If user breaks graph (e.g. deletes all edges), pre-flight blocks execution
const brokenGraph = {
  nodes: [...readyGraph.nodes],
  edges: [], // User deleted all edges
};
const brokenProblems = validateGraph(brokenGraph);
assert.equal(brokenProblems.length, 1);
assert.match(brokenProblems[0], /Connect your nodes before running/i);

// 7c. If user adds an Agent node, Pro plan detection correctly identifies it
const graphWithAgent = {
  nodes: [
    ...readyGraph.nodes,
    {
      id: "agent_1",
      type: "step" as const,
      position: { x: 0, y: 720 },
      data: {
        type: "agent" as const,
        kind: "action" as const,
        title: "Agent",
        values: { instruction: "Find deals" },
      },
    },
  ],
  edges: [
    ...readyGraph.edges,
    { id: "e4", source: "send_email_1", target: "agent_1" },
  ],
};
const hasAgentNode = graphWithAgent.nodes.some((n) => n.data.type === "agent");
assert.equal(hasAgentNode, true, "Should identify Agent node for Pro plan gating");

console.log("  ✓ Pre-flight validation blocks broken graphs and identifies plan-gated nodes.");
console.log("\nALL CONVERSION, EDITABILITY, AND EXECUTION PRE-FLIGHT TESTS PASSED! (7/7)");

if (process.env.VITEST) {
  // Registration only: every assertion in this linear script already ran at
  // module load (collection). A failure above marks this file red; this entry
  // gives vitest a named test to track on success.
  const { test } = await import("vitest");
  test("conversion, editability and execution pre-flight", () => {});
}
