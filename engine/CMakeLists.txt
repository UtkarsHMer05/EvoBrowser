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
# Sanitizer build options (Milestone 14). ASan+UBSan are the standard C++
# correctness gate; TSan is opt-in via -DEVO_ENABLE_TSAN=ON and is *not*
# enabled together with ASan/UBSan (they are mutually incompatible on the same
# binary). Platform/compiler limits are documented in docs/phase2/PROGRESS.md.
# ---------------------------------------------------------------------------
option(EVO_ENABLE_ASAN "Enable AddressSanitizer + UBSan" OFF)
option(EVO_ENABLE_TSAN "Enable ThreadSanitizer (mutually exclusive with ASan)" OFF)

if(EVO_ENABLE_ASAN AND EVO_ENABLE_TSAN)
  message(FATAL_ERROR "EVO_ENABLE_ASAN and EVO_ENABLE_TSAN are mutually exclusive")
endif()

# Apply sanitizer flags globally so every project-owned target (library + tests)
# is instrumented consistently. This is safer than per-target options because a
# mismatch between sanitized library and unsanitized test produces ODR/double-
# free diagnostics under ASan. We set the *interface* flags on the core library
# and also the raw CXX_FLAGS so test executables link the runtime too.
if(EVO_ENABLE_ASAN)
  set(EVO_SANITIZE_FLAGS -fsanitize=address,undefined -fno-omit-frame-pointer)
endif()
if(EVO_ENABLE_TSAN)
  set(EVO_SANITIZE_FLAGS -fsanitize=thread -fno-omit-frame-pointer)
endif()
if(EVO_SANITIZE_FLAGS)
  add_compile_options(${EVO_SANITIZE_FLAGS})
  add_link_options(${EVO_SANITIZE_FLAGS})
  # Mark sanitizer builds as debuggable for meaningful stack traces.
  set(CMAKE_BUILD_TYPE Debug CACHE STRING "" FORCE)
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
  core/src/execution_policy.cpp
  core/src/concurrent_scheduler.cpp
  core/src/transport.cpp
)
target_include_directories(evo_scheduler_core PUBLIC core/include)
target_compile_features(evo_scheduler_core PUBLIC cxx_std_20)
target_compile_options(evo_scheduler_core PRIVATE
  -Wall -Wextra -Wpedantic -Werror
)
target_compile_definitions(evo_scheduler_core PUBLIC
  EVO_BUILD_COMMIT="${EVO_BUILD_COMMIT}"
  EVO_BUILD_TYPE="$<CONFIG>"
)

# ---------------------------------------------------------------------------
# evo_proto — generated C++ protobuf + gRPC bindings for the execution
# contract (Milestone 16–17). Protobuf + grpc headers are SYSTEM (no -Werror
# on third-party code). Generated sources are compiled in a separate target so
# the strict -Werror on evo_scheduler_core does not apply to them.
#
# Reproducible generation: the proto source-of-truth is engine/proto/evo/
# execution.proto. Generated *_pb.cc/_pb.h + *_grpc.pb.cc/_grpc.pb.h are emitted
# by `protoc` + the grpc C++ plugin (see engine/proto/generate.sh). The checked-
# in evo_gen/ copies are the committed artifacts; this custom command keeps them
# in sync when protoc is available.
# ---------------------------------------------------------------------------
find_package(protobuf CONFIG REQUIRED)
# gRPC: link via pkg-config for the complete, correctly-ordered absl set on
# Homebrew (the gRPC CMake config target does not propagate all
# absl::log_internal symbols reliably on macOS). Falls back to CMake config.
find_package(PkgConfig QUIET)
set(_grpc_libs "")
if(PkgConfig_FOUND)
  pkg_check_modules(GRPC_PKG QUIET grpc++)
  if(GRPC_PKG_FOUND)
    # Resolve pkg-config -l flags into absolute .dylib paths (macOS ld needs
    # the files findable; link_directories from the pkg-config Libs.private).
    pkg_check_modules(GRPC_PKG grpc++ IMPORTED_TARGET)
    set(_grpc_libs PkgConfig::GRPC_PKG)
  endif()
