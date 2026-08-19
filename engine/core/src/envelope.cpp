#include "evo/envelope.hpp"

namespace evo {

namespace {

bool is_empty(const std::string& s) { return s.empty(); }

}  // namespace

std::vector<std::string> validate_task_envelope(
    const execution::v1::TaskEnvelope& env) {
  std::vector<std::string> problems;
  if (is_empty(env.run_id())) problems.push_back("run_id is empty");
  if (is_empty(env.node_id())) problems.push_back("node_id is empty");
  if (is_empty(env.org_id())) problems.push_back("org_id is empty");
  if (is_empty(env.node_type())) problems.push_back("node_type is empty");
  if (env.attempt_number() == 0) {
    problems.push_back("attempt_number must be >= 1");
  }
  const std::size_t bytes = env.ByteSizeLong();
  if (bytes > kMaxEnvelopeBytes) {
    problems.push_back("envelope exceeds size limit (" + std::to_string(bytes) +
                       " > " + std::to_string(kMaxEnvelopeBytes) + " bytes)");
  }
  return problems;
}

std::vector<std::string> validate_result_envelope(
    const execution::v1::ResultEnvelope& env) {
  std::vector<std::string> problems;
  if (is_empty(env.run_id())) problems.push_back("run_id is empty");
  if (is_empty(env.node_id())) problems.push_back("node_id is empty");
  if (env.attempt_number() == 0) {
    problems.push_back("attempt_number must be >= 1");
  }
  if (env.completed() && !is_empty(env.error())) {
    problems.push_back("completed result must not carry an error");
  }
  if (!env.completed() && is_empty(env.error()) && !env.abandoned()) {
    problems.push_back("failed result must carry an error or be abandoned");
  }
  const std::size_t bytes = env.ByteSizeLong();
  if (bytes > kMaxEnvelopeBytes) {
    problems.push_back("envelope exceeds size limit (" + std::to_string(bytes) +
                       " > " + std::to_string(kMaxEnvelopeBytes) + " bytes)");
  }
  return problems;
}

AttemptKey attempt_key_of(const execution::v1::ResultEnvelope& env) {
  return AttemptKey{env.run_id(), env.node_id(), env.attempt_number()};
}

bool ResultDedupe::first_time(const AttemptKey& key) {
  std::lock_guard lock(mu_);
  return seen_.insert(key).second;
}

std::size_t ResultDedupe::size() const {
  std::lock_guard lock(mu_);
  return seen_.size();
}

bool is_late_result(const execution::v1::ResultEnvelope& result,
                    unsigned current_attempt_number, bool node_is_terminal) {
  if (node_is_terminal) return true;
  return result.attempt_number() < current_attempt_number;
}

bool should_consider_retry(const execution::v1::ResultEnvelope& result) {
  if (result.completed()) return false;
  if (result.status() == execution::v1::ResultEnvelope::CANCELED) return false;
  switch (result.error_class()) {
    case execution::v1::ERROR_PERMANENT:
      return false;  // policy floor: permanent errors never retry
    case execution::v1::ERROR_CANCELED:
      return false;
    case execution::v1::ERROR_TRANSIENT:
    case execution::v1::ERROR_RESOURCE_EXHAUSTED:
      // Default retryable unless the worker explicitly said not.
      return result.retryable() || !result.has_retryable();
    case execution::v1::ERROR_CLASS_UNSPECIFIED:
    default:
      // Unknown class: trust the worker's explicit hint only.
      return result.retryable();
  }
}

}  // namespace evo
