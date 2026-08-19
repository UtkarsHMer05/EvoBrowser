#!/usr/bin/env bash
# Build a local, self-contained arm64 static hiredis for the Phase-2 engine
# (Milestone 21). Avoids depending on whichever Homebrew prefix/arch is on the
# machine: the engine links engine/third_party/hiredis-prefix/lib/libhiredis.a.
#
# Re-run this script to rebuild; outputs land in engine/third_party/
# hiredis-prefix/ (gitignored). Requires: git, make, a C compiler.

set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SRC="${HERE}/hiredis-src"
PREFIX="${HERE}/hiredis-prefix"
VERSION="v1.4.1"

if [ ! -d "${SRC}" ]; then
  echo "==> Cloning hiredis ${VERSION}"
  git clone --depth 1 --branch "${VERSION}" https://github.com/redis/hiredis.git "${SRC}"
fi

echo "==> Building static libhiredis (arm64)"
cd "${SRC}"
make clean >/dev/null 2>&1 || true
make -j8 static ARCHFLAGS="-arch arm64"

echo "==> Installing to ${PREFIX}"
mkdir -p "${PREFIX}/lib" "${PREFIX}/include/hiredis"
cp libhiredis.a "${PREFIX}/lib/"
cp *.h "${PREFIX}/include/hiredis/"

echo "==> Done."
file "${PREFIX}/lib/libhiredis.a"
