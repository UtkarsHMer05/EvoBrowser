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
# Clang-only third-party header noise suppressions (M38 CI). These -Wno-*
# options are not recognized by GCC; under -Werror an unknown warning option
# combined with any other warning becomes a hard error. The sanitizer and
# distributed CI jobs build the proto layer with GCC, so gate the Clang-only
# flags to Clang/AppleClang via generator expressions. -Wno-deprecated-
# declarations is valid on both compilers and stays unconditional.
# ---------------------------------------------------------------------------
set(EVO_IS_CLANG $<OR:$<CXX_COMPILER_ID:Clang>,$<CXX_COMPILER_ID:AppleClang>>)
set(EVO_CLANG_PROTO_FLAGS
  $<${EVO_IS_CLANG}:-Wno-nullability-extension>
  $<${EVO_IS_CLANG}:-Wno-gcc-compat>
  $<${EVO_IS_CLANG}:-Wno-gnu-anonymous-struct>
  $<${EVO_IS_CLANG}:-Wno-nested-anon-types>)
set(EVO_CLANG_HIREDIS_FLAGS
  $<${EVO_IS_CLANG}:-Wno-c99-extensions>)

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
  core/src/run_store.cpp
  core/src/quota.cpp
  core/src/log.cpp
  core/src/prometheus.cpp
  core/src/metrics_registry.cpp
  core/src/auth_token.cpp
  core/src/input_limits.cpp
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
# evo_proto / evo_proto_grpc — generated C++ protobuf (+ gRPC) bindings for
# the execution contract (Milestone 16–17). Protobuf + grpc headers are SYSTEM
# (no -Werror on third-party code). Generated sources are compiled in separate
# targets so the strict -Werror on evo_scheduler_core does not apply to them.
#
# Reproducible generation: the proto source-of-truth is engine/proto/evo/
# execution.proto. Generated *_pb.cc/_pb.h + *_grpc.pb.cc/_grpc.pb.h are emitted
# by `protoc` + the grpc C++ plugin (see engine/proto/generate.sh). The checked-
# in evo_gen/ copies are the committed artifacts.
# ---------------------------------------------------------------------------
# M38 CI: protobuf + gRPC are OPTIONAL, and split into two layers.
#
# The committed gencode is pinned to the exact protobuf runtime it was
# generated with (see the PROTOBUF_VERSION guard in
# proto/evo_gen/evo/execution.pb.h), which stock CI runners do not provide.
# CI therefore regenerates gencode against the runner's own protobuf before
# building the proto layer (engine/proto/generate.sh is platform-aware).
#
# Two capability levels, mirroring the hiredis/libpq graceful-degradation
# pattern below:
#   EVO_HAVE_PROTO — protobuf found. Builds the MESSAGES-ONLY layer
#     (evo_proto): envelope, distributed run loop, Redis/PG transports, and the
#     distributed e2e/crash/restart tests. This is all the distributed runtime
#     needs — it never touches gRPC.
#   EVO_HAVE_GRPC  — protobuf AND gRPC found. Additionally builds the gRPC
#     stubs (evo_proto_grpc) + the ControlService server and its integration
#     tests.
# When protobuf is absent we skip the whole proto layer and still build + test
# the full scheduler core. Local dev machines with Homebrew protobuf + grpc
# discover both and build everything.
#
# macOS dual-Homebrew guard (M38 CI): an arm64 Mac can have BOTH an Intel
# prefix (/usr/local, x86_64) and an Apple-silicon prefix (/opt/homebrew,
# arm64). A hintless find_package searches /usr/local first and can select the
# wrong-architecture protobuf while pkg-config's grpc++ resolves to the native
# prefix — an arch mismatch that fails at link. Prefer the NATIVE-architecture
# Homebrew prefix for protobuf + gRPC discovery. No-op on Linux (not APPLE).
if(APPLE)
  if(CMAKE_SYSTEM_PROCESSOR MATCHES "arm64|aarch64")
    set(_native_brew /opt/homebrew)
  else()
    set(_native_brew /usr/local)
  endif()
  # Prepend the native prefix so ALL transitive dependency resolution
  # (protobuf -> absl -> utf8_range, gRPC -> absl, ...) prefers the
  # native-architecture Homebrew. Without this, CMake's default search order
  # finds /usr/local (x86_64) before /opt/homebrew (arm64) for transitive
  # find_package calls, producing an arch mismatch at link.
  list(PREPEND CMAKE_PREFIX_PATH ${_native_brew})
