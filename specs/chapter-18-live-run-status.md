# Live Run Status — prompts

While a workflow runs, the canvas shows which node is in progress (spinner + blue
border) and which node failed (red border). Powered by Trigger.dev Realtime: the run
streams each step's status, and the canvas subscribes and paints each node.

Work through these in order.

---

## 1. Stream step status from the run

```
In @run-workflow.ts, stream each node's live status so the canvas can show progress.

Before running anything, build a list of the steps we're about to run — each just a
node id and a status starting at "pending" — and publish it to the run's metadata
under "steps". Export the step type and call it `RunStep`.

As we go, set a node's status to "running" before its executor, "done" after it
succeeds, and "failed" if it throws (then stop the run). Re-publish the metadata on
every change.

Right after marking a node "running", force the metadata to flush — otherwise that
state gets overwritten by "done" before it's ever pushed and we'd never see the
spinner.

Also flush right after marking a node "failed", before the run stops — a thrown run
returns no output, so the flushed metadata is the only way that failed state ever
reaches the canvas.

Also return the final steps from the task so a successful run's finished state is
guaranteed. Keep the existing interpolation and execution as-is.

Use the Trigger.dev tasks skill.
```

## 2. Subscribe to the workflow's runs

```
I want one shared subscription to this workflow's runs that any component on the
canvas can read.

Give me a client provider that subscribes to the workflow's runs in realtime by
their tag (workflow:<id>), using a public access token passed in as a prop. Call it
`WorkflowRunsProvider`.

Expose a `useLatestRunSteps` hook that returns the most recent run's steps plus
whether it's still live — preferring the run's final output steps and falling back
to the live metadata steps. "Live" means the run is queued or executing.

Put it with the workflow feature's components. Use the Trigger.dev realtime &
frontend skill.
```

## 3. Wire the subscription into the page

```
In the workflow page (@page.tsx), wire up the realtime subscription.

Mint a read-only public token scoped to this workflow's run tag, good for about an
hour, and wrap the canvas shell in the `WorkflowRunsProvider` we just made — passing
it the workflow id and that token.

Use the Trigger.dev realtime & frontend skill.
```

## 4. Paint the nodes

```
In @step-node.tsx, make each node reflect the latest run.

Look up this node's status from `useLatestRunSteps` by matching the node id. When
it's running, show a spinner in place of the node's icon and give it a blue border;
when it failed, give it a destructive border.

Only treat a node as running while the run is actually live — if the run has ended, a
node left marked "running" should stop spinning.
```
