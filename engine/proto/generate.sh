#!/usr/bin/env bash
# Reproducible protobuf + gRPC C++ binding generation for Milestone 16/17.
#
# Regenerates engine/proto/evo_gen/evo/{execution.pb.{cc,h},
# execution.grpc.pb.{cc,h}} from engine/proto/evo/execution.proto using the
# installed protobuf (protoc) + grpc C++ plugin. Re-run this after any proto
# change; the generated files are committed for reproducible offline builds.
#
# Platform-aware (M38 CI): on macOS it uses Homebrew protobuf + grpc; on Linux
# (CI) it uses the system protoc + grpc_cpp_plugin. The committed gencode is
# pinned to the exact protobuf runtime it was generated with (see the
# PROTOBUF_VERSION guard in proto/evo_gen/evo/execution.pb.h), so CI runners
# regenerate gencode against their own protobuf before building the proto layer.
#
# Requirements (verified versions):
#   protoc >= 3.19  (Homebrew: protobuf 35.1; Ubuntu 24.04: libprotobuf-dev)
#   grpc_cpp_plugin (Homebrew: grpc 1.83.0; Ubuntu: grpc++ / protobuf-compiler-grpc)
#
# Usage:
#   generate.sh                 # regenerate messages + gRPC stubs (default)
#   generate.sh --messages-only # regenerate only execution.pb.{cc,h}; used by
#                               # the distributed CI job, which builds the
#                               # messages-only layer and never needs gRPC.
set -euo pipefail
cd "$(dirname "$0")/.."

MESSAGES_ONLY=0
if [ "${1:-}" = "--messages-only" ]; then
  MESSAGES_ONLY=1
fi

PROTOC="$(command -v protoc || true)"
if [[ -z "${PROTOC}" ]]; then
  echo "protoc not found. macOS: brew install protobuf; Linux: apt-get install protobuf-compiler" >&2
  exit 1
fi

# Locate the grpc C++ plugin (only needed for the full generation). Honor an
# explicit override, then probe common install locations for the platform.
PLUGIN="${EVO_GRPC_CPP_PLUGIN:-}"
if [[ "${MESSAGES_ONLY}" -eq 0 && -z "${PLUGIN}" ]]; then
  for cand in \
    "$(command -v grpc_cpp_plugin || true)" \
    /usr/local/bin/grpc_cpp_plugin \
    /opt/homebrew/bin/grpc_cpp_plugin \
    /usr/bin/grpc_cpp_plugin; do
    if [[ -n "${cand}" && -x "${cand}" ]]; then
      PLUGIN="${cand}"
      break
    fi
  done
fi
if [[ "${MESSAGES_ONLY}" -eq 0 && -z "${PLUGIN}" ]]; then
  echo "grpc_cpp_plugin not found. macOS: brew install grpc; Linux: apt-get install protobuf-compiler-grpc" >&2
  exit 1
fi

# Well-known types (google/protobuf/timestamp.proto). On macOS they live under
# the Homebrew prefix; on Linux they ship with libprotobuf-dev in /usr/include.
if command -v brew >/dev/null 2>&1; then
  PROTO_WELLKNOWN="$(brew --prefix)/include"
else
  PROTO_WELLKNOWN="/usr/include"
fi
if [[ ! -f "${PROTO_WELLKNOWN}/google/protobuf/timestamp.proto" ]]; then
  echo "google/protobuf/timestamp.proto not found under ${PROTO_WELLKNOWN}" >&2
  echo "macOS: brew install protobuf; Linux: apt-get install libprotobuf-dev" >&2
  exit 1
fi

mkdir -p proto/evo_gen
"${PROTOC}" -I="${PROTO_WELLKNOWN}" -I=proto \
  --cpp_out=proto/evo_gen --proto_path="${PROTO_WELLKNOWN}" --proto_path=proto \
  proto/evo/execution.proto

if [[ "${MESSAGES_ONLY}" -eq 1 ]]; then
  echo "regenerated engine/proto/evo_gen/evo/execution.pb.{cc,h} (messages only; protoc: $("${PROTOC}" --version))"
  exit 0
fi

"${PROTOC}" -I="${PROTO_WELLKNOWN}" -I=proto \
  --grpc_out=proto/evo_gen \
  --plugin=protoc-gen-grpc="${PLUGIN}" \
  --proto_path="${PROTO_WELLKNOWN}" --proto_path=proto \
  proto/evo/execution.proto

echo "regenerated engine/proto/evo_gen/evo/* (protoc: $("${PROTOC}" --version))"