endif()
find_package(protobuf CONFIG QUIET HINTS ${_native_brew}/lib/cmake/protobuf)
if(NOT protobuf_FOUND)
  find_package(protobuf CONFIG QUIET)
endif()
if(NOT protobuf_FOUND)
  # Fallback for system protobuf (Linux CI): CMake's FindProtobuf module.
  # Ubuntu's libprotobuf-dev may not ship CMake CONFIG files, but FindProtobuf
  # locates the headers + lib and defines protobuf::libprotobuf (CMake >= 3.9).
  find_package(Protobuf MODULE QUIET)
  if(Protobuf_FOUND)
    set(protobuf_FOUND ON)
  endif()
endif()
set(EVO_HAVE_PROTO OFF)
set(EVO_HAVE_GRPC OFF)
set(_grpc_libs "")
if(protobuf_FOUND)
  set(EVO_HAVE_PROTO ON)
  # gRPC: link via pkg-config for the complete, correctly-ordered absl set on
  # Homebrew (the gRPC CMake config target does not propagate all
  # absl::log_internal symbols reliably on macOS). Falls back to CMake config.
  find_package(PkgConfig QUIET)
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
    # Prefer the native-architecture Homebrew prefix (see dual-Homebrew guard
    # above); fall back to the other prefix for single-install machines.
    if(APPLE)
      if(_native_brew STREQUAL "/opt/homebrew")
        set(_brew_prefixes /opt/homebrew /usr/local)
      else()
        set(_brew_prefixes /usr/local /opt/homebrew)
      endif()
    else()
      set(_brew_prefixes /usr/local /opt/homebrew)
    endif()
    find_package(gRPC CONFIG QUIET HINTS ${_brew_prefixes})
    if(gRPC_FOUND)
      set(_grpc_libs gRPC::grpc++)
    else()
      find_library(GRPCXX_LIB NAMES grpc++ grpc
        PATHS ${_brew_prefixes}/lib /usr/local/Cellar/grpc/1.83.0_1/lib)
      find_library(GRPC_LIB NAMES grpc
        PATHS ${_brew_prefixes}/lib /usr/local/Cellar/grpc/1.83.0_1/lib)
      if(GRPCXX_LIB AND GRPC_LIB)
        set(_grpc_libs ${GRPCXX_LIB} ${GRPC_LIB})
      endif()
    endif()
  endif()
  if(_grpc_libs)
    set(EVO_HAVE_GRPC ON)
  endif()
endif()

if(EVO_HAVE_PROTO)
# evo_proto — MESSAGES-ONLY generated bindings (execution.pb.cc). Everything
# the distributed runtime needs; no gRPC dependency. Protobuf headers are
# SYSTEM (no -Werror on third-party code). Generated sources are compiled in a
# separate target so the strict -Werror on evo_scheduler_core does not apply.
#
# Reproducible generation: the proto source-of-truth is engine/proto/evo/
# execution.proto. Generated *_pb.cc/_pb.h + *_grpc.pb.cc/_grpc.pb.h are emitted
# by `protoc` + the grpc C++ plugin (see engine/proto/generate.sh). The checked-
# in evo_gen/ copies are the committed artifacts; CI regenerates them against
# the runner's protobuf before building this layer.
add_library(evo_proto STATIC
  ${CMAKE_CURRENT_SOURCE_DIR}/proto/evo_gen/evo/execution.pb.cc
)
target_include_directories(evo_proto SYSTEM PUBLIC
  ${Protobuf_INCLUDE_DIRS}
  ${CMAKE_CURRENT_SOURCE_DIR}/proto
  ${CMAKE_CURRENT_SOURCE_DIR}/proto/evo_gen
)
target_link_libraries(evo_proto PUBLIC protobuf::libprotobuf)
# protobuf headers use Clang extensions (-Wnullability-extension,
# -Wgcc-compat, -gnu-anonymous-struct, -Wnested-anon-types) that trip -Werror
# on project test targets linking evo_proto. Project code stays -Werror; these
# suppressions propagate to consumers to silence third-party header noise only.
# Gated to Clang (M38 CI): GCC does not recognize these options.
target_compile_options(evo_proto PUBLIC ${EVO_CLANG_PROTO_FLAGS})
target_compile_features(evo_proto PUBLIC cxx_std_20)

