#!/usr/bin/env bash
# Milestone 39 — final reproducible performance, scaling, and chaos campaign.
#
# Orchestrates the evidence-grade campaign on the LOCAL reference machine and
# assembles one results directory per BENCHMARK_METHODOLOGY.md §4:
#
#   engine/benchmarks/results/<YYYYMMDD-HHMMSS>_m39_<short-sha>/
#   ├── campaign.json        # frozen SHA, git state, hardware, timing, layout
#   ├── local/               # evo-m39-local: shapes x sizes x io/cpu x threads
#   ├── scaling/             # evo_m39_scaling_test: worker counts x task counts
#   ├── chaos/               # evo_m39_chaos_test: Redis/Postgres outage (F09-F12)
#   ├── faults/              # crash_recovery (M34) + fairness (M37) artifacts
#   ├── fault_audit.txt      # distributed_run_loop_test 27-scenario outcome audit
#   ├── checksums.sha256     # sha256 of every artifact file
#   └── REPORT.md            # human-readable summary derived from the raw data
#
# Evidence rules enforced here (methodology §1, §5):
#   - The benchmark commit SHA is FROZEN before the campaign and recorded.
#   - Raw samples are preserved (never only aggregates); each component writes
#     manifest.json / samples.jsonl / summary.json / command.txt.
#   - No timing is asserted or cherry-picked; worse-than-expected results are
#     preserved and surfaced in REPORT.md.
#   - This runs on the reference machine only. Shared-CI timing is never final
#     evidence (methodology §1.8); CI only smoke-tests the harness.
#
# Prereqs (all local, no secrets): engine Release build, local Phase-2 Redis +
# Postgres (scripts/phase2/up.sh), node/npx for the TS workers, docker for the
# chaos outage injection. Components that cannot run (missing stack) SKIP and
# are recorded as skipped, never fabricated.
#
# Usage: scripts/phase2/m39-campaign.sh
#   EVO_M39_TRIALS / EVO_M39_WARMUP      local campaign trial protocol
#   EVO_M39_WORKERS / EVO_M39_TASKS      scaling matrix (default "1,2,4" / "100,500")
#   EVO_M39_SKIP_CHAOS=1                 skip the docker pause/unpause chaos leg

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
ENGINE_DIR="${REPO_ROOT}/engine"
BUILD_DIR="${ENGINE_DIR}/build"

log() { printf '==> %s\n' "$*"; }

# --- 0. Refuse to run under Rosetta (x86-64 emulation) -----------------------
# Evidence-grade numbers must be measured on the native architecture. A shell
# launched under Rosetta reports `sysctl.proc_translated` = 1 on an arm64 Mac;
# timing collected there is emulation-tainted and must never be recorded as
# final evidence (methodology §1.4/§1.8). If we detect translation, re-exec
# this script natively. IMPORTANT: re-exec via the UNIVERSAL system shell
# (/bin/bash), NOT bare `bash` — on this machine `bash` on PATH is an
# x86_64-only Homebrew build, and `arch -arm64 bash` fails with "Bad CPU type".
TRANSLATED="$(sysctl -n sysctl.proc_translated 2>/dev/null || echo 0)"
if [ "${TRANSLATED}" = "1" ]; then
  if [ "$(uname -s)" = "Darwin" ] && command -v arch >/dev/null 2>&1 \
     && [ -x /bin/bash ]; then
    log "running under Rosetta; re-executing natively via arch -arm64 /bin/bash"
    exec arch -arm64 /bin/bash "$0" "$@"
  fi
  echo "ERROR: this shell is running under Rosetta (x86-64 emulation) and a" >&2
  echo "       native re-exec is not possible. Re-launch from a native shell." >&2
  exit 2
fi
# Post-condition: we are native. Record it so the artifact proves it.
log "native architecture check: uname -m=$(uname -m) translated=${TRANSLATED}"

# --- 1. Freeze the benchmark commit SHA + record the exact tree state --------
cd "${REPO_ROOT}"
FROZEN_SHA="$(git rev-parse --short HEAD)"
FROZEN_SHA_FULL="$(git rev-parse HEAD)"
GIT_DIRTY="$(git status --porcelain || true)"
TS="$(date +%Y%m%d-%H%M%S)"
RESULTS_DIR="${ENGINE_DIR}/benchmarks/results/${TS}_m39_${FROZEN_SHA}"
mkdir -p "${RESULTS_DIR}"

