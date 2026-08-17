import type { Edge } from "@xyflow/react";
import {
  nodeRegistry,
  type NodeType,
  type StepNodeType,
} from "@/features/workflows/nodes/node-registry";
import type { WorkflowPlan } from "@/features/workflows/lib/planner-types";
import { validateGraph } from "@/features/workflows/lib/validate-graph";

export interface ConvertedWorkflowGraph {
  nodes: StepNodeType[];
  edges: Edge[];
}

/**
 * Computes deterministic 2D coordinates for a DAG using depth-layering.
 * Nodes at the same depth are centered horizontally with equal spacing.
 */
export function layoutPlanNodes(
  planNodes: WorkflowPlan["nodes"],
  planEdges: WorkflowPlan["edges"],
): Map<string, { x: number; y: number }> {
  const incoming = new Map<string, string[]>();

  for (const node of planNodes) {
    incoming.set(node.id, []);
  }

  for (const edge of planEdges) {
    if (incoming.has(edge.target)) {
      incoming.get(edge.target)!.push(edge.source);
    }
  }

  // Compute depth for each node (longest path from any root)
  const depths = new Map<string, number>();

  function getDepth(nodeId: string, visited = new Set<string>()): number {
    if (depths.has(nodeId)) return depths.get(nodeId)!;
    if (visited.has(nodeId)) return 0; // Guard against cycle loop

    visited.add(nodeId);
    const parents = incoming.get(nodeId) || [];
    if (parents.length === 0) {
      depths.set(nodeId, 0);
      return 0;
    }

    let maxParentDepth = 0;
    for (const parent of parents) {
      maxParentDepth = Math.max(maxParentDepth, getDepth(parent, visited));
    }

    const depth = maxParentDepth + 1;
    depths.set(nodeId, depth);
    return depth;
  }

  for (const node of planNodes) {
    getDepth(node.id);
  }

  // Group nodes by layer depth
  const layers = new Map<number, string[]>();
  for (const node of planNodes) {
    const d = depths.get(node.id) ?? 0;
    if (!layers.has(d)) {
      layers.set(d, []);
    }
    layers.get(d)!.push(node.id);
  }

  const positions = new Map<string, { x: number; y: number }>();
  const HORIZONTAL_SPACING = 340;
  const VERTICAL_SPACING = 180;

  for (const [depth, nodeIdsInLayer] of layers.entries()) {
    const count = nodeIdsInLayer.length;
    nodeIdsInLayer.forEach((nodeId, idx) => {
      const x = (idx - (count - 1) / 2) * HORIZONTAL_SPACING;
      const y = depth * VERTICAL_SPACING;
      positions.set(nodeId, { x, y });
    });
  }

  return positions;
}

/**
 * Converts a validated WorkflowPlan into standard StepNodeType[] and Edge[]
 * ready for React Flow and Liveblocks canvas display.
 */
export function convertWorkflowPlanToGraph(
  plan: WorkflowPlan,
): ConvertedWorkflowGraph {
  if (!plan.canBuild || plan.nodes.length === 0) {
    throw new Error(
      plan.unsupportedReason || "Workflow plan cannot be built.",
    );
  }

  const positions = layoutPlanNodes(plan.nodes, plan.edges);

  const nodes: StepNodeType[] = plan.nodes.map((node) => {
    const typeKey = node.type as NodeType;
    const def = nodeRegistry[typeKey];

    if (!def) {
      throw new Error(`Unknown node type "${node.type}" in workflow plan.`);
    }

    const position = positions.get(node.id) || { x: 0, y: 0 };

    // Ensure values only contain known field keys
    const validValues: Record<string, string> = {};
    if (node.values) {
      for (const field of def.fields) {
        if (node.values[field.key] !== undefined) {
          validValues[field.key] = String(node.values[field.key]);
        }
      }
    }

    return {
      id: node.id,
      type: "step",
      position,
      data: {
        type: typeKey,
        kind: def.kind,
        title: node.title || def.label,
        values: validValues,
      },
    };
  });

  const edges: Edge[] = plan.edges.map((edge) => ({
    id: edge.id || `e-${edge.source}-${edge.target}`,
    source: edge.source,
    target: edge.target,
    type: "smoothstep",
    style: { stroke: "var(--border)" },
  }));

  // Run standard graph validation backstop
  const problems = validateGraph({ nodes, edges });
  if (problems.length > 0) {
    throw new Error(`Generated graph validation failed: ${problems.join(" ")}`);
  }

  return { nodes, edges };
}
