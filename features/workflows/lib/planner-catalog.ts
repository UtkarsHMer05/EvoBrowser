import {
  nodeRegistry,
  type NodeField,
  type NodeOutput,
  type StepNodeKind,
} from "@/features/workflows/nodes/node-registry";

export interface PlannerNodeField {
  key: string;
  label: string;
  placeholder?: string;
  multiline?: boolean;
  required?: boolean;
}

export interface PlannerNodeOutput {
  path: string;
  label: string;
}

export interface PlannerNodeCatalogItem {
  type: string;
  kind: StepNodeKind;
  label: string;
  fields: PlannerNodeField[];
  outputs: PlannerNodeOutput[];
}

/**
 * Derives a clean, planner-relevant machine-readable catalog from the actual nodeRegistry.
 * Strips UI-only properties (icons, accent Tailwind classes) and keeps only semantic
 * metadata, input fields, and output paths.
 */
export function getPlannerNodeCatalog(): Record<string, PlannerNodeCatalogItem> {
  const catalog: Record<string, PlannerNodeCatalogItem> = {};

  for (const [key, def] of Object.entries(nodeRegistry)) {
    catalog[key] = {
      type: def.type,
      kind: def.kind,
      label: def.label,
      fields: (def.fields as NodeField[]).map((f) => ({
        key: f.key,
        label: f.label,
        placeholder: f.placeholder,
        multiline: f.multiline,
        required: f.required,
      })),
      outputs: (def.outputs as NodeOutput[]).map((o) => ({
        path: o.path,
        label: o.label,
      })),
    };
  }

  return catalog;
}