# evo_proto_grpc — gRPC service stubs (execution.grpc.pb.cc). Only built when
# gRPC is available; consumed by the ControlService server + its tests.
if(EVO_HAVE_GRPC)
add_library(evo_proto_grpc STATIC
  ${CMAKE_CURRENT_SOURCE_DIR}/proto/evo_gen/evo/execution.grpc.pb.cc
)
target_include_directories(evo_proto_grpc SYSTEM PUBLIC
  ${Protobuf_INCLUDE_DIRS}
  ${CMAKE_CURRENT_SOURCE_DIR}/proto
  ${CMAKE_CURRENT_SOURCE_DIR}/proto/evo_gen
)
target_link_libraries(evo_proto_grpc PUBLIC evo_proto ${_grpc_libs})
target_compile_options(evo_proto_grpc PUBLIC ${EVO_CLANG_PROTO_FLAGS})
target_compile_features(evo_proto_grpc PUBLIC cxx_std_20)
endif()  # EVO_HAVE_GRPC (gRPC stubs)
else()
  message(STATUS
    "protobuf not found; skipping the proto layer (envelope, distributed "
    "run loop, gRPC server + their tests). The scheduler core still builds.")
endif()  # EVO_HAVE_PROTO

# ---------------------------------------------------------------------------
# evo_envelope — task/result envelope semantics (Milestone 22). Depends on
# evo_proto (ResultEnvelope/TaskEnvelope) but not on the transport, so the
# dedupe/late-result/validation rules are testable without Redis.
#
# M38 CI: the whole proto layer (envelope -> distributed -> gRPC server and
# their tests) is gated on EVO_HAVE_PROTO. On runners without the pinned
# protobuf runtime we skip it and still build/test the scheduler core.
# ---------------------------------------------------------------------------
if(EVO_HAVE_PROTO)
add_library(evo_envelope STATIC core/src/envelope.cpp)
target_include_directories(evo_envelope PUBLIC core/include)
target_link_libraries(evo_envelope PUBLIC evo_proto)
target_compile_options(evo_envelope PRIVATE
  -Wall -Wextra -Wpedantic -Werror)
target_compile_features(evo_envelope PUBLIC cxx_std_20)

# ---------------------------------------------------------------------------
# evo_distributed — the distributed run loop (Milestone 26): scheduler ->
# worker -> result -> successor cycle over a durable transport + RunStore.
# Depends on the scheduler core (state machine, DAG, transport abstraction)
# and the envelope semantics (dedupe/late-result/validation). Not part of
# evo_scheduler_core because it needs the protobuf contract.
# ---------------------------------------------------------------------------
add_library(evo_distributed STATIC
  core/src/distributed_run_loop.cpp
  core/src/retry_policy.cpp
)
target_include_directories(evo_distributed PUBLIC core/include)
target_link_libraries(evo_distributed PUBLIC evo_scheduler_core evo_envelope)
target_compile_options(evo_distributed PRIVATE
  -Wall -Wextra -Wpedantic -Werror)
target_compile_features(evo_distributed PUBLIC cxx_std_20)
endif()  # EVO_HAVE_PROTO (envelope + distributed)

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
    # Clang-only flag gated via generator expression (M38 CI).
    ${EVO_CLANG_HIREDIS_FLAGS}
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
# evo_pg_run_store — Postgres RunStore over libpq (Milestone 26). Parameterized
# SQL against the M19 schema; never owns migrations. libpq headers are SYSTEM
# (third-party). If libpq is not found, the target is skipped and the
# pg_run_store test is not registered (documented in PROGRESS.md).
# ---------------------------------------------------------------------------
set(_libpq_prefixes /opt/homebrew/opt/libpq /usr/local/opt/libpq)
find_path(LIBPQ_INCLUDE_DIR libpq-fe.h
  PATHS ${_libpq_prefixes}
  PATH_SUFFIXES include postgresql include/postgresql)
find_library(LIBPQ_LIBRARY NAMES pq libpq
  PATHS ${_libpq_prefixes}
  PATH_SUFFIXES lib)