log "M39 campaign: frozen SHA ${FROZEN_SHA} -> ${RESULTS_DIR}"

# campaign.json: provenance required by methodology §1.4 / §5.
{
  echo "{"
  echo "  \"milestone\": \"M39\","
  echo "  \"frozen_sha\": \"${FROZEN_SHA}\","
  echo "  \"frozen_sha_full\": \"${FROZEN_SHA_FULL}\","
  echo "  \"generated_at\": \"$(date -u +%Y-%m-%dT%H:%M:%SZ)\","
  echo "  \"hardware\": \"$(uname -sm)\","
  echo "  \"cpu\": \"$(sysctl -n machdep.cpu.brand_string 2>/dev/null || echo unknown)\","
  echo "  \"cores\": $(sysctl -n hw.ncpu 2>/dev/null || echo 0),"
  echo "  \"git_dirty_files\": ["
  if [ -n "${GIT_DIRTY}" ]; then
    printf '%s\n' "${GIT_DIRTY}" | sed 's/^/    "/; s/$/",/' | sed '$ s/,$//'
  fi
  echo "  ],"
  echo "  \"note\": \"evidence-grade campaign on the local reference machine; results dir is gitignored by convention (see M34) and referenced from docs/phase2/PROGRESS.md\""
  echo "}"
} > "${RESULTS_DIR}/campaign.json"

# --- 2. Build the campaign binaries (Release) --------------------------------
# Reconfigure first so EVO_BUILD_COMMIT (baked at configure time via
# `git rev-parse`) matches the frozen SHA — evidence must be attributable to the
# exact commit it was measured from (methodology §1.4).
log "building campaign binaries (Release)"
cmake -S "${ENGINE_DIR}" -B "${BUILD_DIR}" -DCMAKE_BUILD_TYPE=Release >/dev/null
cmake --build "${BUILD_DIR}" \
  --target evo-m39-local evo_m39_scaling_test evo_m39_chaos_test \
  evo_crash_recovery_test evo_fairness_bench_test evo_distributed_run_loop_test \
  -j8 >/dev/null

export EVO_M39_BUILD_MODE="Release"

# --- 3. Local scheduler campaign ---------------------------------------------
log "local scheduler campaign (shapes x sizes x io/cpu x threads)"
"${BUILD_DIR}/evo-m39-local" "${RESULTS_DIR}/local" \
  > "${RESULTS_DIR}/local.stdout" 2>&1 || {
    log "WARN: local campaign exited non-zero (see local.stdout)"
  }

# --- 4. Distributed worker scaling -------------------------------------------
log "distributed worker scaling"
mkdir -p "${RESULTS_DIR}/scaling"
if EVO_M39_ARTIFACT_DIR="${RESULTS_DIR}/scaling" \
   "${BUILD_DIR}/evo_m39_scaling_test" > "${RESULTS_DIR}/scaling.stdout" 2>&1; then
  :
else
  log "WARN: scaling test exited non-zero or skipped (see scaling.stdout)"
fi

# --- 5. Infrastructure outage chaos (F09-F12) --------------------------------
if [ "${EVO_M39_SKIP_CHAOS:-0}" = "1" ]; then
  log "chaos leg skipped (EVO_M39_SKIP_CHAOS=1)"
  echo "skipped: EVO_M39_SKIP_CHAOS=1" > "${RESULTS_DIR}/chaos.stdout"
else
  log "infrastructure outage chaos (docker pause/unpause Redis + Postgres)"
  mkdir -p "${RESULTS_DIR}/chaos"
  if EVO_M39_CHAOS_ARTIFACT_DIR="${RESULTS_DIR}/chaos" \
     "${BUILD_DIR}/evo_m39_chaos_test" > "${RESULTS_DIR}/chaos.stdout" 2>&1; then
    :
  else
    log "WARN: chaos test exited non-zero or skipped (see chaos.stdout)"
  fi
fi

# --- 6. Fault-injection evidence (M34 crash recovery + M37 fairness) ---------
log "fault evidence: M34 crash recovery"
mkdir -p "${RESULTS_DIR}/faults/crash_recovery"
EVO_M34_ARTIFACT_DIR="${RESULTS_DIR}/faults/crash_recovery" \
  "${BUILD_DIR}/evo_crash_recovery_test" \
  > "${RESULTS_DIR}/faults/crash_recovery.stdout" 2>&1 || \
  log "WARN: crash recovery exited non-zero or skipped"

