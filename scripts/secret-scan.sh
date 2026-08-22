#!/usr/bin/env bash
# Milestone 38: secret scan for CI + pre-commit.
#
# Scans every git-TRACKED file for secret-looking content and fails if any is
# found. This is the "secret scan" required validation for M38. It is a
# heuristic (not a substitute for a dedicated scanner) tuned to this repo:
#   - high-signal patterns: AWS access key ids, private-key blocks, generic
#     `*_KEY=`/`*_TOKEN=`/`*_SECRET=` assignments with real-looking values,
#     Bearer tokens with real-looking values.
#   - allowlist: `.env.example` (documented placeholders), the Phase-2 local
#     docker-compose defaults (intentionally committed NON-secret local dev
#     credentials bound to 127.0.0.1), and test fixtures that use obvious
#     placeholder values.
#
# Usage: scripts/secret-scan.sh   (exit 0 clean, 1 on findings)

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "${REPO_ROOT}"

# Files intentionally containing only documented placeholder/local-test values.
# Keep this list narrow: the scanner skips the entire matching file.
ALLOWLIST_REGEX='(\.env\.example$|infra/phase2/docker-compose\.yml$|docs/phase2/LOCAL_INFRA\.md$|scripts/secret-scan\.sh$|\.github/workflows/ci\.yml$|engine/pg/include/evo/pg_run_store\.hpp$|engine/tests/auth_integration_test\.cpp$|engine/tests/auth_token_test\.cpp$|worker/src/logger\.test\.ts$)'

findings=0

# Pattern set. Each entry: <grep -E pattern>|<human label>.
# We grep tracked files (text only) and filter the allowlist.
scan_pattern() {
  local pattern="$1"
  local label="$2"
  # -I skips binaries; -n line numbers; -E extended regex; -i case-insensitive
  # (secret key names appear as apiKey/API_KEY/api_key/etc.).
  while IFS= read -r hit; do
    local file="${hit%%:*}"
    if [[ "${file}" =~ ${ALLOWLIST_REGEX} ]]; then
      continue
    fi
    echo "SECRET-SCAN FINDING [${label}]: ${hit}"
    findings=$((findings + 1))
  done < <(git grep -InEi "${pattern}" -- . 2>/dev/null || true)
}

echo "==> secret-scan: scanning tracked files"

# 1. AWS access key ids (AKIA/ASIA + 16 uppercase alnum).
scan_pattern '(AKIA|ASIA)[0-9A-Z]{16}' "aws-access-key-id"

# 2. Private key blocks.
scan_pattern 'BEGIN (RSA|EC|OPENSSH|DSA|PGP) PRIVATE KEY' "private-key-block"

# 3. Generic secret assignments with a real-looking value (>= 16 chars, not a
#    placeholder). Placeholders like `...`, `xxx`, `your-...`, `re_...`,
#    `<...>`, `${...}` are excluded.
scan_pattern '(api[_-]?key|apikey|secret|token|password|credential|authorization)[A-Za-z0-9_]*["'"'"']?[[:space:]]*[:=][[:space:]]*["'"'"'][A-Za-z0-9+/=._-]{16,}["'"'"']' "secret-assignment"

# 4. Bearer tokens with a real-looking value.
scan_pattern '[Bb]earer[[:space:]]+[A-Za-z0-9._~+/=-]{24,}' "bearer-token"

# 5. Slack / GitHub / Stripe style tokens.
scan_pattern '(xox[baprs]-[0-9A-Za-z-]{10,}|ghp_[A-Za-z0-9]{36}|sk_live_[A-Za-z0-9]{24,}|sk-[A-Za-z0-9]{32,})' "well-known-token-format"

if [ "${findings}" -gt 0 ]; then
  echo ""
  echo "secret-scan: ${findings} potential secret(s) found. Review each finding."
  echo "If a line is a documented placeholder or local-dev default, add it to the"
  echo "allowlist in scripts/secret-scan.sh with a comment explaining why."
  exit 1
fi

echo "secret-scan: clean (no secrets detected in tracked files)"
exit 0
