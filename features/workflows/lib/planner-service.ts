import toposort from "toposort";
import { getPlannerNodeCatalog } from "@/features/workflows/lib/planner-catalog";
import {
  WorkflowPlanSchema,
  type WorkflowPlan,
} from "@/features/workflows/lib/planner-types";

export interface GenerateWorkflowPlanOptions {
  goal: string;
}

// Transient gateway errors worth retrying: request timeout, rate limit, and the
// 5xx family (a 502 "gateway error: backend" is the classic transient blip).
// 401 is deliberately NOT retried — it's a configuration problem, not a blip.
const RETRYABLE_STATUSES = new Set([408, 429, 500, 502, 503, 504]);
// Free-tier upstreams behind the gateway can be down for tens of seconds at a
// time (observed: consecutive 503s that recover on their own). Retry enough
// times with capped exponential backoff to ride out a short outage rather
// than failing the whole generation after a couple of seconds.
const MAX_ATTEMPTS = 5;
const MAX_BACKOFF_MS = 8000;

// The outcome of a single provider call, so the retry loop can tell a
// transient failure (retry) from a permanent one (surface immediately).
type AttemptResult =
  | { ok: true; content: string }
  | { ok: false; retryable: boolean; message: string };

/**
 * Server-only service that sends the user's natural language goal and the derived
 * node catalog to the configured AI provider (TokenRouter) and returns a validated
 * WorkflowPlan.
 */
