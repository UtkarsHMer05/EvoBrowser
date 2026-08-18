// Milestone 04 toolchain proof. Verifies the C++20 features the engine
// contract depends on before any real engine code is written:
//   - C++20 language/library mode
//   - std::jthread + std::stop_token (owned, stoppable worker threads)
//   - std::chrono::steady_clock (all engine duration measurements)
// Returns non-zero on any failure so CTest reports it.

#include <chrono>
#include <iostream>
#include <thread>

#include "evo/version.hpp"

int main() {
  int failures = 0;
  const auto check = [&](bool ok, const char* what) {
    std::cout << (ok ? "  ok   " : "  FAIL ") << what << '\n';
    if (!ok) ++failures;
  };

  check(evo::build_info().cpp_standard >= 202002L, "C++20 mode active");
  check(evo::build_info().jthread_supported, "std::jthread available");

  // Prove jthread + stop_token actually work, not just compile.
  bool ran = false;
  {
    std::jthread worker([&](std::stop_token stop) {
      while (!stop.stop_requested()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
      }
      ran = true;  // reached only via cooperative stop, never detach/kill
    });
    worker.request_stop();
  }  // jthread destructor joins — RAII ownership proven
  check(ran, "jthread cooperative stop + RAII join");

  // steady_clock must be monotonic for evidence-grade duration measurement.
  const auto t0 = std::chrono::steady_clock::now();
  const auto t1 = std::chrono::steady_clock::now();
  check(t1 >= t0, "steady_clock monotonic");

  std::cout << evo::build_banner() << '\n';
  if (failures != 0) {
    std::cout << failures << " toolchain check(s) failed\n";
    return 1;
  }
  std::cout << "all toolchain checks passed\n";
  return 0;
}
