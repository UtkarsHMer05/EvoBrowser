# Data Passthrough — prompts

Let a node read the output of any upstream node: at runtime, `{{nodeId.path}}`
placeholders in a field are replaced with data from nodes that already ran, and the
inspector shows clickable chips that insert those placeholders for you.

Work through these in order.

---

## 1. The interpolation helper

```
I want to let a workflow field pull in another node's output by writing a
placeholder like {{ someNodeId.title }} or {{ someNodeId.items[0].name }} inside it.

To make that work I need a small pure helper that takes one field's text plus a
collection of every node's output from this run — keyed by node id — and returns
the text with each placeholder swapped for the value it points at.

If a placeholder resolves to nothing, replace it with an empty string; if it
resolves to an object, drop in the JSON.

It has to resolve nested paths, so lean on whatever small get-by-path utility is
cleanest.

Put it with the workflow feature's helpers and call it `interpolate`.
```

## 2. Pass outputs through the run

```
In @run-workflow.ts, let each node use the output of the nodes before it.

Right now we run nodes in order and throw their results away. Instead, keep each
node's result as we go, keyed by its id.

Then, right before running a node, replace the placeholders in its field values
with the matching upstream data using the `interpolate` helper we just made.

Nodes already run in dependency order, so anything a node references has produced
its output by the time we get there.
```

---

## 3. Declare each node's outputs (manual)

In the node registry, add the outputs a downstream node can reference: a small
`{ path, label }` shape and an `outputs` list on each node. `start` exposes nothing;
`open-url` exposes `url` and `title`.

---

## 4. The upstream-outputs hook

```
I want people to reference an upstream node's output without typing raw node ids.

Give me a hook that takes the node I currently have selected and returns every
output that any node upstream of it produces — each as a ready-to-insert {{ }}
token, a friendly label like "Open URL 1 · Title", and the source node's type so
I can show its icon.

Follow the connections all the way back up the graph, not just the node's direct
parents, and re-compute as I connect and disconnect edges.

Each node already declares which outputs it exposes.

Put it with the workflow feature's hooks and call it `useUpstreamConnections`.
Follow the CLAUDE.md rule for using ReactFlow.
```

## 5. Connections chips in the inspector

```
In @right-sidebar.tsx, under the selected node's fields, add a "Connections"
section that shows up whenever `useUpstreamConnections` returns any outputs for that
node.

Render each available output as a small chip with the source node's icon and its
label.

When I click a chip, insert its token into whichever field I was last editing —
or the first field if I haven't touched one yet.

I want this for every connected node, not only the ones with a large text field.

Follow the CLAUDE.md rule for using ReactFlow.
```