export async function generateWorkflowPlan({
  goal,
}: GenerateWorkflowPlanOptions): Promise<WorkflowPlan> {
  const apiKey = process.env.TOKENROUTER_API_KEY;
  if (!apiKey) {
    throw new Error(
      "TOKENROUTER_API_KEY is not configured in the server environment (.env.local).",
    );
  }

  const baseUrl = (
    process.env.TOKENROUTER_BASE_URL || "https://api.tokenrouter.com/v1"
  ).replace(/\/+$/, "");

  const model =
    process.env.PLANNER_MODEL || "deepseek/deepseek-v4-pro-0813-free";

  const catalog = getPlannerNodeCatalog();
  const catalogSummary = JSON.stringify(catalog, null, 2);

  const systemPrompt = `You are the AI Workflow Planner for EvoBrowser, an automated browser workflow engine.
Your task is to convert the user's automation goal into a valid, structured, executable workflow plan.

### AVAILABLE NODE CATALOG:
${catalogSummary}

### RULES & CONSTRAINTS:
1. ONLY use node types from the catalog above. Never invent new node types or capabilities.
2. If the user's goal CANNOT be achieved using the available nodes (e.g. requires unsupported external integrations like WhatsApp, Twilio SMS, direct SQL database write, etc.), you MUST return:
   "canBuild": false,
   "unsupportedReason": "A clear explanation of which capability is missing in EvoBrowser"
   with "nodes": [] and "edges": [].
3. If the goal can be built:
   - "canBuild": true
   - "name": A concise, descriptive title for the workflow (e.g. "Hacker News Top Stories Digest")
   - "nodes": An array of node objects.
     - Every workflow MUST start with exactly one "start" node (kind: "trigger", type: "start", title: "Start", values: {}).
     - Action nodes must be ordered logically to achieve the user's goal.
     - Each node must have a unique alphanumeric id (e.g. "start_1", "open_url_1", "extract_1", "send_email_1").
     - Populate required and relevant input fields in the "values" object using key names from the catalog.
     - You may use interpolation syntax "{{ <nodeId>.<outputField> }}" to pass upstream outputs to downstream fields (e.g. "{{ extract_1.extraction }}" or "{{ open_url_1.title }}").
   - "edges": An array of directed edge objects connecting the nodes in execution order:
     - Each edge must have: "id" (e.g. "e1", "e2"), "source" (upstream node id), "target" (downstream node id).
     - The workflow must be a connected Directed Acyclic Graph (DAG) with NO cycles.
     - Every node (except "start") should be reachable from upstream nodes.

### OUTPUT FORMAT:
You MUST respond with valid JSON matching this exact structure:
{
  "version": "1.0",
  "name": "Workflow Name",
  "canBuild": true,
  "unsupportedReason": "Optional string if canBuild is false",
  "nodes": [
    {
      "id": "start_1",
      "type": "start",
      "title": "Start",
      "values": {}
    },
    {
      "id": "open_url_1",
      "type": "open-url",
      "title": "Open URL 1",
      "values": {
        "url": "https://news.ycombinator.com"
      }
    }
  ],
  "edges": [
    {
      "id": "e_start_to_open",
      "source": "start_1",
      "target": "open_url_1"
    }
  ]
}`;

  // One provider call. Network errors and transient statuses (429/5xx) are
  // reported as retryable; auth failures and empty responses are not.
  const callProviderOnce = async (): Promise<AttemptResult> => {
    let response: Response;
    try {
      response = await fetch(`${baseUrl}/chat/completions`, {
        method: "POST",
        headers: {
          "Content-Type": "application/json",
          Authorization: `Bearer ${apiKey}`,
        },
        body: JSON.stringify({
          model,
          messages: [
            { role: "system", content: systemPrompt },
            { role: "user", content: goal },
          ],
          temperature: 0.1,
          max_tokens: 3000,
          response_format: { type: "json_object" },
        }),
      });
    } catch (error) {
      // Network-level failure (DNS, connection reset, timeout) — transient.
      return {
        ok: false,
        retryable: true,
        message: `AI Provider communication error: ${
          error instanceof Error ? error.message : String(error)
        }`,
      };
    }

    if (!response.ok) {
      const errorText = await response.text().catch(() => "");
      if (response.status === 401) {
        return {
          ok: false,
          retryable: false,
          message:
            "AI Provider authentication failed (401 Unauthorized). Please verify TOKENROUTER_API_KEY.",
        };
      }
      if (response.status === 429) {
        return {
          ok: false,
          retryable: true,
          message:
            "AI Provider rate limit exceeded (429 Too Many Requests). Please try again in a moment.",
        };
      }
      return {
        ok: false,
        retryable: RETRYABLE_STATUSES.has(response.status),
        message: `AI Provider request failed with status ${response.status}: ${
          errorText || response.statusText
        }`,
      };
    }

    const data = await response.json();
    const messageContent = data.choices?.[0]?.message?.content;

    if (!messageContent) {
      // Some reasoning models return content inside message or reasoning
      return {
        ok: false,
        retryable: true,
        message:
          "AI Provider returned an empty response content. Please try again or check the model configuration.",
      };
    }

    return { ok: true, content: messageContent };
  };

  // Retry transient failures with a short backoff so a single gateway blip
  // (e.g. a 502) doesn't fail the whole generation.
  let rawResponseText = "";
  let lastError = "";
  for (let attempt = 1; attempt <= MAX_ATTEMPTS; attempt++) {
    const result = await callProviderOnce();
    if (result.ok) {
      rawResponseText = result.content;
      break;
    }

    lastError = result.message;
    if (!result.retryable || attempt === MAX_ATTEMPTS) {
      throw new Error(result.message);
    }

    // Capped exponential backoff: 1s, 2s, 4s, 8s, ...
    const backoffMs = Math.min(1000 * 2 ** (attempt - 1), MAX_BACKOFF_MS);
    await new Promise((r) => setTimeout(r, backoffMs));
  }

  if (!rawResponseText) {
    throw new Error(lastError || "AI Provider returned no content.");
  }

  // Parse JSON
  let parsedJson: unknown;
  try {
    // Strip possible markdown code fences if model included ```json ... ```
    const sanitized = rawResponseText
      .replace(/^```json\s*/i, "")
      .replace(/^```\s*/i, "")
      .replace(/\s*```$/i, "")
      .trim();
    parsedJson = JSON.parse(sanitized);
  } catch {
    throw new Error(
      "AI Provider returned malformed JSON output that could not be parsed.",
    );
  }

  // Runtime schema validation
  const validationResult = WorkflowPlanSchema.safeParse(parsedJson);
  if (!validationResult.success) {
    throw new Error(
      `AI Provider response does not match the required WorkflowPlan schema: ${validationResult.error.message}`,
    );
  }

  const plan = validationResult.data;

  // Semantic post-validation
  if (!plan.canBuild) {
    return {
      version: plan.version || "1.0",
      name: plan.name || "Unsupported Workflow",
      canBuild: false,
      unsupportedReason:
        plan.unsupportedReason ||
        "The requested automation cannot be built with the currently available workflow nodes.",
      nodes: [],
      edges: [],
    };
  }

  // Validation rules for buildable workflows:
  const nodeMap = new Map(plan.nodes.map((n) => [n.id, n]));

  // 1. Unique node IDs
  if (nodeMap.size !== plan.nodes.length) {
    throw new Error("Generated workflow plan contains duplicate node IDs.");
  }

  // 2. Exactly one start trigger node
  const startNodes = plan.nodes.filter((n) => n.type === "start");
  if (startNodes.length !== 1) {
    throw new Error(
      `Generated workflow plan must have exactly 1 Start trigger (found ${startNodes.length}).`,
    );
  }

  // 3. Only registered node types
  for (const node of plan.nodes) {
    const def = catalog[node.type];
    if (!def) {
      throw new Error(
        `Generated plan referenced unknown node type "${node.type}". Only catalog nodes are allowed.`,
      );
    }
  }

  // 4. Edges must reference existing node IDs
  for (const edge of plan.edges) {
    if (!nodeMap.has(edge.source)) {
      throw new Error(
        `Generated edge references non-existent source node ID "${edge.source}".`,
      );
    }
    if (!nodeMap.has(edge.target)) {
      throw new Error(
        `Generated edge references non-existent target node ID "${edge.target}".`,
      );
    }
  }

  // 5. Must have at least 1 edge if there are action nodes
  if (plan.nodes.length > 1 && plan.edges.length === 0) {
    throw new Error(
      "Generated workflow has multiple nodes but no connecting edges.",
    );
  }

  // 6. Check for cycles using toposort
  if (plan.edges.length > 0) {
    try {
      toposort(plan.edges.map((e) => [e.source, e.target]));
    } catch {
      throw new Error(
        "Generated workflow plan contains a cycle (loop), which is not allowed.",
      );
    }
  }

  return plan;
}
