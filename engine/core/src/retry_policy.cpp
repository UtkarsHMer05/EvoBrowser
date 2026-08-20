#include "evo/retry_policy.hpp"

#include <algorithm>
#include <cmath>

namespace evo {

namespace {

// Deterministic xorshift64* PRNG for bounded backoff jitter. Self-contained
// (not the benchmark Rng) so production retry jitter does not depend on the
// benchmark module. Same seed => same sequence => reproducible tests.
class JitterRng {
 public:
  explicit JitterRng(std::uint64_t seed)
      : state_(seed == 0 ? 0x9E3779B97F4A7C15ULL : seed) {}
  std::uint64_t next() {
    std::uint64_t x = state_;
    x ^= x >> 12;
    x ^= x << 25;
    x ^= x >> 27;
    state_ = x;
    return x * 0x2545F4914F6CDD1DULL;
  }
  // Uniform double in [0, 1).
  double next_double() {
    return static_cast<double>(next() >> 11) / 9007199254740992.0;  // 2^53
  }

 private:
  std::uint64_t state_;
};

}  // namespace

const char* to_string(ErrorCategory c) {
  switch (c) {
    case ErrorCategory::Transient: return "transient";
    case ErrorCategory::ResourceLost: return "resource_lost";
    case ErrorCategory::Permanent: return "permanent";
    case ErrorCategory::Validation: return "validation";
    case ErrorCategory::Authorization: return "authorization";
    case ErrorCategory::Canceled: return "canceled";
    case ErrorCategory::Unknown: return "unknown";
  }
  return "unknown";
}

ErrorCategory classify_error(execution::v1::ErrorClass ec) {
  switch (ec) {
    case execution::v1::ERROR_TRANSIENT: return ErrorCategory::Transient;
    case execution::v1::ERROR_RESOURCE_EXHAUSTED:
      return ErrorCategory::ResourceLost;
    case execution::v1::ERROR_PERMANENT: return ErrorCategory::Permanent;
    case execution::v1::ERROR_VALIDATION: return ErrorCategory::Validation;
    case execution::v1::ERROR_AUTHORIZATION:
      return ErrorCategory::Authorization;
    case execution::v1::ERROR_CANCELED: return ErrorCategory::Canceled;
    case execution::v1::ERROR_CLASS_UNSPECIFIED:
    default:
      return ErrorCategory::Unknown;
  }
}

bool is_retryable_category(ErrorCategory c) {
  return c == ErrorCategory::Transient || c == ErrorCategory::ResourceLost;
}

const RetryPolicy& RetryPolicySet::for_class(ResourceClass rc) const {
  switch (rc) {
    case ResourceClass::Browser: return browser;
    case ResourceClass::ExternalIo: return external_io;
    case ResourceClass::Internal:
    default:
      return internal;
  }
}

RetryPolicySet RetryPolicySet::defaults() {
  RetryPolicySet set;
  // Internal: 3 attempts, 100ms base, x2, cap 10s, +/-25% jitter.
  set.internal.max_attempts = 3;
  set.internal.base_backoff = std::chrono::milliseconds(100);
  set.internal.max_backoff = std::chrono::milliseconds(10000);
  set.internal.multiplier = 2.0;
  set.internal.jitter_fraction = 0.25;
  // Browser: conservative — 2 attempts, 500ms base (sessions are expensive).
  set.browser.max_attempts = 2;
  set.browser.base_backoff = std::chrono::milliseconds(500);
  set.browser.max_backoff = std::chrono::milliseconds(10000);
  set.browser.multiplier = 2.0;
  set.browser.jitter_fraction = 0.25;
  // ExternalIo: NO retry by default (1 attempt). Side-effecting external calls
  // (email) require an idempotency strategy before retry is safe (M32 no-go).
  set.external_io.max_attempts = 1;
  set.external_io.base_backoff = std::chrono::milliseconds(1000);
  set.external_io.max_backoff = std::chrono::milliseconds(10000);
  set.external_io.multiplier = 2.0;
  set.external_io.jitter_fraction = 0.25;
  return set;
}

std::chrono::milliseconds compute_backoff(const RetryPolicy& policy,
                                          unsigned failed_attempt,
                                          std::uint64_t seed) {
  // base * multiplier^(attempt-1), capped at max_backoff.
  const unsigned exp = failed_attempt > 0 ? failed_attempt - 1 : 0;
  double delay = static_cast<double>(policy.base_backoff.count());
  for (unsigned i = 0; i < exp; ++i) {
    delay *= policy.multiplier;
    if (delay >= static_cast<double>(policy.max_backoff.count())) {
      delay = static_cast<double>(policy.max_backoff.count());
      break;
    }
  }
  delay = std::min(delay, static_cast<double>(policy.max_backoff.count()));

  // Bounded jitter: scale by a deterministic factor in [1-j, 1+j].
  double jitter = policy.jitter_fraction;
  if (jitter < 0.0) jitter = 0.0;
  if (jitter > 1.0) jitter = 1.0;
  if (jitter > 0.0) {
    JitterRng rng(seed);
    const double factor = (1.0 - jitter) + rng.next_double() * (2.0 * jitter);
    delay *= factor;
  }
  if (delay < 0.0) delay = 0.0;
  return std::chrono::milliseconds(static_cast<std::int64_t>(delay));
}

RetryDecision decide_retry(const RetryPolicy& policy, unsigned failed_attempt,
                           execution::v1::ErrorClass ec,
                           bool has_retryable_hint, bool retryable_hint,
                           std::uint64_t jitter_seed) {
  RetryDecision d;
  d.category = classify_error(ec);

  // Policy floor: permanent / validation / authorization / canceled are NEVER
  // retried, even if the worker said retryable (M32 no-go).
  const bool never_retry = d.category == ErrorCategory::Permanent ||
                           d.category == ErrorCategory::Validation ||
                           d.category == ErrorCategory::Authorization ||
                           d.category == ErrorCategory::Canceled;
  if (never_retry) {
    d.fail = true;
    d.reason = std::string("not retryable (") + to_string(d.category) + ")";
    return d;
  }

  // Retryable by default? Transient/resource-lost yes; unknown only on an
  // explicit worker hint.
  bool retryable = is_retryable_category(d.category);
  if (d.category == ErrorCategory::Unknown) {
    retryable = has_retryable_hint && retryable_hint;
  } else if (has_retryable_hint && !retryable_hint) {
    // The worker explicitly said NOT retryable for a default-retryable class.
    retryable = false;
  }

  if (!retryable) {
    d.fail = true;
    d.reason = std::string("not retryable (") + to_string(d.category) + ")";
    return d;
  }

  // Retryable: is there attempt budget left?
  if (failed_attempt >= policy.max_attempts) {
    d.dead_letter = true;
    d.reason = "retries exhausted (" + std::to_string(failed_attempt) + "/" +
               std::to_string(policy.max_attempts) + " attempts)";
    return d;
  }

  d.retry = true;
  d.backoff = compute_backoff(policy, failed_attempt, jitter_seed);
  d.reason = "retry after " + std::to_string(d.backoff.count()) + "ms (" +
             to_string(d.category) + ")";
  return d;
}

}  // namespace evo