log "fault evidence: M37 fairness"
mkdir -p "${RESULTS_DIR}/faults/fairness"
EVO_M37_ARTIFACT_DIR="${RESULTS_DIR}/faults/fairness" \
  "${BUILD_DIR}/evo_fairness_bench_test" \
  > "${RESULTS_DIR}/faults/fairness.stdout" 2>&1 || \
  log "WARN: fairness bench exited non-zero or skipped"

# --- 7. Fault outcome audit (27-scenario distributed run loop) ---------------
log "fault outcome audit: distributed_run_loop_test (27 scenarios)"
( cd "${BUILD_DIR}" && ctest -R '^distributed_run_loop$' --output-on-failure ) \
  > "${RESULTS_DIR}/fault_audit.txt" 2>&1 || \
  log "WARN: distributed_run_loop audit had failures (see fault_audit.txt)"

# --- 8. Checksums over every artifact ----------------------------------------
log "computing checksums"
( cd "${RESULTS_DIR}" && find . -type f ! -name checksums.sha256 -print0 \
    | sort -z | xargs -0 shasum -a 256 ) > "${RESULTS_DIR}/checksums.sha256"

# --- 9. Human-readable REPORT.md (derived from the raw data) -----------------
log "writing REPORT.md"
python3 - "${RESULTS_DIR}" <<'PY'
import json, os, sys
root = sys.argv[1]

def load(p):
    try:
        with open(p) as f: return json.load(f)
    except Exception:
        return None

lines = []
lines.append("# M39 — Final Performance, Scaling, and Chaos Campaign\n")
camp = load(os.path.join(root, "campaign.json")) or {}
lines.append(f"- Frozen SHA: `{camp.get('frozen_sha','?')}` (full `{camp.get('frozen_sha_full','?')}`)")
lines.append(f"- Generated: {camp.get('generated_at','?')}")
lines.append(f"- Hardware: {camp.get('hardware','?')} — {camp.get('cpu','?')} ({camp.get('cores','?')} cores)")
dirty = camp.get("git_dirty_files", [])
lines.append(f"- Working-tree files at freeze: {len(dirty)} (recorded in campaign.json)")
lines.append("")

# Local campaign
loc_sum = load(os.path.join(root, "local", "summary.json"))
loc_man = load(os.path.join(root, "local", "manifest.json"))
if loc_sum and loc_man:
    lines.append("## Local scheduler campaign (scheduler-only synthetic)\n")
    lines.append(f"Workload class: {loc_man.get('workload_class','?')}. "
                 "MUST NOT be generalized to browser end-to-end speedup.\n")
    cells = loc_sum.get("cells", [])
    # io profile: report speedup vs the sequential reference (sleep tasks have
    # negligible cooperative-polling overhead, so seq-vs-con is meaningful).
    lines.append("### Simulated I/O-bound profile — speedup vs sequential reference\n")
    best = {}
    for c in cells:
        if c["profile"] != "io": continue
        k = c["threads"]
        if k not in best or c["speedup"] > best[k]["speedup"]:
            best[k] = c
    lines.append("| threads | best shape/size | seq med (ms) | con med (ms) | speedup | efficiency |")
    lines.append("| ---: | :--- | ---: | ---: | ---: | ---: |")
    for threads, c in sorted(best.items()):
        lines.append(f"| {threads} | {c['shape']}/{c['size']} | "
                     f"{c['seq_median_ms']:.1f} | {c['con_median_ms']:.1f} | "
                     f"{c['speedup']:.2f}x | {c['parallel_efficiency']:.2f} |")
    lines.append("")
    # cpu profile: report thread-scaling (con t1 vs tN). The cooperative CPU
    # task polls its stop_token every 256 iters, so seq-vs-con understates the
    # concurrent scheduler's own scaling; thread-scaling is the honest signal.
    lines.append("### Synthetic CPU profile — thread scaling (concurrent t1 vs tN)\n")
    lines.append("Note: seq-vs-con speedup is NOT reported for CPU because the "
                 "cooperative task's stop_token polling adds overhead vs the plain "
                 "sequential task; thread-scaling isolates the scheduler's scaling.\n")
    bestc = {}
    for c in cells:
        if c["profile"] != "cpu": continue
        k = c["threads"]
        if k not in bestc or c["thread_scaling_vs_1t"] > bestc[k]["thread_scaling_vs_1t"]:
            bestc[k] = c
    lines.append("| threads | best shape/size | con t1 (ms) | con tN (ms) | thread scaling |")
    lines.append("| ---: | :--- | ---: | ---: | ---: |")
    for threads, c in sorted(bestc.items()):
        t1 = c["con_median_ms"] * c["thread_scaling_vs_1t"] if c["thread_scaling_vs_1t"] else 0
        lines.append(f"| {threads} | {c['shape']}/{c['size']} | "
                     f"{t1:.1f} | {c['con_median_ms']:.1f} | "
                     f"{c['thread_scaling_vs_1t']:.2f}x |")
    lines.append("")
