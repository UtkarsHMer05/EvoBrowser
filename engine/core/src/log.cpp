// Milestone 38: structured JSON logging implementation. See evo/log.hpp.

#include "evo/log.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <mutex>
#include <string>

#include "evo/json.hpp"

namespace evo::log {

namespace {

std::mutex g_log_mu;

const char* level_name(Level l) {
  switch (l) {
    case Level::Debug: return "debug";
    case Level::Info: return "info";
    case Level::Warn: return "warn";
    case Level::Error: return "error";
  }
  return "info";
}

std::string to_lower(std::string s) {
  std::transform(s.begin(), s.end(), s.begin(),
                 [](unsigned char c) { return std::tolower(c); });
  return s;
}

bool contains(const std::string& haystack, const char* needle) {
  return haystack.find(needle) != std::string::npos;
}

}  // namespace

bool is_secret_key(const std::string& key) {
  const std::string k = to_lower(key);
  return contains(k, "password") || contains(k, "secret") ||
         contains(k, "token") || contains(k, "credential") ||
         contains(k, "authorization") || contains(k, "api_key") ||
         contains(k, "apikey") || contains(k, "private_key");
}

std::string redact(const std::string& key, const std::string& value) {
  return is_secret_key(key) ? "[REDACTED]" : value;
}

void emit(Level level, const std::string& event,
          std::initializer_list<Field> fields) {
  const std::int64_t wall_ms =
      std::chrono::duration_cast<std::chrono::milliseconds>(
          std::chrono::system_clock::now().time_since_epoch())
          .count();

  json::Object o;
  o.emplace("ts_ms", json::Value(static_cast<double>(wall_ms)));
  o.emplace("level", json::Value(std::string(level_name(level))));
  o.emplace("event", json::Value(event));
  for (const auto& f : fields) {
    // Secret redaction: never serialize a secret-like value, even if a caller
    // passes one by mistake. Identifiers (run_id/org_id/...) pass through.
    o.emplace(f.key, json::Value(redact(f.key, f.value)));
  }

  const std::string line = json::serialize(json::Value(std::move(o)));
  std::lock_guard lock(g_log_mu);
  std::fprintf(stderr, "%s\n", line.c_str());
}

void debug(const std::string& event, std::initializer_list<Field> fields) {
  emit(Level::Debug, event, fields);
}
void info(const std::string& event, std::initializer_list<Field> fields) {
  emit(Level::Info, event, fields);
}
void warn(const std::string& event, std::initializer_list<Field> fields) {
  emit(Level::Warn, event, fields);
}
void error(const std::string& event, std::initializer_list<Field> fields) {
  emit(Level::Error, event, fields);
}

}  // namespace evo::log