endif()
if(NOT _grpc_libs)
  set(_brew_prefixes /usr/local /opt/homebrew)
  find_package(gRPC CONFIG QUIET HINTS ${_brew_prefixes})
  if(gRPC_FOUND)
    set(_grpc_libs gRPC::grpc++)
  else()
    find_library(GRPCXX_LIB NAMES grpc++ grpc
      PATHS /usr/local/lib /opt/homebrew/lib /usr/local/Cellar/grpc/1.83.0_1/lib)
    find_library(GRPC_LIB NAMES grpc
      PATHS /usr/local/lib /opt/homebrew/lib /usr/local/Cellar/grpc/1.83.0_1/lib)
    set(_grpc_libs ${GRPCXX_LIB} ${GRPC_LIB})
  endif()
endif()
add_library(evo_proto STATIC
  ${CMAKE_CURRENT_SOURCE_DIR}/proto/evo_gen/evo/execution.pb.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/proto/evo_gen/evo/execution.grpc.pb.cc
)
target_include_directories(evo_proto SYSTEM PUBLIC
  ${Protobuf_INCLUDE_DIRS}
  ${CMAKE_CURRENT_SOURCE_DIR}/proto
  ${CMAKE_CURRENT_SOURCE_DIR}/proto/evo_gen
)
target_link_libraries(evo_proto PUBLIC protobuf::libprotobuf ${_grpc_libs})
# protobuf/absl/grpc headers use Clang extensions (-Wnullability-extension,
# -Wgcc-compat, -gnu-anonymous-struct, -Wnested-anon-types) that trip -Werror
# on project test targets linking evo_proto. Project code stays -Werror; these
# suppressions propagate to consumers to silence third-party header noise only.
target_compile_options(evo_proto PUBLIC
  -Wno-nullability-extension -Wno-gcc-compat
  -Wno-gnu-anonymous-struct -Wno-nested-anon-types
)
target_compile_features(evo_proto PUBLIC cxx_std_20)

# ---------------------------------------------------------------------------
# evo_envelope — task/result envelope semantics (Milestone 22). Depends on
# evo_proto (ResultEnvelope/TaskEnvelope) but not on the transport, so the
# dedupe/late-result/validation rules are testable without Redis.
# ---------------------------------------------------------------------------
add_library(evo_envelope STATIC core/src/envelope.cpp)
target_include_directories(evo_envelope PUBLIC core/include)
target_link_libraries(evo_envelope PUBLIC evo_proto)
target_compile_options(evo_envelope PRIVATE
  -Wall -Wextra -Wpedantic -Werror)
target_compile_features(evo_envelope PUBLIC cxx_std_20)

# ---------------------------------------------------------------------------
# evo_redis_transport — Redis Streams task transport (Milestone 21).
# Links the self-contained static hiredis built by
# engine/third_party/build-hiredis.sh (gitignored; arm64). hiredis headers are
# SYSTEM (third-party). If the prefix is missing, the target is skipped and
# the redis_transport test is not registered (documented in PROGRESS.md).
# ---------------------------------------------------------------------------
set(EVO_HIREDIS_PREFIX ${CMAKE_CURRENT_SOURCE_DIR}/third_party/hiredis-prefix)
set(EVO_HIREDIS_LIB ${EVO_HIREDIS_PREFIX}/lib/libhiredis.a)
if(EXISTS ${EVO_HIREDIS_LIB})
  add_library(evo_redis_transport STATIC redis/src/redis_transport.cpp)
  target_include_directories(evo_redis_transport PUBLIC redis/include)
  target_include_directories(evo_redis_transport SYSTEM PUBLIC
    ${EVO_HIREDIS_PREFIX}/include)
  target_link_libraries(evo_redis_transport PUBLIC evo_scheduler_core
    ${EVO_HIREDIS_LIB})
  target_compile_options(evo_redis_transport PRIVATE
    -Wall -Wextra -Wpedantic -Werror
    # hiredis headers use C99 flexible array members (sds.h) that trip
    # -Wpedantic in C++ mode; suppress third-party header noise only.
    -Wno-c99-extensions
  )
  target_compile_features(evo_redis_transport PUBLIC cxx_std_20)
  set(EVO_HAVE_HIREDIS ON)
