// M22 unit tests for task/result envelope semantics (evo/envelope.hpp):
// validation, size limits, malformed payloads, dedupe, late-result rules, and
// retry-hint resolution.

#include <cstdio>
#include <string>

#include "evo/envelope.hpp"

using evo::execution::v1::ResultEnvelope;
using evo::execution::v1::TaskEnvelope;

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

TaskEnvelope make_task(const std::string& run, const std::string& node,
                       unsigned attempt) {
  TaskEnvelope t;
  t.set_run_id(run);
  t.set_node_id(node);
  t.set_org_id("org");
  t.set_node_type("bench:sleep");
  t.set_attempt_number(attempt);
  t.set_node_payload_json("{\"ms\":3}");
  return t;
}

ResultEnvelope make_result(const std::string& run, const std::string& node,
                           unsigned attempt, bool completed) {
  ResultEnvelope r;
  r.set_run_id(run);
  r.set_node_id(node);
  r.set_attempt_number(attempt);
  r.set_completed(completed);
  if (!completed) r.set_error("boom");
  return r;
}
}  // namespace

int main() {
  // --- TaskEnvelope validation --------------------------------------------
  check(evo::validate_task_envelope(make_task("r", "n", 1)).empty(),
        "valid task envelope passes");
  {
    TaskEnvelope t = make_task("", "n", 1);
    check(!evo::validate_task_envelope(t).empty(), "empty run_id rejected");
  }
  {
    TaskEnvelope t = make_task("r", "n", 0);
    check(!evo::validate_task_envelope(t).empty(), "attempt_number 0 rejected");
  }
  {
    TaskEnvelope t = make_task("r", "n", 1);
    t.set_node_payload_json(std::string(evo::kMaxEnvelopeBytes, 'x'));
    check(!evo::validate_task_envelope(t).empty(),
          "oversized task payload rejected (size limit)");
  }

  // --- ResultEnvelope validation ------------------------------------------
  check(evo::validate_result_envelope(make_result("r", "n", 1, true)).empty(),
        "valid success result passes");
  check(evo::validate_result_envelope(make_result("r", "n", 1, false)).empty(),
        "valid failure result passes");
  {
    ResultEnvelope r = make_result("r", "n", 1, true);
    r.set_error("should not be here");
    check(!evo::validate_result_envelope(r).empty(),
          "completed result with error rejected");
  }
  {
    ResultEnvelope r = make_result("r", "n", 1, false);
    r.clear_error();
    check(!evo::validate_result_envelope(r).empty(),
          "failed result without error/abandoned rejected");
  }
  {
    ResultEnvelope r = make_result("r", "n", 1, false);
    r.clear_error();
    r.set_abandoned(true);
    check(evo::validate_result_envelope(r).empty(),
          "abandoned result without error accepted");
  }
  {
    ResultEnvelope r = make_result("r", "n", 1, true);
    r.set_output(std::string(evo::kMaxEnvelopeBytes, 'y'));
    check(!evo::validate_result_envelope(r).empty(),
          "oversized result output rejected (size limit)");
  }

  // --- Dedupe: first_time true once per attempt id ------------------------
  {
    evo::ResultDedupe dedupe;
    auto k1 = evo::attempt_key_of(make_result("r", "n", 1, true));
    auto k1_dup = evo::attempt_key_of(make_result("r", "n", 1, true));
    auto k2 = evo::attempt_key_of(make_result("r", "n", 2, true));
    check(dedupe.first_time(k1), "dedupe: first event applied");
    check(!dedupe.first_time(k1_dup), "dedupe: duplicate event ignored");
    check(dedupe.first_time(k2), "dedupe: new attempt applied");
    check(dedupe.size() == 2, "dedupe: 2 distinct attempts tracked");
  }

  // --- Late-result rule ----------------------------------------------------
  {
    auto current = make_result("r", "n", 3, true);
    check(evo::is_late_result(make_result("r", "n", 2, true), 3, false),
          "older attempt result is late (ignored)");
    check(!evo::is_late_result(current, 3, false),
          "current attempt result is not late");
    check(evo::is_late_result(current, 3, true),
          "result after terminal state is late (ignored)");
  }

  // --- Retry-hint resolution ------------------------------------------------
  {
    using evo::execution::v1::ERROR_PERMANENT;
    using evo::execution::v1::ERROR_TRANSIENT;
    using evo::execution::v1::ERROR_RESOURCE_EXHAUSTED;

    ResultEnvelope ok = make_result("r", "n", 1, true);
    check(!evo::should_consider_retry(ok), "success never retried");

    ResultEnvelope perm = make_result("r", "n", 1, false);
    perm.set_error_class(ERROR_PERMANENT);
    perm.set_retryable(true);  // worker says retry, but policy floor says no
    check(!evo::should_consider_retry(perm),
          "permanent error not retried even if worker hints retryable");

    ResultEnvelope trans = make_result("r", "n", 1, false);
    trans.set_error_class(ERROR_TRANSIENT);  // hint unset
    check(evo::should_consider_retry(trans),
          "transient error retried by default (hint unset)");

    ResultEnvelope trans_no = make_result("r", "n", 1, false);
    trans_no.set_error_class(ERROR_TRANSIENT);
    trans_no.set_retryable(false);  // worker explicitly says no
    check(!evo::should_consider_retry(trans_no),
          "transient error not retried when worker hints non-retryable");

    ResultEnvelope exhausted = make_result("r", "n", 1, false);
    exhausted.set_error_class(ERROR_RESOURCE_EXHAUSTED);
    check(evo::should_consider_retry(exhausted),
          "resource-exhausted retried by default (backoff at policy layer)");

    ResultEnvelope unknown = make_result("r", "n", 1, false);
    check(!evo::should_consider_retry(unknown),
          "unspecified error class retries only on explicit hint");

    ResultEnvelope canceled = make_result("r", "n", 1, false);
    canceled.set_status(ResultEnvelope::CANCELED);
    check(!evo::should_consider_retry(canceled), "canceled not retried");
  }

  if (failures == 0) {
    printf("\nALL M22 ENVELOPE SEMANTICS TESTS PASSED\n");
    return 0;
  }
  printf("\n%d M22 ENVELOPE TEST(S) FAILED\n", failures);
  return 1;
}