if(LIBPQ_INCLUDE_DIR AND LIBPQ_LIBRARY)
  add_library(evo_pg_run_store STATIC pg/src/pg_run_store.cpp)
  target_include_directories(evo_pg_run_store PUBLIC pg/include)
  target_include_directories(evo_pg_run_store SYSTEM PUBLIC ${LIBPQ_INCLUDE_DIR})
  target_link_libraries(evo_pg_run_store PUBLIC evo_scheduler_core
    ${LIBPQ_LIBRARY})
  target_compile_options(evo_pg_run_store PRIVATE
    -Wall -Wextra -Wpedantic -Werror)
  target_compile_features(evo_pg_run_store PUBLIC cxx_std_20)
  set(EVO_HAVE_LIBPQ ON)
else()
  message(STATUS
    "libpq not found; run `brew install libpq` to enable the Postgres RunStore")
  set(EVO_HAVE_LIBPQ OFF)
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
# evo-m39-local — M39 final local-scheduler performance campaign. Evidence
# runner (shapes x sizes x io/cpu profiles x thread counts) emitting
# BENCHMARK_METHODOLOGY.md §4 artifacts. Not a CTest target: it is invoked by
# scripts/phase2/m39-campaign.sh on the reference machine (evidence-grade
# numbers never come from shared CI runners, methodology §1.8).
# ---------------------------------------------------------------------------
add_executable(evo-m39-local app/m39_local_campaign.cpp)
target_link_libraries(evo-m39-local PRIVATE evo_scheduler_core)

# ---------------------------------------------------------------------------
# evo-scheduler-server — gRPC ControlService over the scheduler core (M17).
# Milestone 29: when the Redis transport + Postgres RunStore are available,
# product DAGs are driven by the DistributedRunLoop (EVO_HAVE_DISTRIBUTED);
# synthetic DAGs always run in-process. Listens on $EVO_SCHEDULER_ADDR
# (default 127.0.0.1:50051). Requires the gRPC layer (M38 CI gate).
# ---------------------------------------------------------------------------
if(EVO_HAVE_GRPC)
add_executable(evo-scheduler-server app/grpc_service.cpp)
target_link_libraries(evo-scheduler-server PRIVATE evo_proto_grpc evo_scheduler_core)
if(EVO_HAVE_HIREDIS AND EVO_HAVE_LIBPQ)
  target_link_libraries(evo-scheduler-server PRIVATE
    evo_distributed evo_redis_transport evo_pg_run_store)
  target_compile_definitions(evo-scheduler-server PRIVATE EVO_HAVE_DISTRIBUTED)
endif()
endif()  # EVO_HAVE_GRPC (server)

# ---------------------------------------------------------------------------
# evo-run-loop-driver — M35: single-run scheduler driver binary. Runs ONE
# DistributedRunLoop against real Redis + Postgres so the restart-recovery
# integration test can SIGKILL the scheduler as a real process and restart it
# with resume=true. Requires both hiredis and libpq. No gRPC dependency.
# ---------------------------------------------------------------------------
if(EVO_HAVE_PROTO AND EVO_HAVE_HIREDIS AND EVO_HAVE_LIBPQ)
  add_executable(evo-run-loop-driver app/run_loop_driver.cpp)
  target_link_libraries(evo-run-loop-driver PRIVATE
    evo_distributed evo_redis_transport evo_pg_run_store)
  target_compile_options(evo-run-loop-driver PRIVATE
    -Wall -Wextra -Wpedantic -Werror
    ${EVO_CLANG_HIREDIS_FLAGS}  # hiredis headers (transitive)
  )
endif()

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

# evo_quota_test — M36: multi-tenant quota gate (admission + task capacity +
# counters). Pure scheduler-core; no transport/store/threads.
add_executable(evo_quota_test tests/quota_test.cpp)
target_link_libraries(evo_quota_test PRIVATE evo_scheduler_core)
target_compile_options(evo_quota_test PRIVATE
  -Wall -Wextra -Wpedantic -Werror
)
add_test(NAME quota COMMAND evo_quota_test)

# evo_log_test — M38: structured JSON logging + secret redaction. Pure
# scheduler-core; captures stderr to verify the emitted lines parse as JSON.
add_executable(evo_log_test tests/log_test.cpp)
target_link_libraries(evo_log_test PRIVATE evo_scheduler_core)
target_compile_options(evo_log_test PRIVATE
  -Wall -Wextra -Wpedantic -Werror
)
add_test(NAME log COMMAND evo_log_test)