else()
  message(STATUS
    "hiredis static lib not found at ${EVO_HIREDIS_LIB}; "
    "run engine/third_party/build-hiredis.sh to enable the Redis transport")
  set(EVO_HAVE_HIREDIS OFF)
endif()

# ---------------------------------------------------------------------------
# evo-smoke — prints version/build metadata only (no scheduling yet)
# ---------------------------------------------------------------------------
add_executable(evo-smoke app/smoke_main.cpp)
target_link_libraries(evo-smoke PRIVATE evo_scheduler_core)

# ---------------------------------------------------------------------------
# evo-bench — local benchmark corpus runner (Milestone 15). Release-only; no
# -Werror on the bench runner so wall-clock diagnostic code is not treated as
# failure-prone, but project core stays strict.
# ---------------------------------------------------------------------------
add_executable(evo-bench app/bench_runner.cpp)
target_link_libraries(evo-bench PRIVATE evo_scheduler_core)

# ---------------------------------------------------------------------------
# evo-scheduler-server — gRPC ControlService over the scheduler core (M17).
# Not wired into the Next.js app yet (M16/M17 no-go: do not integrate Next.js
# in this milestone). Listens on $EVO_SCHEDULER_ADDR (default 0.0.0.0:50051).
# ---------------------------------------------------------------------------
add_executable(evo-scheduler-server app/grpc_service.cpp)
target_link_libraries(evo-scheduler-server PRIVATE evo_proto evo_scheduler_core)

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

add_executable(evo_concurrent_scheduler_test tests/concurrent_scheduler_test.cpp)
target_link_libraries(evo_concurrent_scheduler_test PRIVATE evo_scheduler_core)
target_compile_options(evo_concurrent_scheduler_test PRIVATE
  -Wall -Wextra -Wpedantic -Werror
)
add_test(NAME concurrent_scheduler COMMAND evo_concurrent_scheduler_test)

add_executable(evo_affinity_scheduler_test tests/affinity_scheduler_test.cpp)
target_link_libraries(evo_affinity_scheduler_test PRIVATE evo_scheduler_core)
target_compile_options(evo_affinity_scheduler_test PRIVATE
  -Wall -Wextra -Wpedantic -Werror
)
add_test(NAME affinity_scheduler COMMAND evo_affinity_scheduler_test)

add_executable(evo_metrics_test tests/metrics_test.cpp)
target_link_libraries(evo_metrics_test PRIVATE evo_scheduler_core)
target_compile_options(evo_metrics_test PRIVATE
  -Wall -Wextra -Wpedantic -Werror
)
add_test(NAME metrics COMMAND evo_metrics_test)

add_executable(evo_stress_test tests/stress_test.cpp)
target_link_libraries(evo_stress_test PRIVATE evo_scheduler_core)
target_compile_options(evo_stress_test PRIVATE
  -Wall -Wextra -Wpedantic -Werror
)
add_test(NAME stress COMMAND evo_stress_test)

# ---------------------------------------------------------------------------
# evo-contract — golden round-trip tests for the protobuf (M16) contract.
# Uses protobuf headers as SYSTEM; test code itself is strict (-Werror).
# ---------------------------------------------------------------------------
add_executable(evo_contract_test tests/contract_test.cpp)
target_link_libraries(evo_contract_test PRIVATE evo_proto evo_scheduler_core)
target_compile_options(evo_contract_test PRIVATE
  -Wall -Wextra -Wpedantic -Werror
)
add_test(NAME contract COMMAND evo_contract_test)

