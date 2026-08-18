# Building the Evo Engine (C++20)

The engine lives in `engine/` and is fully separate from the Next.js app —
building it never touches the Phase-1 product.

## Prerequisites (verified on the reference machine, 2026-08-18)

| Tool | Minimum | Reference machine |
| :--- | :--- | :--- |
| CMake | 3.25 | 4.2.1 |
| C++20 compiler with `std::jthread` | clang 14+ / gcc 11+ | Apple clang 21.0.0 |
| Ninja (recommended) or Make | any recent | ninja (Homebrew) |
| Platform | macOS arm64 / Linux x86_64+arm64 | macOS darwin 25.5.0 arm64 |

No global installs are performed by the build. Third-party C++ dependencies
(gRPC, Redis client, Postgres client) are **not** required until Milestone 16+
and will be pinned via a manifest strategy decided at that milestone; the core
engine (M04–M15) builds with the standard library only.

## Configure, build, test

```bash
cd engine

# Configure (Release is the default build type for any performance work)
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release

# Build
cmake --build build

# Run tests
ctest --test-dir build --output-on-failure

# Smoke binary — prints version/build metadata only
./build/evo-smoke
```

Example smoke output:

```text
evo-engine v0.1.0 (Release) commit=a3e3210 compiler=clang 21.0.0 cxx=202002 jthread=yes
```

## Layout

```text
engine/
├── CMakeLists.txt          # evo_scheduler_core lib + evo-smoke + CTest
├── core/
│   ├── include/evo/        # public engine headers
│   └── src/                # engine implementation
├── app/                    # executables (smoke today; scheduler service later)
├── tests/                  # CTest unit tests
└── benchmarks/             # benchmark corpus + results (M15+)
```

## Build rules (binding)

- C++20, no compiler extensions (`CMAKE_CXX_EXTENSIONS OFF`).
- Project-owned code compiles with `-Wall -Wextra -Wpedantic -Werror`.
- Performance numbers are only ever produced from `Release` builds
  (`BENCHMARK_METHODOLOGY.md`); Debug builds are for development and
  sanitizers.
- Build trees (`engine/build*/`), caches, and benchmark result artifacts are
  git-ignored; never commit them.
- The configure step captures the git SHA into the binary (`EVO_BUILD_COMMIT`)
  so every engine build is attributable to a commit.

## Sanitizers (Milestone 14)

Sanitizer builds use a separate build directory so they never mix with
Release artifacts:

```bash
cmake -S . -B build-asan -G Ninja -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_CXX_FLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer"
cmake --build build-asan
ctest --test-dir build-asan --output-on-failure
```

(ThreadSanitizer runs use `-fsanitize=thread` in their own `build-tsan`
directory; ASan and TSan are never combined.)