# evo_metrics_registry_test — M38: Prometheus text format + thread-safe metrics
# registry. Pure scheduler-core; includes a concurrency check (TSan-verified).
add_executable(evo_metrics_registry_test tests/metrics_registry_test.cpp)
target_link_libraries(evo_metrics_registry_test PRIVATE evo_scheduler_core)
target_compile_options(evo_metrics_registry_test PRIVATE
  -Wall -Wextra -Wpedantic -Werror
)
add_test(NAME metrics_registry COMMAND evo_metrics_registry_test)

# evo_auth_token_test — M38: engine-token auth helpers (constant-time compare +
# bearer extraction). Pure scheduler-core; no gRPC.
add_executable(evo_auth_token_test tests/auth_token_test.cpp)
target_link_libraries(evo_auth_token_test PRIVATE evo_scheduler_core)
target_compile_options(evo_auth_token_test PRIVATE
  -Wall -Wextra -Wpedantic -Werror
)
add_test(NAME auth_token COMMAND evo_auth_token_test)

# evo_input_limits_test — M38: input size limits + identifier validation at the
# trust boundary. Pure scheduler-core; no gRPC.
add_executable(evo_input_limits_test tests/input_limits_test.cpp)
target_link_libraries(evo_input_limits_test PRIVATE evo_scheduler_core)
target_compile_options(evo_input_limits_test PRIVATE
  -Wall -Wextra -Wpedantic -Werror
)
add_test(NAME input_limits COMMAND evo_input_limits_test)

# evo_fairness_bench_test — M37: per-tenant wait distributions + Jain's fairness
# index on controlled workloads (equal + unequal task durations). Scheduler-core
# only (InMemoryTransport + InMemoryRunStore); emits raw samples when
# EVO_M37_ARTIFACT_DIR is set. Diagnostic timings, not evidence-grade (M39).
# Requires the proto layer (M38 CI gate).
if(EVO_HAVE_PROTO)
add_executable(evo_fairness_bench_test tests/fairness_bench_test.cpp)
target_link_libraries(evo_fairness_bench_test PRIVATE evo_distributed)
target_compile_options(evo_fairness_bench_test PRIVATE
  -Wall -Wextra -Wpedantic -Werror
)
add_test(NAME fairness_bench COMMAND evo_fairness_bench_test)
set_tests_properties(fairness_bench PROPERTIES TIMEOUT 120)
endif()

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
# Requires the gRPC layer (asserts the ControlService descriptor).
# ---------------------------------------------------------------------------
if(EVO_HAVE_GRPC)
add_executable(evo_contract_test tests/contract_test.cpp)
target_link_libraries(evo_contract_test PRIVATE evo_proto_grpc evo_scheduler_core)
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
target_link_libraries(evo_grpc_integration_test PRIVATE evo_proto_grpc evo_scheduler_core)
target_compile_options(evo_grpc_integration_test PRIVATE
  -Wall -Wextra -Wpedantic -Werror
  # grpc++ headers emit Clang-extension + deprecation warnings on macOS;
  # suppress third-party header noise only (project code stays -Werror).
  # Clang-only flags gated via generator expression (M38 CI).
  ${EVO_CLANG_PROTO_FLAGS}
  -Wno-deprecated-declarations
)
add_test(NAME grpc_integration COMMAND evo_grpc_integration_test)

# ---------------------------------------------------------------------------
# evo_quota_admission — M36 multi-tenant admission control over the real gRPC
# ControlService. Spawns evo-scheduler-server with a per-org active-run cap and
# asserts RESOURCE_EXHAUSTED rejection + per-org isolation + Health counters.
# Skipped when EVO_SCHEDULER_SERVER_BIN is unset.
# ---------------------------------------------------------------------------
add_executable(evo_quota_admission_test tests/quota_admission_test.cpp)
target_link_libraries(evo_quota_admission_test PRIVATE evo_proto_grpc evo_scheduler_core)
target_compile_options(evo_quota_admission_test PRIVATE
  -Wall -Wextra -Wpedantic -Werror
  ${EVO_CLANG_PROTO_FLAGS}
  -Wno-deprecated-declarations
)
add_test(NAME quota_admission COMMAND evo_quota_admission_test)
set_tests_properties(quota_admission PROPERTIES TIMEOUT 60)

