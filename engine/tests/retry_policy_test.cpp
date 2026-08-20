// M32 unit tests for the node-level retry policy (evo/retry_policy.hpp):
// error taxonomy, retryability policy floor, attempt budget + dead-lettering,
// and deterministic exponential backoff with bounded jitter.

#include <chrono>
#include <cstdio>
#include <set>

#include "evo/retry_policy.hpp"

using evo::compute_backoff;
using evo::decide_retry;
using evo::ErrorCategory;
using evo::RetryPolicy;
using evo::RetryPolicySet;
using evo::ResourceClass;
using namespace std::chrono_literals;

namespace evo_ec = evo::execution::v1;

namespace {
int failures = 0;
void check(bool cond, const char* label) {
  if (cond) {
    printf("  ok   %s\n", label);
  } else {
    printf("  FAIL %s\n", label);
    ++failures;
  }
}

RetryPolicy test_policy() {
  RetryPolicy p;
  p.max_attempts = 3;
  p.base_backoff = 100ms;
  p.max_backoff = 10000ms;
  p.multiplier = 2.0;
  p.jitter_fraction = 0.0;  // no jitter for exact-value assertions
  return p;
}
}  // namespace

int main() {
  // --- 1. Error taxonomy classification ------------------------------------
  check(evo::classify_error(evo_ec::ERROR_TRANSIENT) == ErrorCategory::Transient,
        "transient -> Transient");
  check(evo::classify_error(evo_ec::ERROR_RESOURCE_EXHAUSTED) ==
            ErrorCategory::ResourceLost,
        "resource_exhausted -> ResourceLost");
  check(evo::classify_error(evo_ec::ERROR_PERMANENT) == ErrorCategory::Permanent,
        "permanent -> Permanent");
  check(evo::classify_error(evo_ec::ERROR_VALIDATION) == ErrorCategory::Validation,
        "validation -> Validation");
  check(evo::classify_error(evo_ec::ERROR_AUTHORIZATION) ==
            ErrorCategory::Authorization,
        "authorization -> Authorization");
  check(evo::classify_error(evo_ec::ERROR_CANCELED) == ErrorCategory::Canceled,
        "canceled -> Canceled");
  check(evo::classify_error(evo_ec::ERROR_CLASS_UNSPECIFIED) ==
            ErrorCategory::Unknown,
        "unspecified -> Unknown");

  // --- 2. Retryable-by-default categories ----------------------------------
  check(evo::is_retryable_category(ErrorCategory::Transient),
        "Transient retryable by default");
  check(evo::is_retryable_category(ErrorCategory::ResourceLost),
        "ResourceLost retryable by default");
  check(!evo::is_retryable_category(ErrorCategory::Permanent),
        "Permanent not retryable by default");
  check(!evo::is_retryable_category(ErrorCategory::Validation),
        "Validation not retryable by default");
  check(!evo::is_retryable_category(ErrorCategory::Authorization),
        "Authorization not retryable by default");
  check(!evo::is_retryable_category(ErrorCategory::Canceled),
        "Canceled not retryable by default");
  check(!evo::is_retryable_category(ErrorCategory::Unknown),
        "Unknown not retryable by default");

  // --- 3. Policy floor: never-retry classes fail even with retryable hint ---
  {
    const RetryPolicy p = test_policy();
    // Permanent with an explicit retryable=true hint must still fail.
    auto d = decide_retry(p, 1, evo_ec::ERROR_PERMANENT, true, true, 1);
    check(d.fail && !d.retry && !d.dead_letter,
          "permanent + retryable hint -> fail (policy floor)");
    // Validation with retryable hint -> fail (M32 no-go).
    d = decide_retry(p, 1, evo_ec::ERROR_VALIDATION, true, true, 1);
    check(d.fail && !d.retry, "validation + retryable hint -> fail");
    // Authorization with retryable hint -> fail (M32 no-go).
    d = decide_retry(p, 1, evo_ec::ERROR_AUTHORIZATION, true, true, 1);
    check(d.fail && !d.retry, "authorization + retryable hint -> fail");
    // Canceled -> fail (not a failure to retry).
    d = decide_retry(p, 1, evo_ec::ERROR_CANCELED, false, false, 1);
    check(d.fail && !d.retry, "canceled -> fail");
  }

  // --- 4. Transient retry with attempt budget ------------------------------
  {
    const RetryPolicy p = test_policy();  // max_attempts = 3
    // Attempt 1 fails transiently -> retry (budget: 1 < 3).
    auto d = decide_retry(p, 1, evo_ec::ERROR_TRANSIENT, false, false, 1);
    check(d.retry && !d.fail && !d.dead_letter, "transient attempt 1 -> retry");
    check(d.backoff == 100ms, "attempt 1 backoff = base (100ms, no jitter)");
    // Attempt 2 fails transiently -> retry (budget: 2 < 3).
    d = decide_retry(p, 2, evo_ec::ERROR_TRANSIENT, false, false, 1);
    check(d.retry, "transient attempt 2 -> retry");
    check(d.backoff == 200ms, "attempt 2 backoff = base*2 (200ms)");
    // Attempt 3 fails transiently -> dead-letter (budget exhausted: 3 >= 3).
    d = decide_retry(p, 3, evo_ec::ERROR_TRANSIENT, false, false, 1);
    check(d.dead_letter && !d.retry && !d.fail,
          "transient attempt 3 (== max) -> dead_letter");
  }

  // --- 5. Worker explicit not-retryable overrides default-retryable --------
  {
    const RetryPolicy p = test_policy();
    auto d = decide_retry(p, 1, evo_ec::ERROR_TRANSIENT, true, false, 1);
    check(d.fail && !d.retry,
          "transient + explicit retryable=false -> fail (worker override)");
  }

  // --- 6. Unknown class: retry only on explicit positive hint --------------
  {
    const RetryPolicy p = test_policy();
    // No hint -> fail.
    auto d = decide_retry(p, 1, evo_ec::ERROR_CLASS_UNSPECIFIED, false, false, 1);
    check(d.fail && !d.retry, "unknown, no hint -> fail");
    // Explicit retryable=false -> fail.
    d = decide_retry(p, 1, evo_ec::ERROR_CLASS_UNSPECIFIED, true, false, 1);
    check(d.fail && !d.retry, "unknown, hint=false -> fail");
    // Explicit retryable=true -> retry.
    d = decide_retry(p, 1, evo_ec::ERROR_CLASS_UNSPECIFIED, true, true, 1);
    check(d.retry && !d.fail, "unknown, hint=true -> retry");
  }

  // --- 7. max_attempts == 1 => never retry (dead-letter on first failure) --
  {
    RetryPolicy p = test_policy();
    p.max_attempts = 1;
    auto d = decide_retry(p, 1, evo_ec::ERROR_TRANSIENT, false, false, 1);
    check(d.dead_letter && !d.retry,
          "max_attempts=1: first transient failure -> dead_letter");
  }

  // --- 8. Exponential backoff, capped, deterministic -----------------------
  {
    RetryPolicy p = test_policy();  // base 100, x2, cap 10000, no jitter
    check(compute_backoff(p, 1, 7) == 100ms, "backoff attempt 1 = 100ms");
    check(compute_backoff(p, 2, 7) == 200ms, "backoff attempt 2 = 200ms");
    check(compute_backoff(p, 3, 7) == 400ms, "backoff attempt 3 = 400ms");
    check(compute_backoff(p, 4, 7) == 800ms, "backoff attempt 4 = 800ms");
    // Cap: attempt 8 would be 100*2^7 = 12800 > 10000 -> capped.
    check(compute_backoff(p, 8, 7) == 10000ms, "backoff capped at max (10000ms)");
    // Deterministic: same (policy, attempt, seed) => same delay.
    check(compute_backoff(p, 3, 42) == compute_backoff(p, 3, 42),
          "backoff deterministic for same seed");
  }

  // --- 9. Bounded jitter stays within [1-j, 1+j] and varies by seed --------
  {
    RetryPolicy p = test_policy();
    p.jitter_fraction = 0.25;  // +/-25%
    std::set<long long> seen;
    bool in_bounds = true;
    for (std::uint64_t seed = 1; seed <= 50; ++seed) {
      const auto d = compute_backoff(p, 3, seed);
      seen.insert(d.count());
      // 400 * 0.75 = 300 <= d <= 400 * 1.25 = 500
      if (d.count() < 300 || d.count() > 500) in_bounds = false;
    }
    check(in_bounds, "jittered backoff stays within [300,500]ms (+/-25%)");
    check(seen.size() > 1, "jitter varies across seeds (not constant)");
    // Same seed reproduces the same jittered value.
    check(compute_backoff(p, 3, 99) == compute_backoff(p, 3, 99),
          "jittered backoff deterministic for same seed");
  }

  // --- 10. Default policy set by resource class ----------------------------
  {
    const RetryPolicySet set = RetryPolicySet::defaults();
    check(set.for_class(ResourceClass::Internal).max_attempts == 3,
          "internal default: 3 attempts");
    check(set.for_class(ResourceClass::Browser).max_attempts == 2,
          "browser default: 2 attempts (conservative)");
    check(set.for_class(ResourceClass::ExternalIo).max_attempts == 1,
          "external_io default: 1 attempt (no retry; side effects)");
    // external_io: a transient failure on attempt 1 dead-letters immediately.
    auto d = decide_retry(set.for_class(ResourceClass::ExternalIo), 1,
                          evo_ec::ERROR_TRANSIENT, false, false, 1);
    check(d.dead_letter && !d.retry,
          "external_io transient failure -> dead_letter (no retry by default)");
  }

  if (failures == 0) {
    printf("\nALL M32 RETRY POLICY TESTS PASSED!\n");
    return 0;
  }
  printf("\n%d FAILURE(S)\n", failures);
  return 1;
}
