// Milestone 38: input size limits + identifier validation. See input_limits.hpp.

#include "evo/input_limits.hpp"

namespace evo::limits {

bool is_valid_identifier(const std::string& id) {
  if (id.empty() || id.size() > kMaxIdLength) return false;
  for (const unsigned char c : id) {
    const bool ok = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
                    (c >= '0' && c <= '9') || c == '.' || c == '_' || c == '-';
    if (!ok) return false;
  }
  return true;
}

bool is_dag_size_ok(const std::string& dag_json) {
  return dag_json.size() <= kMaxDagJsonBytes;
}

}  // namespace evo::limits
