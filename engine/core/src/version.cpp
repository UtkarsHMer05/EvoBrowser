#include "evo/version.hpp"

#include <sstream>
#include <thread>
#include <type_traits>

namespace evo {

namespace {

// Detect std::jthread / std::stop_token at compile time. The engine contract
// requires owned, stoppable worker threads (never detached), so the toolchain
// must prove this capability before any pool work begins (Milestone 09).
template <typename, typename = void>
struct has_jthread : std::false_type {};

template <typename T>
struct has_jthread<T, std::void_t<decltype(std::declval<T&>().request_stop()),
                                  decltype(std::declval<T&>().joinable())>>
    : std::true_type {};

constexpr bool kJthreadSupported =
#if defined(__cpp_lib_jthread) && __cpp_lib_jthread >= 201911L
    true && has_jthread<std::jthread>::value;
#else
    false;
#endif

}  // namespace

const BuildInfo& build_info() {
  static const BuildInfo info{
      .version = "0.1.0",
      .commit = EVO_BUILD_COMMIT,
      .build_type = EVO_BUILD_TYPE,
      .compiler =
#if defined(__clang__)
          "clang " + std::to_string(__clang_major__) + "." +
          std::to_string(__clang_minor__) + "." +
          std::to_string(__clang_patchlevel__),
#elif defined(__GNUC__)
          "gcc " + std::to_string(__GNUC__) + "." +
          std::to_string(__GNUC_MINOR__) + "." +
          std::to_string(__GNUC_PATCHLEVEL__),
#else
          "unknown",
#endif
      .cpp_standard = static_cast<int>(__cplusplus),
      .jthread_supported = kJthreadSupported,
  };
  return info;
}

std::string build_banner() {
  const BuildInfo& info = build_info();
  std::ostringstream out;
  out << "evo-engine v" << info.version << " (" << info.build_type << ") "
      << "commit=" << info.commit << " compiler=" << info.compiler
      << " cxx=" << info.cpp_standard
      << " jthread=" << (info.jthread_supported ? "yes" : "no");
  return out.str();
}

}  // namespace evo
