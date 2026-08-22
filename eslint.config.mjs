import { defineConfig, globalIgnores } from "eslint/config";
import nextVitals from "eslint-config-next/core-web-vitals";
import nextTs from "eslint-config-next/typescript";

const eslintConfig = defineConfig([
  ...nextVitals,
  ...nextTs,
  // Override default ignores of eslint-config-next.
  globalIgnores([
    // Default ignores of eslint-config-next:
    "node_modules/**",
    ".next/**",
    "out/**",
    "build/**",
    "next-env.d.ts",
    // Trigger.dev build artifacts.
    ".trigger/**",
    // Legacy course template files — not imported anywhere.
    "templates/**",
    // Phase-2 C++ engine — pure C++, never linted by ESLint. Its CMake build
    // trees emit compiler_depend.ts files that would otherwise trip the parser.
    "engine/**",
  ]),
]);

export default eslintConfig;
