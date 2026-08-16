Fix the hydration error in the Canvas component. The mismatch happens because next-themes has no theme value on the server, so React Flow renders as light on the server and dark on the client.

Add a hydration-safe mounted flag using useSyncExternalStore that is false during server render and hydration and true after mount. Switch from theme to resolvedTheme, and until the component is mounted always use "light" as the colorMode, only switching to the real resolved theme once mounted.

This keeps the server and client render identical.
