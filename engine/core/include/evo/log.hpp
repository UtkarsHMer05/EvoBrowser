#pragma once

// Milestone 38: structured JSON logging for the C++ engine services.
//
// Emits one JSON object per line to stderr so logs are machine-parseable and
// greppable. Every record carries a wall-clock UTC millisecond timestamp, a
// level, and an `event` name; callers add identifier fields (run_id, org_id,
// node_id, attempt, worker_id, trace_id) for correlation across the
// scheduler / transport / worker boundary (M38 step 1).
//
// Secret redaction (M38 step 2): any field whose KEY matches a secret-like
// pattern (password/secret/token/credential/authorization/api_key/private) has
// its VALUE replaced with "[REDACTED]" before serialization. Identifiers are
// never secret, so correlation fields pass through untouched. This is defense
// in depth — callers must not pass secrets in the first place.
//
// Thread-safe: a single mutex serializes writes so concurrent run loops never
// interleave partial lines.

#include <initializer_list>
#include <string>

namespace evo::log {

enum class Level { Debug, Info, Warn, Error };

// One structured field (key -> string value). The value is JSON-escaped; the
// key is emitted verbatim (callers use fixed literal keys).
struct Field {
  std::string key;
  std::string value;
};

// Emit one JSON log line to stderr. `event` is a short snake_case verb
// (e.g. "submit_accepted", "dispatch_deferred"). Thread-safe.
void emit(Level level, const std::string& event,
          std::initializer_list<Field> fields = {});

// Level conveniences.
void debug(const std::string& event, std::initializer_list<Field> fields = {});
void info(const std::string& event, std::initializer_list<Field> fields = {});
void warn(const std::string& event, std::initializer_list<Field> fields = {});
void error(const std::string& event, std::initializer_list<Field> fields = {});

// True when `key` names a secret-like field whose value must be redacted.
// Exposed for tests.
bool is_secret_key(const std::string& key);

// Redact `value` to "[REDACTED]" when `key` is secret-like; else return it
// unchanged. Exposed for tests.
std::string redact(const std::string& key, const std::string& value);

}  // namespace evo::log
