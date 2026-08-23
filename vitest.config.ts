import { fileURLToPath } from "node:url";

import { defineConfig, configDefaults } from "vitest/config";

// The repo's "@" -> project-root TS alias (tsconfig paths), which vitest must
// be told about explicitly.
const rootDir = fileURLToPath(new URL(".", import.meta.url));

// Node-side regression suites (Phase-1 planning/lifecycle/engines, the Phase-2
// worker runtime, and the cross-language contract fixtures). These files were
// originally standalone `tsx` scripts; they are registered as single-file
// suites so vitest provides the runner, watch mode, isolation, and coverage
// while each file keeps its own sequential, scenario-based style.
//
// Mock-based unit suites (server-action + per-run artifact route handlers,
// run authorization) live beside their subjects under features/workflows/ and
// app/api/. They are vitest-only (vi.mock requires the runner) and carry no
// local-infra dependency, so their thresholds hold in every environment.
//
// Distributed sections still SKIP themselves when local Redis/Postgres
// (127.0.0.1:6390 / 127.0.0.1:5433) are down — run scripts/phase2/up.sh +
// migrate-local.sh to exercise them. CI runs `npm test` inside its
// `distributed` job, where both services exist.
export default defineConfig({
  resolve: {
    alias: {
      "@": rootDir,
    },
  },
  test: {
    environment: "node",
    include: [
      "features/workflows/**/*.test.ts",
      "worker/src/**/*.test.ts",
      "app/**/*.test.ts",
    ],
    // .kilo holds linked git worktrees (checked-out branch copies) — never
    // run their suites from here.
    exclude: [...configDefaults.exclude, ".kilo/**"],
    // The suites were designed to run one after another: several touch shared
    // local endpoints (the scheduler gRPC port in tests that opt into it, the
    // Phase-2 Redis/Postgres) and print interleaved milestone banners. Keep
    // the historical ordering instead of racing files across workers.
    fileParallelism: false,
    testTimeout: 180_000,
    hookTimeout: 60_000,
    coverage: {
      provider: "v8",
      include: [
        "features/workflows/lib/**/*.ts",
        "features/workflows/nodes/**/*.ts",
        "features/workflows/tasks/**/*.ts",
        "features/workflows/actions.ts",
        "app/api/**/*.ts",
        "worker/src/**/*.ts",
      ],
      exclude: ["**/*.test.ts"],
      reporter: ["text", "html"],
      // Ratchet policy: hard floors ONLY on files whose suites are pure unit
      // tests with every dependency mocked (identical results locally and in
      // CI). The modest global floor sits under both environments' observed
      // totals (44.11% lines with infra down) so only real regressions trip
      // it; tighten deliberately over time.
      thresholds: {
        lines: 40,
        statements: 40,
        "features/workflows/lib/run-authorization.ts": {
          lines: 90,
          functions: 90,
          branches: 80,
          statements: 90,
        },
        "features/workflows/lib/evo-run-events-route.ts": {
          lines: 80,
          functions: 70,
          branches: 60,
          statements: 80,
        },
        "app/api/live-view/**": { lines: 85 },
        "app/api/replays/**": { lines: 85 },
        "app/api/runs/**": { lines: 80 },
      },
    },
  },
});
