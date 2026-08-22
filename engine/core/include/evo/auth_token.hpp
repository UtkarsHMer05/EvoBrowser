#pragma once

// Milestone 38: service-to-service engine-token authentication helpers.
//
// The scheduler's gRPC ControlService is reachable only from the authenticated
// server-side app (never a browser). To defend against a misconfigured or
// non-loopback exposure, M38 adds an OPTIONAL shared engine token:
//   - Server: when EVO_ENGINE_TOKEN is set (non-empty), every RPC must carry
//     `authorization: Bearer <token>` metadata matching it, else UNAUTHENTICATED.
//     When unset/empty, auth is disabled (backwards compatible; the default
//     deployment binds loopback only).
//   - Client: the server-only gRPC client reads EVO_ENGINE_TOKEN from its own
//     environment and attaches it as call metadata. The token never reaches a
//     browser (M38 no-go).
//
// These helpers are pure (no gRPC dependency) so they are unit-testable:
//   - constant_time_equals: fixed-time string comparison (no early exit on the
//     first differing byte) to avoid leaking the token via timing.
//   - extract_bearer: pull the token out of an "Authorization: Bearer <tok>"
//     header value, tolerating case and surrounding whitespace.

#include <string>

namespace evo::auth {

// Constant-time equality. Compares every byte regardless of where a mismatch
// occurs; returns false for differing lengths (after still scanning `a`).
bool constant_time_equals(const std::string& a, const std::string& b);

// Extract the bearer token from an Authorization header value.
// Accepts "Bearer <token>" (case-insensitive scheme), trims whitespace.
// Returns an empty string when the value is not a bearer header.
std::string extract_bearer(const std::string& header_value);

}  // namespace evo::auth
