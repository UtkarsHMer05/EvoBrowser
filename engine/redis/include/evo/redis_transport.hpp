#pragma once

// Redis Streams task transport (Milestone 21).
//
// Implements TaskTransport over Redis Streams using hiredis (synchronous
// API). Semantics mirror the in-memory fake but backed by a durable,
// append-only log:
//
//   ensure_group  -> XGROUP CREATE <stream> <group> $ MKSTREAM (idempotent;
//                    BUSYGROUP is treated as success)
//   publish       -> XADD <stream> * payload <bytes>
//   read          -> XREADGROUP GROUP <group> <consumer> COUNT 1 BLOCK <ms>
//                    STREAMS <stream> >   (at-least-once; message is pending
//                    until ack)
//   ack           -> XACK <stream> <group> <id>   (workers only; the
//                    scheduler never acks on behalf of workers)
//   pending_count -> XPENDING <stream> <group> summary count
//   stream_length -> XLEN <stream>
//
// Connection handling: a single redisContext guarded by a mutex (hiredis
// contexts are not thread-safe). On command failure the transport reconnects
// with bounded exponential backoff (base 50ms, cap 2s, max 5 attempts per
// operation). If the operation still fails it returns failure (nullopt/false)
// and the caller applies its own retry policy.
//
// Payloads are opaque byte strings. Callers publish deterministically-encoded
// TaskEnvelopes (proto SerializeToString is byte-stable for TaskEnvelope: it
// has no map fields). Timestamps inside envelopes are wall-clock UTC.
//
// Keys are namespaced by the caller via task_stream_key()/result_stream_key()/
// control_stream_key() with an explicit environment/project prefix.

#include <chrono>
#include <memory>
#include <mutex>
#include <string>

#include "evo/transport.hpp"

struct redisContext;  // hiredis forward declaration

namespace evo {

struct RedisTransportConfig {
  std::string host = "127.0.0.1";
  int port = 6390;  // Phase-2 local default (scripts/phase2)
  std::chrono::milliseconds connect_timeout{2000};
  // Bounded reconnect backoff per operation.
  int max_retries = 5;
  std::chrono::milliseconds backoff_base{50};
  std::chrono::milliseconds backoff_cap{2000};
};

class RedisTransport final : public TaskTransport {
 public:
  explicit RedisTransport(RedisTransportConfig config = {});
  ~RedisTransport() override;

  RedisTransport(const RedisTransport&) = delete;
  RedisTransport& operator=(const RedisTransport&) = delete;

  // Attempt the initial connection. Returns true if connected. The transport
  // also reconnects lazily on demand, so calling this is optional but lets
  // callers fail fast / report status.
  bool connect();

  bool connected() const;

  bool ensure_group(const std::string& stream_key,
                    const std::string& group,
                    const std::string& start_id = "$") override;

  std::optional<std::string> publish(const std::string& stream_key,
                                     const std::string& payload) override;

  std::optional<TransportMessage> read(const std::string& stream_key,
                                       const std::string& group,
                                       const std::string& consumer,
                                       std::chrono::milliseconds timeout,
                                       std::stop_token st =
                                           std::stop_token{}) override;

  bool ack(const std::string& stream_key, const std::string& group,
           const std::string& message_id) override;

  std::size_t pending_count(const std::string& stream_key,
                            const std::string& group) override;

  std::size_t stream_length(const std::string& stream_key) override;

 private:
  bool ensure_connected_locked();

  RedisTransportConfig config_;
  mutable std::mutex mu_;
  redisContext* ctx_ = nullptr;  // owned; guarded by mu_
};

}  // namespace evo
