#!/usr/bin/env bash
# Reproducible protobuf + gRPC C++ binding generation for Milestone 16/17.
#
# Regenerates engine/proto/evo_gen/evo/{execution.pb.{cc,h},
# execution.grpc.pb.{cc,h}} from engine/proto/evo/execution.proto using the
# Homebrew protobuf (protoc) + grpc C++ plugin. Re-run this after any proto
# change; the generated files are committed for reproducible offline builds.
#
# Requirements (verified versions):
#   protoc >= 3.19  (Homebrew: protobuf 35.1)
#   grpc_cpp_plugin (Homebrew: grpc 1.83.0, at /usr/local/bin/grpc_cpp_plugin)
set -euo pipefail
cd "$(dirname "$0")/.."

PROTOC="$(command -v protoc || true)"
PLUGIN=/usr/local/bin/grpc_cpp_plugin
if [[ -z "${PROTOC}" ]]; then
  echo "protoc not found. Install: brew install protobuf" >&2
  exit 1
fi
if [[ ! -x "${PLUGIN}" ]]; then
  echo "grpc_cpp_plugin not found. Install: brew install grpc" >&2
  exit 1
fi

# Well-known types (google/protobuf/timestamp.proto) come from Homebrew.
PROTO_WELLKNOWN="$(brew --prefix)/include"

mkdir -p proto/evo_gen
"${PROTOC}" -I="${PROTO_WELLKNOWN}" -I=proto \
  --cpp_out=proto/evo_gen --proto_path="${PROTO_WELLKNOWN}" --proto_path=proto \
  proto/evo/execution.proto

"${PROTOC}" -I="${PROTO_WELLKNOWN}" -I=proto \
  --grpc_out=proto/evo_gen \
  --plugin=protoc-gen-grpc="${PLUGIN}" \
  --proto_path="${PROTO_WELLKNOWN}" --proto_path=proto \
  proto/evo/execution.proto

echo "regenerated engine/proto/evo_gen/evo/*"
