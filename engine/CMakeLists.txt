cmake_minimum_required(VERSION 3.25)

project(
  evo_engine
  VERSION 0.1.0
  DESCRIPTION "EvoBrowser Phase-2 concurrent DAG scheduler engine"
  LANGUAGES CXX
)

# ---------------------------------------------------------------------------
# Global settings
# ---------------------------------------------------------------------------
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

# Capture the commit this engine was configured from (evidence-grade builds
# must be attributable to a SHA).
execute_process(
  COMMAND git rev-parse --short HEAD
  WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
  OUTPUT_VARIABLE EVO_BUILD_COMMIT
  OUTPUT_STRIP_TRAILING_WHITESPACE
  ERROR_QUIET
)
if(NOT EVO_BUILD_COMMIT)
  set(EVO_BUILD_COMMIT "unknown")
endif()

# ---------------------------------------------------------------------------
# evo_scheduler_core — the engine library (Milestone 04: version metadata only)
# ---------------------------------------------------------------------------
add_library(evo_scheduler_core STATIC
  core/src/version.cpp
  core/src/json.cpp
  core/src/dag.cpp
  core/src/scheduler.cpp
  core/src/bench.cpp
  core/src/state_machine.cpp
  core/src/ready_queue.cpp
  core/src/thread_pool.cpp
)
target_include_directories(evo_scheduler_core PUBLIC core/include)
target_compile_features(evo_scheduler_core PUBLIC cxx_std_20)
target_compile_options(evo_scheduler_core PRIVATE
  -Wall -Wextra -Wpedantic -Werror
)
target_compile_definitions(evo_scheduler_core PRIVATE
  EVO_BUILD_COMMIT="${EVO_BUILD_COMMIT}"
  EVO_BUILD_TYPE="$<CONFIG>"
)

# ---------------------------------------------------------------------------
# evo-smoke — prints version/build metadata only (no scheduling yet)
# ---------------------------------------------------------------------------
add_executable(evo-smoke app/smoke_main.cpp)
target_link_libraries(evo-smoke PRIVATE evo_scheduler_core)

# ---------------------------------------------------------------------------
# Tests (CTest)
# ---------------------------------------------------------------------------
enable_testing()

add_executable(evo_toolchain_test tests/toolchain_test.cpp)
target_link_libraries(evo_toolchain_test PRIVATE evo_scheduler_core)
target_compile_options(evo_toolchain_test PRIVATE
  -Wall -Wextra -Wpedantic -Werror
)
add_test(NAME toolchain COMMAND evo_toolchain_test)

add_executable(evo_dag_test tests/dag_test.cpp)
target_link_libraries(evo_dag_test PRIVATE evo_scheduler_core)
target_compile_options(evo_dag_test PRIVATE
  -Wall -Wextra -Wpedantic -Werror
)
add_test(NAME dag COMMAND evo_dag_test)

add_executable(evo_scheduler_test tests/scheduler_test.cpp)
target_link_libraries(evo_scheduler_test PRIVATE evo_scheduler_core)
target_compile_options(evo_scheduler_test PRIVATE
  -Wall -Wextra -Wpedantic -Werror
)
add_test(NAME scheduler COMMAND evo_scheduler_test)

add_executable(evo_state_test tests/state_test.cpp)
target_link_libraries(evo_state_test PRIVATE evo_scheduler_core)
target_compile_options(evo_state_test PRIVATE
  -Wall -Wextra -Wpedantic -Werror
)
add_test(NAME state COMMAND evo_state_test)

add_executable(evo_ready_queue_test tests/ready_queue_test.cpp)
target_link_libraries(evo_ready_queue_test PRIVATE evo_scheduler_core)
target_compile_options(evo_ready_queue_test PRIVATE
  -Wall -Wextra -Wpedantic -Werror
)
add_test(NAME ready_queue COMMAND evo_ready_queue_test)

add_executable(evo_thread_pool_test tests/thread_pool_test.cpp)
target_link_libraries(evo_thread_pool_test PRIVATE evo_scheduler_core)
target_compile_options(evo_thread_pool_test PRIVATE
  -Wall -Wextra -Wpedantic -Werror
)
add_test(NAME thread_pool COMMAND evo_thread_pool_test)
