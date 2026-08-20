#pragma once

// Node-level retry policy, exponential backoff with bounded jitter, and
// dead-lettering (Milestone 32).
//
// This module is the single place that decides, for a FAILED attempt, whether
// the logical node should be retried, how long to wait before the next attempt,
// or whether the retries are exhausted and the node must be dead-lettered. It
// is pure policy: it owns no threads, no timers, and no durable state. The
// distributed run loop (M26) consumes a RetryDecision and performs the actual
// state transition + persistence; the backoff wait is realized as a RETRY_WAIT
// state with a due-time (never by blocking a scheduler/worker thread — M32
// step 6).
//
// Error taxonomy (M32 step 1). Every failed attempt carries an ErrorClass
// (execution.proto). We classify it into a coarse category that drives
// retryability:
//
//   transient        network blip / 5xx / timeout      -> retryable
//   resource_lost    quota / rate limit (exhausted)    -> retryable (backoff)
//   permanent        bad input / 4xx / logic error     -> NOT retryable
//   validation       input/schema validation failed    -> NOT retryable
//   authorization    auth/permission denied            -> NOT retryable
//   canceled         the attempt was canceled          -> NOT retryable
//   unknown          class unspecified                 -> retryable only on an
//                                                         explicit worker hint
//
// The worker's `retryable` flag is advisory (M22): the policy floor is that
// permanent/validation/authorization/canceled errors are NEVER retried even if
// the worker said retryable (M32 no-go: do not retry authorization/validation
// errors blindly). Transient/resource-lost default to retryable unless the
// worker explicitly said not. Unknown is retryable only when the worker
// explicitly said so.
//
// Backoff (M32 step 4): exponential with BOUNDED jitter. The delay before the
// retry after a failed attempt N (1-based) is
//     base * multiplier^(N-1), capped at max_backoff,
// then multiplied by a deterministic factor in [1-jitter, 1+jitter] drawn from
// a seeded xorshift64* PRNG. Passing the same seed reproduces the same delay,
// which is what makes the tests deterministic.
//
// Timestamps: this module computes DURATIONS (std::chrono::milliseconds) only;
// it never emits wall-clock or steady-clock instants. The run loop owns clocks.

#include <chrono>
#include <cstdint>
#include <string>

#include "evo/execution.pb.h"
#include "evo/execution_policy.hpp"

namespace evo {

// Coarse error category driving retryability (M32 step 1).
enum class ErrorCategory {
  Transient,       // retry likely helps
  ResourceLost,    // quota/rate limit; retry after backoff
  Permanent,       // retry won't help
  Validation,      // retry won't help (never retried blindly)
  Authorization,   // retry won't help (never retried blindly)
  Canceled,        // not a failure to retry
  Unknown,         // unspecified; retry only on explicit worker hint
};

const char* to_string(ErrorCategory c);

// Map a proto ErrorClass to the coarse category.
ErrorCategory classify_error(execution::v1::ErrorClass ec);

// True when the category is retryable by default (transient / resource-lost).
// Unknown/permanent/validation/authorization/canceled are not retryable by
// default; Unknown may still be retried on an explicit worker hint (see
// decide_retry).
bool is_retryable_category(ErrorCategory c);

// Retry policy for one resource class (M32 step 2).
struct RetryPolicy {
  // Total attempts allowed for a retryable failure, including the first. After
  // the max_attempts-th attempt fails, the node is dead-lettered (no further
  // attempt). max_attempts == 1 => never retry.
  unsigned max_attempts = 3;

  // Backoff before the retry that follows the N-th failed attempt:
  //   base_backoff * multiplier^(N-1), capped at max_backoff, +/- jitter.
  std::chrono::milliseconds base_backoff{100};
  std::chrono::milliseconds max_backoff{10000};
  double multiplier = 2.0;

  // Bounded jitter fraction: the computed delay is scaled by a deterministic
  // factor in [1 - jitter_fraction, 1 + jitter_fraction]. 0 => no jitter.
  double jitter_fraction = 0.25;
};

// Default retry policies by resource class (M32 step 2). These do NOT change
// legacy Trigger.dev behavior (the legacy engine has its own retry semantics);
// they apply only to the Evo distributed engine.
struct RetryPolicySet {
  RetryPolicy internal;     // start / synthetic / CPU-bound
  RetryPolicy browser;      // browser-session nodes (conservative)
  RetryPolicy external_io;  // side-effecting external calls (email)

  // Resolve the policy for a resource class.
  const RetryPolicy& for_class(ResourceClass rc) const;

  // Product defaults:
  //   internal    -> 3 attempts, 100ms base
  //   browser     -> 2 attempts, 500ms base (browser sessions are expensive)
  //   external_io -> 1 attempt  (NO retry by default: side effects require an
  //                  idempotency strategy before retry is safe — M32 no-go.
  //                  Enabling email retry is a deliberate later change, M33.)
  static RetryPolicySet defaults();
};

// The outcome of the retry decision for one failed attempt.
struct RetryDecision {
  // Retry the node after `backoff` (RETRY_WAIT). Mutually exclusive with the
  // terminal outcomes below.
  bool retry = false;
  std::chrono::milliseconds backoff{0};

  // Terminal: the failure is not retryable (permanent/validation/authorization/
  // canceled, or unknown without a hint). The node should be FAILED.
  bool fail = false;

  // Terminal: the failure was retryable but the attempt budget is exhausted.
  // The node should be DEAD_LETTERED.
  bool dead_letter = false;

  // Human-readable reason (persisted as the retry/dead-letter reason).
  std::string reason;

  // The category that drove the decision (diagnostics/events).
  ErrorCategory category = ErrorCategory::Unknown;
};

// Decide what to do after attempt `failed_attempt` (1-based) of a node failed
// with the given error class + worker retryable hint, under `policy`.
//
//   - Not retryable (policy floor)            -> fail
//   - Retryable but failed_attempt >= max     -> dead_letter
//   - Retryable with attempts remaining       -> retry (backoff via jitter_seed)
//
// `jitter_seed` seeds the deterministic PRNG for the backoff jitter so tests
// are reproducible. `has_retryable_hint`/`retryable_hint` carry the worker's
// advisory flag (proto `optional bool retryable`).
RetryDecision decide_retry(const RetryPolicy& policy, unsigned failed_attempt,
                           execution::v1::ErrorClass ec,
                           bool has_retryable_hint, bool retryable_hint,
                           std::uint64_t jitter_seed);

// Deterministic exponential backoff with bounded jitter for the retry that
// follows failed attempt `failed_attempt` (1-based). Same (policy, attempt,
// seed) => same delay. Exposed for direct testing.
std::chrono::milliseconds compute_backoff(const RetryPolicy& policy,
                                          unsigned failed_attempt,
                                          std::uint64_t seed);

}  // namespace evo