# ---------------------------------------------------------------------------
# evo_grpc_integration — M17 end-to-end gRPC test. Spawns the
# evo-scheduler-server subprocess and drives it over a real gRPC channel.
# Skipped when EVO_SCHEDULER_SERVER_BIN is unset.
# ---------------------------------------------------------------------------
add_executable(evo_grpc_integration_test tests/grpc_integration_test.cpp)
target_link_libraries(evo_grpc_integration_test PRIVATE evo_proto evo_scheduler_core)
target_compile_options(evo_grpc_integration_test PRIVATE
  -Wall -Wextra -Wpedantic -Werror
  # grpc++ headers emit Clang-extension + deprecation warnings on macOS;
  # suppress third-party header noise only (project code stays -Werror).
  -Wno-nullability-extension -Wno-gcc-compat
  -Wno-gnu-anonymous-struct -Wno-nested-anon-types
  -Wno-deprecated-declarations
)
add_test(NAME grpc_integration COMMAND evo_grpc_integration_test)

# ---------------------------------------------------------------------------
# evo_transport — in-memory transport fake unit tests (M21). Always built;
# scheduler-core tests use the fake so they never need Redis.
# ---------------------------------------------------------------------------
add_executable(evo_transport_test tests/transport_test.cpp)
target_link_libraries(evo_transport_test PRIVATE evo_scheduler_core)
target_compile_options(evo_transport_test PRIVATE
  -Wall -Wextra -Wpedantic -Werror
)
add_test(NAME transport COMMAND evo_transport_test)

# ---------------------------------------------------------------------------
# evo_envelope_test — M22 envelope semantics: validation, size limits,
# malformed payloads, dedupe, late-result rules, retry-hint resolution.
# ---------------------------------------------------------------------------
add_executable(evo_envelope_test tests/envelope_test.cpp)
target_link_libraries(evo_envelope_test PRIVATE evo_envelope)
target_compile_options(evo_envelope_test PRIVATE
  -Wall -Wextra -Wpedantic -Werror
)
add_test(NAME envelope COMMAND evo_envelope_test)

# ---------------------------------------------------------------------------
# evo-envelope-fixture-gen — M22 cross-language golden fixture generator.
# Writes deterministic envelope bytes to engine/tests/fixtures/ for the TS
# cross-language test. Run manually after proto changes; fixtures are committed.
# ---------------------------------------------------------------------------
add_executable(evo-envelope-fixture-gen tests/envelope_fixture_gen.cpp)
target_link_libraries(evo-envelope-fixture-gen PRIVATE evo_envelope)
target_compile_options(evo-envelope-fixture-gen PRIVATE
  -Wall -Wextra -Wpedantic -Werror
)

# ---------------------------------------------------------------------------
# evo_redis_transport — integration test against the LOCAL Phase-2 Redis
# (scripts/phase2). Skipped at runtime when EVO_PHASE2_REDIS is unreachable.
# Only registered when the static hiredis prefix exists.
# ---------------------------------------------------------------------------
if(EVO_HAVE_HIREDIS)
  add_executable(evo_redis_transport_test tests/redis_transport_test.cpp)
  target_link_libraries(evo_redis_transport_test PRIVATE
    evo_redis_transport evo_proto evo_scheduler_core)
  target_compile_options(evo_redis_transport_test PRIVATE
    -Wall -Wextra -Wpedantic -Werror
  )
  add_test(NAME redis_transport COMMAND evo_redis_transport_test)
endif()
# Point the integration test at the real server binary so it runs instead of
# skipping (the test also honors an EVO_SCHEDULER_SERVER_BIN env override).
set_tests_properties(grpc_integration PROPERTIES
  ENVIRONMENT "EVO_SCHEDULER_SERVER_BIN=$<TARGET_FILE:evo-scheduler-server>"
)