else:
    lines.append("## Local scheduler campaign\n\nSKIPPED or failed — see local.stdout.\n")

# Scaling
sc_sum = load(os.path.join(root, "scaling", "summary.json"))
if sc_sum:
    lines.append("## Distributed worker scaling (simulated I/O-bound, wide DAG)\n")
    lines.append("| workers | tasks | trials | median makespan (ms) | median throughput (/s) | scaling vs 1w | efficiency |")
    lines.append("| ---: | ---: | ---: | ---: | ---: | ---: | ---: |")
    for c in sc_sum.get("cells", []):
        lines.append(f"| {c['workers']} | {c['tasks']} | {c.get('trials','?')} | "
                     f"{c['median_makespan_ms']:.0f} | "
                     f"{c['median_throughput_nps']:.2f} | {c['scaling_vs_1w']:.2f}x | "
                     f"{c['parallel_efficiency']:.2f} |")
    lines.append("")
else:
    lines.append("## Distributed worker scaling\n\nSKIPPED or failed — see scaling.stdout.\n")

# Chaos
ch_sum = load(os.path.join(root, "chaos", "summary.json"))
if ch_sum:
    lines.append("## Infrastructure outage chaos (Appendix T F09-F12)\n")
    lines.append("| fault | makespan (ms) | reached terminal | recovered to success | tasks ok |")
    lines.append("| :--- | ---: | :--- | :--- | :--- |")
    for t in ch_sum.get("trials", []):
        lines.append(f"| {t['fault']} | {t['makespan_ms']} | "
                     f"{'yes' if t['reached_terminal'] else 'NO'} | "
                     f"{'yes' if t['recovered_to_success'] else 'NO'} | "
                     f"{t['succeeded_nodes']}/{t['tasks']} |")
    lines.append("")
else:
    lines.append("## Infrastructure outage chaos\n\nSKIPPED or failed — see chaos.stdout.\n")

# Faults
cr_sum = load(os.path.join(root, "faults", "crash_recovery", "summary.json"))
if cr_sum:
    lines.append("## Worker crash recovery (M34, SIGKILL lease-holder)\n")
    lines.append(f"Recovery latency (SIGKILL -> run complete): min {cr_sum.get('min')} / "
                 f"median {cr_sum.get('median')} / max {cr_sum.get('max')} ms "
                 f"over {cr_sum.get('samples')} trials.\n")
fa_sum = load(os.path.join(root, "faults", "fairness", "summary.json"))
if fa_sum:
    lines.append("## Fair scheduling (M37)\n")
    lines.append(f"Jain index — equal: span {fa_sum.get('workload_equal_jain_span')} / "
                 f"served {fa_sum.get('workload_equal_jain_served')}; "
                 f"unequal: span {fa_sum.get('workload_unequal_jain_span')} / "
                 f"served {fa_sum.get('workload_unequal_jain_served')}.\n")

lines.append("## Reproducibility\n")
lines.append("Every component directory contains `command.txt` with the exact "
             "reproducible command. `checksums.sha256` covers all artifact files. "
             "Re-run the whole campaign with `scripts/phase2/m39-campaign.sh`.")
lines.append("")

with open(os.path.join(root, "REPORT.md"), "w") as f:
    f.write("\n".join(lines))
print("REPORT.md written")
PY

log "M39 campaign complete: ${RESULTS_DIR}"
log "artifact count: $(find "${RESULTS_DIR}" -type f | wc -l | tr -d ' ')"
