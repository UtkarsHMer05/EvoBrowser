// M38 unit tests: structured JSON logging + secret redaction (evo/log.hpp).
//
// Pure scheduler-core: no transport, no store, no threads. Verifies:
//   1. is_secret_key recognizes secret-like keys (case-insensitive, substring).
//   2. is_secret_key does NOT flag identifier/correlation keys.
//   3. redact replaces secret values with "[REDACTED]" and passes others.
//   4. emit() produces a parseable single-line JSON object with ts_ms/level/
//      event and the caller's fields.
//   5. emit() redacts a secret-like field value even if a caller passes one.
//   6. JSON string escaping keeps the line valid when a value has quotes.

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <unistd.h>

#include "evo/json.hpp"
#include "evo/log.hpp"

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

}  // namespace

int main() {
  // --- 1. Secret-like keys are recognized (case-insensitive, substring) ----
  check(evo::log::is_secret_key("password"), "password is secret-like");
  check(evo::log::is_secret_key("db_password"), "db_password is secret-like");
  check(evo::log::is_secret_key("PASSWORD"), "PASSWORD (upper) is secret-like");
  check(evo::log::is_secret_key("api_key"), "api_key is secret-like");
  check(evo::log::is_secret_key("apiKey"), "apiKey is secret-like");
  check(evo::log::is_secret_key("engine_token"), "engine_token is secret-like");
  check(evo::log::is_secret_key("authorization"), "authorization is secret-like");
  check(evo::log::is_secret_key("client_secret"), "client_secret is secret-like");
  check(evo::log::is_secret_key("credential"), "credential is secret-like");
  check(evo::log::is_secret_key("private_key"), "private_key is secret-like");

  // --- 2. Identifier/correlation keys are NOT flagged ----------------------
  check(!evo::log::is_secret_key("run_id"), "run_id is not secret-like");
  check(!evo::log::is_secret_key("org_id"), "org_id is not secret-like");
  check(!evo::log::is_secret_key("node_id"), "node_id is not secret-like");
  check(!evo::log::is_secret_key("trace_id"), "trace_id is not secret-like");
  check(!evo::log::is_secret_key("worker_id"), "worker_id is not secret-like");
  check(!evo::log::is_secret_key("event"), "event is not secret-like");
  check(!evo::log::is_secret_key("detail"), "detail is not secret-like");

  // --- 3. redact() replaces secret values, passes others -------------------
  check(evo::log::redact("password", "hunter2") == "[REDACTED]",
        "redact masks a password value");
  check(evo::log::redact("run_id", "r-1") == "r-1",
        "redact passes an identifier value");

  // --- 4/5/6. Live emit() captured from stderr and re-parsed ---------------
  // Redirect stderr to a temp file, emit records (including a secret-like
  // field and a value with quotes/backslashes), then read the file back and
  // parse each line as JSON. This exercises the REAL serialization + redaction
  // + escaping path end to end.
  const char* tmp_path = "/tmp/evo_m38_log_test.jsonl";
  std::fflush(stderr);
  const int saved_stderr = dup(fileno(stderr));
  FILE* tmp = freopen(tmp_path, "w", stderr);
  check(tmp != nullptr, "stderr redirected to temp file");

  evo::log::info("m38_log_test",
                 {
                     {"run_id", "r-1"},
                     {"org_id", "org-a"},
                     {"trace_id", "t-1"},
                     {"password", "should-be-redacted"},
                     {"detail", "value with \"quotes\" and \\backslash"},
                 });
  evo::log::warn("m38_warn_test", {{"worker_id", "w-9"}});

  std::fflush(stderr);
  dup2(saved_stderr, fileno(stderr));  // restore stderr
  close(saved_stderr);

  std::ifstream in(tmp_path);
  std::stringstream buf;
  buf << in.rdbuf();
  const std::string captured = buf.str();
  std::remove(tmp_path);

  // Split into lines; each must parse as a JSON object.
  std::vector<std::string> lines;
  {
    std::string cur;
    for (char c : captured) {
      if (c == '\n') {
        if (!cur.empty()) lines.push_back(cur);
        cur.clear();
      } else {
        cur.push_back(c);
      }
    }
    if (!cur.empty()) lines.push_back(cur);
  }
  check(lines.size() == 2, "emit() wrote exactly 2 JSON lines");

  bool all_parse = true;
  for (const auto& l : lines) {
    if (!evo::json::parse(l).has_value()) all_parse = false;
  }
  check(all_parse, "every emitted line parses as JSON (escaping correct)");

  if (lines.size() == 2) {
    auto rec1 = evo::json::parse(lines[0]);
    check(rec1.has_value() && rec1->find("event") &&
              rec1->find("event")->as_string() == "m38_log_test",
          "line 1 carries the event name");
    check(rec1.has_value() && rec1->find("level") &&
              rec1->find("level")->as_string() == "info",
          "line 1 carries level=info");
    check(rec1.has_value() && rec1->find("ts_ms") != nullptr,
          "line 1 carries a ts_ms timestamp");
    check(rec1.has_value() && rec1->find("run_id") &&
              rec1->find("run_id")->as_string() == "r-1",
          "line 1 carries run_id");
    check(rec1.has_value() && rec1->find("trace_id") &&
              rec1->find("trace_id")->as_string() == "t-1",
          "line 1 carries trace_id (correlation)");
    // Secret redaction: the password value must be masked, never raw.
    check(rec1.has_value() && rec1->find("password") &&
              rec1->find("password")->as_string() == "[REDACTED]",
          "line 1 redacts the secret-like field value");
    check(rec1.has_value() && rec1->find("password") &&
              rec1->find("password")->as_string().find("should-be-redacted") ==
                  std::string::npos,
          "raw secret value never appears in the log line");
    // Escaping: the quoted/backslash detail round-trips intact.
    check(rec1.has_value() && rec1->find("detail") &&
              rec1->find("detail")->as_string() ==
                  "value with \"quotes\" and \\backslash",
          "line 1 escapes quotes/backslashes correctly");

    auto rec2 = evo::json::parse(lines[1]);
    check(rec2.has_value() && rec2->find("level") &&
              rec2->find("level")->as_string() == "warn",
          "line 2 carries level=warn");
    check(rec2.has_value() && rec2->find("worker_id") &&
              rec2->find("worker_id")->as_string() == "w-9",
          "line 2 carries worker_id");
  }

  if (failures == 0) {
    printf("\nALL M38 LOG TESTS PASSED!\n");
    return 0;
  }
  printf("\n%d FAILURE(S)\n", failures);
  return 1;
}
