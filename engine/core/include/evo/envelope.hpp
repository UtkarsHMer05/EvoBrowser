#pragma once

// Task/result envelope semantics (Milestone 22).
//
// Makes distributed attempt semantics explicit before real workers execute
// product nodes. This module defines, in code, the rules the transport layer
// (M21) carries but does not enforce:
//
//   Identity
//     logical task id  = (run_id, node_id)
//     attempt id       = (run_id, node_id, attempt_number)
//   Transport message ids (Redis stream ids) are NOT identity; dedupe and
//   late-result rules key off the fields above.
//
//   Dedupe
//     Repeated result events with the same attempt id are duplicates: the
//     first is applied, later ones are ignored (ResultDedupe).
//
//   Late results
//     A result for an attempt older than the node's current attempt, or one
//     arriving after the node reached a terminal state, is IGNORED — a late
//     result can never overwrite newer logical state.
//
//   Validation & size limits
//     Envelopes crossing the process boundary are validated before mutating
//     durable state. Oversized or malformed payloads are rejected with an
//     explicit reason (quarantine/log at the call site), never applied.
//
// Timestamps inside envelopes are wall-clock UTC (proto Timestamp); this
// module adds no timestamps of its own.

#include <cstddef>
#include <mutex>
#include <set>
#include <string>
#include <tuple>
#include <vector>

#include "evo/execution.pb.h"

namespace evo {

// Hard bound on a serialized envelope crossing the transport. Keeps a single
// malformed/huge payload from bloating the stream or worker memory. 256 KiB
// comfortably bounds node payloads (which are small JSON) while leaving room
// for large extract outputs.
inline constexpr std::size_t kMaxEnvelopeBytes = 256 * 1024;

// Validate a TaskEnvelope before dispatch. Returns a list of problems; empty
// means valid. Checks identity fields, attempt numbering, and size.
std::vector<std::string> validate_task_envelope(
    const execution::v1::TaskEnvelope& env);

// Validate a ResultEnvelope before the scheduler applies it. Returns a list
// of problems; empty means valid.
std::vector<std::string> validate_result_envelope(
    const execution::v1::ResultEnvelope& env);

// Attempt identity used for dedupe and late-result rules.
struct AttemptKey {
  std::string run_id;
  std::string node_id;
  unsigned attempt_number;

  bool operator<(const AttemptKey& o) const {
    return std::tie(run_id, node_id, attempt_number) <
           std::tie(o.run_id, o.node_id, o.attempt_number);
  }
  bool operator==(const AttemptKey& o) const {
    return run_id == o.run_id && node_id == o.node_id &&
           attempt_number == o.attempt_number;
  }
};

AttemptKey attempt_key_of(const execution::v1::ResultEnvelope& env);

// Dedupe repeated result events by attempt id. Thread-safe. `first_time`
// returns true exactly once per attempt id; repeated events return false and
// must be ignored by the caller.
class ResultDedupe {
 public:
  bool first_time(const AttemptKey& key);
  std::size_t size() const;

 private:
  mutable std::mutex mu_;
  std::set<AttemptKey> seen_;
};

// Late-result rule. A result is "late" (must be ignored, never applied) when:
//   - its attempt_number is older than the node's current attempt, or
//   - the node already reached a terminal state.
// Returns true if the result should be IGNORED.
bool is_late_result(const execution::v1::ResultEnvelope& result,
                    unsigned current_attempt_number, bool node_is_terminal);

// Retryability hint resolution: the worker's `retryable` flag is advisory.
// This helper combines it with the error class into a single hint the retry
// policy (M32) consumes. Success is never retryable. Canceled is not
// retryable as a failure. Permanent errors are not retryable even if the
// worker said retryable (policy floor). Transient/resource-exhausted default
// to retryable unless the worker explicitly said not.
bool should_consider_retry(const execution::v1::ResultEnvelope& result);

}  // namespace evo
