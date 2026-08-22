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
// Distributed sections still SKIP themselves when local Redis/Postgres
// (127.0.0.1:6390 / 127.0.0.1:5433) are down — run scripts/phase2/up.sh +
// migrate-local.sh to exercise them.
export default defineConfig({
  resolve: {
    alias: {
      "@": rootDir,
    },
  },
  test: {
    environment: "node",
    include: ["features/workflows/lib/**/*.test.ts", "worker/src/**/*.test.ts"],
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
        "worker/src/**/*.ts",
      ],
      exclude: ["**/*.test.ts"],
      reporter: ["text", "html"],
    },
  },
});
