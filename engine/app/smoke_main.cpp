// Milestone 04 smoke executable: prints version/build metadata only.
// It performs no scheduling and touches no external service.

#include <iostream>

#include "evo/version.hpp"

int main() {
  std::cout << evo::build_banner() << '\n';
  return 0;
}