# ---------------------------------------------------------------------------
# evo_auth_integration — M38: service-to-service engine-token auth (negative
# tests). Spawns the server with/without EVO_ENGINE_TOKEN and asserts
# UNAUTHENTICATED rejection + backwards-compatible default. Skipped when
# EVO_SCHEDULER_SERVER_BIN is unset.
# ---------------------------------------------------------------------------
add_executable(evo_auth_integration_test tests/auth_integration_test.cpp)
target_link_libraries(evo_auth_integration_test PRIVATE evo_proto_grpc evo_scheduler_core)
target_compile_options(evo_auth_integration_test PRIVATE
  -Wall -Wextra -Wpedantic -Werror
  ${EVO_CLANG_PROTO_FLAGS}
  -Wno-deprecated-declarations
)
add_test(NAME auth_integration COMMAND evo_auth_integration_test)
set_tests_properties(auth_integration PROPERTIES TIMEOUT 60)
endif()  # EVO_HAVE_GRPC (contract + gRPC server tests)

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
# Requires the proto layer (M38 CI gate).
# ---------------------------------------------------------------------------
if(EVO_HAVE_PROTO)
add_executable(evo_envelope_test tests/envelope_test.cpp)
target_link_libraries(evo_envelope_test PRIVATE evo_envelope)
target_compile_options(evo_envelope_test PRIVATE
  -Wall -Wextra -Wpedantic -Werror
)
add_test(NAME envelope COMMAND evo_envelope_test)

# ---------------------------------------------------------------------------
# evo_retry_policy_test — M32: error taxonomy, retryability policy floor,
# attempt budget + dead-lettering, deterministic exponential backoff + jitter.
# Requires the proto layer (M38 CI gate).
# ---------------------------------------------------------------------------
add_executable(evo_retry_policy_test tests/retry_policy_test.cpp)
target_link_libraries(evo_retry_policy_test PRIVATE evo_distributed)
target_compile_options(evo_retry_policy_test PRIVATE
  -Wall -Wextra -Wpedantic -Werror
)
add_test(NAME retry_policy COMMAND evo_retry_policy_test)

# ---------------------------------------------------------------------------
# evo_distributed_run_loop_test — M26: scheduler -> worker -> result ->
# successor cycle over the in-memory transport + store. Duplicate-result
# injection, identity validation, failure/cancel/stop paths, audit asserts.
# Requires the proto layer (M38 CI gate).
# ---------------------------------------------------------------------------
add_executable(evo_distributed_run_loop_test tests/distributed_run_loop_test.cpp)
target_link_libraries(evo_distributed_run_loop_test PRIVATE evo_distributed)
target_compile_options(evo_distributed_run_loop_test PRIVATE
  -Wall -Wextra -Wpedantic -Werror
)
add_test(NAME distributed_run_loop COMMAND evo_distributed_run_loop_test)

# ---------------------------------------------------------------------------
# evo-envelope-fixture-gen — M22 cross-language golden fixture generator.
# Writes deterministic envelope bytes to engine/tests/fixtures/ for the TS
# cross-language test. Run manually after proto changes; fixtures are committed.
# Requires the proto layer (M38 CI gate).
# ---------------------------------------------------------------------------
add_executable(evo-envelope-fixture-gen tests/envelope_fixture_gen.cpp)
target_link_libraries(evo-envelope-fixture-gen PRIVATE evo_envelope)
target_compile_options(evo-envelope-fixture-gen PRIVATE
  -Wall -Wextra -Wpedantic -Werror
)
endif()  # EVO_HAVE_PROTO (proto-dependent tests)

# ---------------------------------------------------------------------------
# evo_redis_transport — integration test against the LOCAL Phase-2 Redis
# (scripts/phase2). Skipped at runtime when EVO_PHASE2_REDIS is unreachable.
# Only registered when the static hiredis prefix exists AND the proto layer is
# available (the test round-trips TaskEnvelope bytes).
# ---------------------------------------------------------------------------
if(EVO_HAVE_HIREDIS AND EVO_HAVE_PROTO)
  add_executable(evo_redis_transport_test tests/redis_transport_test.cpp)
  target_link_libraries(evo_redis_transport_test PRIVATE
    evo_redis_transport evo_proto evo_scheduler_core)
  target_compile_options(evo_redis_transport_test PRIVATE
    -Wall -Wextra -Wpedantic -Werror
  )
  add_test(NAME redis_transport COMMAND evo_redis_transport_test)
