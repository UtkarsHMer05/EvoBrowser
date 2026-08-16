# Remaining Nodes — prompt

Fill out the workflow's browser nodes. First we document the node pattern in `AGENTS.md`,
so the codebase itself tells the agent how to add a node — then one short prompt per node
adds the four remaining nodes, one at a time.

---

## 1. Document the node pattern

Add this section to `AGENTS.md`:

```markdown
# Adding a workflow node

Three edits, all under `features/workflows/nodes/`:

1. the impl file (e.g. `open-url.ts`) — the node's executor logic,
2. register it in `node-executors.ts` — the `satisfies` contract makes a missing
   executor a compile error for action nodes,
3. add its manifest entry in `node-registry.ts` — kind, label, icon, accent, its
   input `fields`, and the `outputs` downstream nodes can reference.

The run task and the canvas step node are registry-driven — never touch them to add
a node.
```

---

## 2. Add the `act` node

```
Add an `act` node — a Stagehand-powered action node with a single multi-line
instruction field.

It performs an action on the page (click, type, scroll) using Stagehand's act method.
Surface whether it worked, a short message, and the resulting URL as outputs.

Follow our convention for adding a node.
```

## 3. Add the `extract` node

```
Add an `extract` node — a Stagehand-powered action node with a single multi-line
instruction field.

It pulls data off the page described in the instruction, using Stagehand's extract
method. Surface the extracted result as an output.

Follow our convention for adding a node.
```

## 4. Add the `observe` node

```
Add an `observe` node — a Stagehand-powered action node with a single multi-line
instruction field.

It finds the actionable elements matching the instruction, using Stagehand's observe
method. Surface the matches, each with its selector and description, as outputs.

Follow our convention for adding a node.
```

## 5. Add the `agent` node

```
Add an `agent` node — a Stagehand-powered action node with a single multi-line
instruction field.

It runs an autonomous, multi-step browser task from one instruction, using Stagehand's
agent. Surface whether it succeeded, a summary message, and whether it completed as
outputs.

Follow our convention for adding a node.
```
