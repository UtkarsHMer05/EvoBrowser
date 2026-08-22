// Milestone 38: engine-token authentication helpers. See auth_token.hpp.

#include "evo/auth_token.hpp"

#include <cctype>

namespace evo::auth {

bool constant_time_equals(const std::string& a, const std::string& b) {
  // Scan every byte of `a` even on length mismatch so the comparison time does
  // not reveal where (or whether) the lengths differ.
  unsigned char diff = (a.size() == b.size()) ? 0 : 1;
  const std::size_t n = a.size();
  for (std::size_t i = 0; i < n; ++i) {
    const unsigned char bc =
        i < b.size() ? static_cast<unsigned char>(b[i]) : 0;
    diff |= static_cast<unsigned char>(a[i]) ^ bc;
  }
  return diff == 0;
}

std::string extract_bearer(const std::string& header_value) {
  // Trim leading whitespace.
  std::size_t start = 0;
  while (start < header_value.size() &&
         std::isspace(static_cast<unsigned char>(header_value[start]))) {
    ++start;
  }
  // Scheme must be "bearer" (case-insensitive) followed by whitespace.
  static const char kScheme[] = "bearer";
  const std::size_t scheme_len = sizeof(kScheme) - 1;
  if (header_value.size() < start + scheme_len + 1) return "";
  for (std::size_t i = 0; i < scheme_len; ++i) {
    if (std::tolower(static_cast<unsigned char>(header_value[start + i])) !=
        kScheme[i]) {
      return "";
    }
  }
  std::size_t tok = start + scheme_len;
  if (!std::isspace(static_cast<unsigned char>(header_value[tok]))) return "";
  // Skip whitespace between scheme and token.
  while (tok < header_value.size() &&
         std::isspace(static_cast<unsigned char>(header_value[tok]))) {
    ++tok;
  }
  // Token runs to the end, trimming trailing whitespace.
  std::size_t end = header_value.size();
  while (end > tok &&
         std::isspace(static_cast<unsigned char>(header_value[end - 1]))) {
    --end;
  }
  return header_value.substr(tok, end - tok);
}

}  // namespace evo::auth