endif()

# ---------------------------------------------------------------------------
# evo_pg_run_store_test — M26 integration test against the LOCAL Phase-2
# Postgres (scripts/phase2). Skipped at runtime when unreachable/unmigrated.
# Only registered when libpq was found. No proto dependency.
# ---------------------------------------------------------------------------
if(EVO_HAVE_LIBPQ)
  add_executable(evo_pg_run_store_test tests/pg_run_store_test.cpp)
  target_link_libraries(evo_pg_run_store_test PRIVATE evo_pg_run_store)
  target_compile_options(evo_pg_run_store_test PRIVATE
    -Wall -Wextra -Wpedantic -Werror
  )
  add_test(NAME pg_run_store COMMAND evo_pg_run_store_test)
endif()

# ---------------------------------------------------------------------------
# evo_distributed_e2e_test — M26 step 8: C++ run loop + real Redis + multiple
# TS worker processes + real Postgres, diamond DAG, duplicate-result storm,
# audit assertions. Skips at runtime when Redis/Postgres/tsx are unavailable.
# Requires hiredis + libpq + the proto layer (M38 CI gate).
# ---------------------------------------------------------------------------
if(EVO_HAVE_HIREDIS AND EVO_HAVE_LIBPQ AND EVO_HAVE_PROTO)
  add_executable(evo_distributed_e2e_test tests/distributed_e2e_test.cpp)
  target_link_libraries(evo_distributed_e2e_test PRIVATE
    evo_distributed evo_redis_transport evo_pg_run_store)
  target_compile_options(evo_distributed_e2e_test PRIVATE
    -Wall -Wextra -Wpedantic -Werror
    ${EVO_CLANG_HIREDIS_FLAGS}  # hiredis headers (transitive)
  )
  target_compile_definitions(evo_distributed_e2e_test PRIVATE
    EVO_REPO_ROOT_DIR="${CMAKE_CURRENT_SOURCE_DIR}/.."
  )
  add_test(NAME distributed_e2e COMMAND evo_distributed_e2e_test)
  # 300s (not 180s): the run itself is fast, but shared CI runners + the final
  # PEL-drain poll need headroom. Matches crash_recovery/scheduler_restart.
  set_tests_properties(distributed_e2e PROPERTIES TIMEOUT 300)

  # -------------------------------------------------------------------------
  # evo_crash_recovery_test — M34: real SIGKILL fault injection of a
  # lease-holding TS worker mid-task; lease expiry -> reassignment on a
  # surviving worker -> no lost task; recovery timeline recorded as raw
  # samples (EVO_M34_ARTIFACT_DIR). Skips at runtime when Redis/Postgres/tsx
  # are unavailable. Requires hiredis + libpq + proto.
  # -------------------------------------------------------------------------
  add_executable(evo_crash_recovery_test tests/crash_recovery_test.cpp)
  target_link_libraries(evo_crash_recovery_test PRIVATE
    evo_distributed evo_redis_transport evo_pg_run_store)
  target_compile_options(evo_crash_recovery_test PRIVATE
    -Wall -Wextra -Wpedantic -Werror
    ${EVO_CLANG_HIREDIS_FLAGS}  # hiredis headers (transitive)
  )
  target_compile_definitions(evo_crash_recovery_test PRIVATE
    EVO_REPO_ROOT_DIR="${CMAKE_CURRENT_SOURCE_DIR}/.."
  )
  add_test(NAME crash_recovery COMMAND evo_crash_recovery_test)
  set_tests_properties(crash_recovery PROPERTIES TIMEOUT 300)

  # -------------------------------------------------------------------------
  # evo_scheduler_restart_test — M35: SIGKILL the scheduler process mid-run
  # (workers/Redis/Postgres stay alive), restart it with resume=true, and
  # verify the run reaches a consistent terminal outcome with no lost task and
  # no double-completion; plus restart-with-no-active-work (no resurrection).
  # Skips at runtime when Redis/Postgres/tsx/the driver are unavailable.
  # -------------------------------------------------------------------------
  add_executable(evo_scheduler_restart_test tests/scheduler_restart_test.cpp)
  target_link_libraries(evo_scheduler_restart_test PRIVATE
    evo_distributed evo_redis_transport evo_pg_run_store)
  target_compile_options(evo_scheduler_restart_test PRIVATE
    -Wall -Wextra -Wpedantic -Werror
    ${EVO_CLANG_HIREDIS_FLAGS}  # hiredis headers (transitive)
  )
  target_compile_definitions(evo_scheduler_restart_test PRIVATE
    EVO_REPO_ROOT_DIR="${CMAKE_CURRENT_SOURCE_DIR}/.."
  )
  add_test(NAME scheduler_restart COMMAND evo_scheduler_restart_test)
  set_tests_properties(scheduler_restart PROPERTIES TIMEOUT 300)
  # Point the test at the real driver binary so it runs instead of skipping
  # (the test also honors an EVO_M35_DRIVER_BIN env override).
  set_tests_properties(scheduler_restart PROPERTIES
    ENVIRONMENT "EVO_M35_DRIVER_BIN=$<TARGET_FILE:evo-run-loop-driver>"
  )

  # -------------------------------------------------------------------------
  # evo_m39_scaling_test — M39: distributed worker scaling campaign (worker
  # counts x logical-task counts over real Redis + Postgres + TS workers),
  # emitting BENCHMARK_METHODOLOGY.md §4 artifacts (EVO_M39_ARTIFACT_DIR).
  # Skips at runtime when Redis/Postgres/tsx are unavailable. Requires
  # hiredis + libpq + proto.
  # -------------------------------------------------------------------------
  add_executable(evo_m39_scaling_test tests/m39_scaling_test.cpp)
  target_link_libraries(evo_m39_scaling_test PRIVATE
    evo_distributed evo_redis_transport evo_pg_run_store)
  target_compile_options(evo_m39_scaling_test PRIVATE
    -Wall -Wextra -Wpedantic -Werror
    ${EVO_CLANG_HIREDIS_FLAGS}  # hiredis headers (transitive)
  )
  target_compile_definitions(evo_m39_scaling_test PRIVATE
    EVO_REPO_ROOT_DIR="${CMAKE_CURRENT_SOURCE_DIR}/.."
  )
  add_test(NAME m39_scaling COMMAND evo_m39_scaling_test)
  set_tests_properties(m39_scaling PROPERTIES TIMEOUT 600)

  # -------------------------------------------------------------------------
  # evo_m39_chaos_test — M39: infrastructure outage chaos (Appendix T F09-F12).
  # docker pause/unpause the Redis and Postgres containers mid-run; verifies
  # the run reaches a terminal state and records recovery as measured evidence.
  # Skips at runtime when Redis/Postgres/docker/the containers are unavailable.
  # -------------------------------------------------------------------------
  add_executable(evo_m39_chaos_test tests/m39_chaos_test.cpp)
  target_link_libraries(evo_m39_chaos_test PRIVATE
    evo_distributed evo_redis_transport evo_pg_run_store)
  target_compile_options(evo_m39_chaos_test PRIVATE
    -Wall -Wextra -Wpedantic -Werror
    ${EVO_CLANG_HIREDIS_FLAGS}  # hiredis headers (transitive)
  )
  target_compile_definitions(evo_m39_chaos_test PRIVATE
    EVO_REPO_ROOT_DIR="${CMAKE_CURRENT_SOURCE_DIR}/.."
  )
  add_test(NAME m39_chaos COMMAND evo_m39_chaos_test)
  set_tests_properties(m39_chaos PROPERTIES TIMEOUT 600)
endif()
# Point the integration test at the real server binary so it runs instead of
# skipping (the test also honors an EVO_SCHEDULER_SERVER_BIN env override).
if(EVO_HAVE_GRPC)
set_tests_properties(grpc_integration PROPERTIES
  ENVIRONMENT "EVO_SCHEDULER_SERVER_BIN=$<TARGET_FILE:evo-scheduler-server>"
)
# Same for the M36 admission test.
set_tests_properties(quota_admission PROPERTIES
  ENVIRONMENT "EVO_SCHEDULER_SERVER_BIN=$<TARGET_FILE:evo-scheduler-server>"
)
# Same for the M38 auth integration test.
set_tests_properties(auth_integration PROPERTIES
  ENVIRONMENT "EVO_SCHEDULER_SERVER_BIN=$<TARGET_FILE:evo-scheduler-server>"
)
endif()  # EVO_HAVE_GRPC (server-bin test env)
