#!/usr/bin/env bash
# Build a local, self-contained static hiredis for the Phase-2 engine
# (Milestone 21). Avoids depending on whichever Homebrew prefix/arch is on the
# machine: the engine links engine/third_party/hiredis-prefix/lib/libhiredis.a.
#
# Re-run this script to rebuild; outputs land in engine/third_party/
# hiredis-prefix/ (gitignored). Requires: git, make, a C compiler.
#
# Platform-aware (M38 CI): on macOS it pins the host architecture via
# ARCHFLAGS; on Linux it builds for the native arch (no ARCHFLAGS).

set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SRC="${HERE}/hiredis-src"
PREFIX="${HERE}/hiredis-prefix"
VERSION="v1.4.1"

if [ ! -d "${SRC}" ]; then
  echo "==> Cloning hiredis ${VERSION}"
  git clone --depth 1 --branch "${VERSION}" https://github.com/redis/hiredis.git "${SRC}"
fi

OS="$(uname -s)"
ARCH="$(uname -m)"
if [ "${OS}" = "Darwin" ]; then
  echo "==> Building static libhiredis (macOS ${ARCH})"
  # Pin the host architecture. Exported as an env var (not a make argument) so
  # the value is not word-split into a bogus make target.
  export ARCHFLAGS="-arch ${ARCH}"
else
  echo "==> Building static libhiredis (${OS} ${ARCH})"
fi

cd "${SRC}"
make clean >/dev/null 2>&1 || true
make -j8 static

echo "==> Installing to ${PREFIX}"
mkdir -p "${PREFIX}/lib" "${PREFIX}/include/hiredis"
cp libhiredis.a "${PREFIX}/lib/"
cp *.h "${PREFIX}/include/hiredis/"

echo "==> Done."
# `file` is informational only and absent on minimal CI images; don't fail.
if command -v file >/dev/null 2>&1; then
  file "${PREFIX}/lib/libhiredis.a"
fi
