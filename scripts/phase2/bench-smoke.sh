#!/usr/bin/env bash
# Milestone 38: benchmark smoke test for CI.
#
# Verifies the benchmark HARNESS works end-to-end without asserting any timing
# (M38 step 9: "verifies the harness works but does not assert timing on shared
# CI runners"). It runs evo-bench into a temp dir and checks that the manifest
# is well-formed: has the header, a metadata line identifying commit + build,
# and at least one raw + one summary sample per workload. Timing values are
# printed but never compared — shared CI runners make wall-clock numbers
# meaningless (evidence-grade numbers are M39, on the reference machine).
#
# Usage: scripts/phase2/bench-smoke.sh [path-to-evo-bench]
#   default binary: engine/build/evo-bench

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
BENCH_BIN="${1:-${REPO_ROOT}/engine/build/evo-bench}"

if [ ! -x "${BENCH_BIN}" ]; then
  echo "bench-smoke: SKIP (binary not found at ${BENCH_BIN}; build the engine first)"
  exit 0
fi

OUT="$(mktemp -d)"
trap 'rm -rf "${OUT}"' EXIT

echo "==> bench-smoke: running ${BENCH_BIN} -> ${OUT}"
"${BENCH_BIN}" "${OUT}" >/dev/null

MANIFEST="${OUT}/manifest.csv"
if [ ! -f "${MANIFEST}" ]; then
  echo "bench-smoke: FAIL (no manifest.csv produced)"
  exit 1
fi

fail=0

# Header present.
if ! head -1 "${MANIFEST}" | grep -q '^phase,workload,workers,seq_ms,con_ms,speedup,p50,p95,p99$'; then
  echo "bench-smoke: FAIL (manifest header missing/malformed)"
  fail=1
fi

# Metadata line identifies commit + build (provenance, per methodology §5).
if ! grep -q '^# metadata,commit=' "${MANIFEST}"; then
  echo "bench-smoke: FAIL (manifest missing commit metadata)"
  fail=1
fi
if ! grep -q 'build=' "${MANIFEST}"; then
  echo "bench-smoke: FAIL (manifest missing build-mode metadata)"
  fail=1
fi

# At least one raw + one summary sample exist.
raw_count=$(grep -c '^raw,' "${MANIFEST}" || true)
summary_count=$(grep -c '^summary,' "${MANIFEST}" || true)
if [ "${raw_count}" -lt 1 ]; then
  echo "bench-smoke: FAIL (no raw samples)"
  fail=1
fi
if [ "${summary_count}" -lt 1 ]; then
  echo "bench-smoke: FAIL (no summary samples)"
  fail=1
fi

echo "bench-smoke: manifest ok (raw=${raw_count} summary=${summary_count})"
echo "bench-smoke: NOTE timings are diagnostic only; not asserted on CI"

if [ "${fail}" -ne 0 ]; then
  exit 1
fi
echo "bench-smoke: PASS"
exit 0
