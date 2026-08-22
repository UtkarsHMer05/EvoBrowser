#pragma once

// Milestone 38: input size limits + identifier validation at the trust boundary.
//
// The gRPC ControlService is the trust boundary between the authenticated
// server-side app and the scheduler. Before mutating any durable state, the
// service validates that identifiers are well-formed and bounded, and that the
// DAG payload is within a sane size. These helpers are pure (no gRPC
// dependency) so they are unit-testable.
//
// Rules:
//   - Identifiers (run_id, org_id, workflow ids) must be non-empty, at most
//     kMaxIdLength bytes, and contain only [A-Za-z0-9._-]. This rejects control
//     characters, whitespace, and path/JSON metacharacters that could confuse
//     logs, stream keys, or downstream stores.
//   - The DAG JSON payload must be at most kMaxDagJsonBytes. The gRPC server
//     allows 64MB messages, but a workflow DAG is far smaller; bounding it here
//     prevents a single oversized submission from consuming memory before parse.

#include <cstddef>
#include <string>

namespace evo::limits {

// Maximum identifier length (bytes). Generous for UUIDs + prefixes.
inline constexpr std::size_t kMaxIdLength = 256;

// Maximum DAG JSON payload (bytes). 8 MiB is far above any real workflow.
inline constexpr std::size_t kMaxDagJsonBytes = 8 * 1024 * 1024;

// True when `id` is a valid identifier: non-empty, <= kMaxIdLength, and every
// byte is [A-Za-z0-9._-].
bool is_valid_identifier(const std::string& id);

// True when `dag_json` is within the payload size limit.
bool is_dag_size_ok(const std::string& dag_json);

}  // namespace evo::limits
