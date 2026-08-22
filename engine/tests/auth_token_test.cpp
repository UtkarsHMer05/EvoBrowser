// M38 unit tests: engine-token authentication helpers (evo/auth_token.hpp).
//
// Pure scheduler-core: no gRPC, no transport. Verifies:
//   1. constant_time_equals: equal, unequal, length-mismatch, empty cases.
//   2. extract_bearer: valid bearer (case/whitespace variants), non-bearer,
//      empty, and malformed inputs all behave correctly.

#include <cstdio>
#include <string>

#include "evo/auth_token.hpp"

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
  // --- 1. constant_time_equals ---------------------------------------------
  check(evo::auth::constant_time_equals("abc", "abc"), "equal strings match");
  check(!evo::auth::constant_time_equals("abc", "abd"),
        "differing last byte rejected");
  check(!evo::auth::constant_time_equals("abc", "ab"),
        "length mismatch rejected (shorter b)");
  check(!evo::auth::constant_time_equals("ab", "abc"),
        "length mismatch rejected (longer b)");
  check(evo::auth::constant_time_equals("", ""), "empty strings match");
  check(!evo::auth::constant_time_equals("abc", ""),
        "non-empty vs empty rejected");
  check(!evo::auth::constant_time_equals("", "abc"),
        "empty vs non-empty rejected");
  check(evo::auth::constant_time_equals("a-longer-token-123", "a-longer-token-123"),
        "longer equal tokens match");

  // --- 2. extract_bearer ---------------------------------------------------
  check(evo::auth::extract_bearer("Bearer abc123") == "abc123",
        "standard bearer extracted");
  check(evo::auth::extract_bearer("bearer abc123") == "abc123",
        "lowercase scheme extracted");
  check(evo::auth::extract_bearer("BEARER abc123") == "abc123",
        "uppercase scheme extracted");
  check(evo::auth::extract_bearer("  Bearer   abc123  ") == "abc123",
        "surrounding whitespace trimmed");
  check(evo::auth::extract_bearer("Bearer") == "",
        "scheme-only (no token) rejected");
  check(evo::auth::extract_bearer("Basic abc123") == "",
        "non-bearer scheme rejected");
  check(evo::auth::extract_bearer("abc123") == "", "bare token rejected");
  check(evo::auth::extract_bearer("") == "", "empty header rejected");
  check(evo::auth::extract_bearer("BearerX abc") == "",
        "scheme not followed by whitespace rejected");

  if (failures == 0) {
    printf("\nALL M38 AUTH TOKEN TESTS PASSED!\n");
    return 0;
  }
  printf("\n%d FAILURE(S)\n", failures);
  return 1;
}
