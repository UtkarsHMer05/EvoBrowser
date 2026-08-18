#pragma once

// Build/version metadata for the Evo engine. Milestone 04 scope: metadata
// only — no scheduling, transport, or persistence lives here yet.

#include <string>

namespace evo {

struct BuildInfo {
  std::string version;      // engine semantic version
  std::string commit;       // git SHA captured at configure time
  std::string build_type;   // Release / Debug / ...
  std::string compiler;     // compiler identification string
  int cpp_standard;         // __cplusplus era (202002 for C++20)
  bool jthread_supported;   // std::jthread/stop_token availability
};

// Returns metadata about this build. Pure function; safe from any thread.
const BuildInfo& build_info();

// One-line human-readable banner, e.g. for the smoke executable.
std::string build_banner();

}  // namespace evo
