// M38 unit tests: input size limits + identifier validation (input_limits.hpp).
//
// Pure scheduler-core: no gRPC, no transport. Verifies:
//   1. is_valid_identifier accepts well-formed ids (UUIDs, prefixes, dots).
//   2. is_valid_identifier rejects empty, over-long, and bad-character ids.
//   3. is_dag_size_ok accepts payloads within the limit, rejects oversized.

#include <cstdio>
#include <string>

#include "evo/input_limits.hpp"

namespace {

int failures = 0;
void check(bool cond, const std::string& label) {
  if (cond) {
    printf("  ok   %s\n", label.c_str());
  } else {
    printf("  FAIL %s\n", label.c_str());
    ++failures;
  }
}

}  // namespace

int main() {
  // --- 1. Valid identifiers ------------------------------------------------
  check(evo::limits::is_valid_identifier("evo_011b7db3-df8c-4300"),
        "run id with underscore + dashes accepted");
  check(evo::limits::is_valid_identifier("org-a"), "org id accepted");
  check(evo::limits::is_valid_identifier("wf.v2"), "dotted id accepted");
  check(evo::limits::is_valid_identifier("A1.b2_c3-d4"),
        "mixed-case alphanumeric accepted");
  check(evo::limits::is_valid_identifier("x"), "single char accepted");

  // --- 2. Invalid identifiers ----------------------------------------------
  check(!evo::limits::is_valid_identifier(""), "empty rejected");
  check(!evo::limits::is_valid_identifier(std::string(257, 'a')),
        "over-long (257) rejected");
  check(evo::limits::is_valid_identifier(std::string(256, 'a')),
        "exactly 256 accepted (boundary)");
  check(!evo::limits::is_valid_identifier("run id"), "space rejected");
  check(!evo::limits::is_valid_identifier("run\nid"), "newline rejected");
  check(!evo::limits::is_valid_identifier("run/id"), "slash rejected");
  check(!evo::limits::is_valid_identifier("run\"id"), "quote rejected");
  check(!evo::limits::is_valid_identifier("run{id}"), "brace rejected");
  check(!evo::limits::is_valid_identifier("run\x01id"),
        "control char rejected");

  // --- 3. DAG payload size -------------------------------------------------
  check(evo::limits::is_dag_size_ok("{}"), "tiny payload accepted");
  check(evo::limits::is_dag_size_ok(std::string(1024, 'x')),
        "1 KiB payload accepted");
  check(evo::limits::is_dag_size_ok(
            std::string(evo::limits::kMaxDagJsonBytes, 'x')),
        "exactly-at-limit payload accepted (boundary)");
  check(!evo::limits::is_dag_size_ok(
            std::string(evo::limits::kMaxDagJsonBytes + 1, 'x')),
        "over-limit payload rejected");

  if (failures == 0) {
    printf("\nALL M38 INPUT LIMITS TESTS PASSED!\n");
    return 0;
  }
  printf("\n%d FAILURE(S)\n", failures);
  return 1;
}
