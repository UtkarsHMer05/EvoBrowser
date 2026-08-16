# Polish — prompts

Two small finishes that close loose ends from earlier chapters: the Run button can start a
workflow but not stop one, and the "New workflow" button on the empty page doesn't do anything
yet. Both just connect pieces that already exist.

---

## Prompt 1 — Run/Stop toggle

```
The Run button in @features/workflows/components/right-sidebar.tsx can only start a
workflow — there's no way to stop one that's running.

Make it a toggle: while a run is in flight it becomes a Stop button that cancels that
run, and when nothing's running it's Run as before.

@features/workflows/components/workflow-runs-provider.tsx already tracks which runs
are live, and @features/workflows/actions.ts has an action that cancels a run by its
id. At most one run is live at a time.
```

---

## Prompt 2 — wire the empty-state New workflow button

```
In @app/(dashboard)/page.tsx, the "New workflow" button in the empty state doesn't do
anything yet — wire it to create a workflow.

Keep it simple: a direct call to the create workflow action in
@features/workflows/actions.ts, no optimistic update. Give the new workflow a
generated name the same way the sidebar's New workflow button does.
```
