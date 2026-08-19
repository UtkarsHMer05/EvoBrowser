#pragma once

// Task transport abstraction (Milestone 21).
//
// Moves task dispatch from local callbacks to a pluggable durable transport.
// Scheduler-core tests use InMemoryTransport (a fake); production uses
// RedisTransport (engine/redis/, Redis Streams). The scheduler never
// acknowledges work on behalf of workers: ack() is exposed for the worker
// runtime (M23+), and the scheduler side only publishes and observes.
//
// Stream keys are namespaced by the caller with an explicit environment /
// project prefix, e.g. "evo:dev:tasks" / "evo:dev:results" (see RedisTransport
// helpers). Payloads are serialized TaskEnvelopes (deterministic proto
// encoding; TaskEnvelope has no map fields, so byte output is stable).
//
// Ownership/lifetime: implementations own their connection state; all methods
// are thread-safe unless documented otherwise. Failure semantics: publish/
// read/ack return success flags (or nullopt) rather than throwing, so callers
// can apply bounded retry/backoff policy.

#include <chrono>
#include <cstddef>
#include <deque>
#include <map>
#include <mutex>
#include <optional>
#include <stop_token>
#include <string>
#include <vector>

namespace evo {

// A message as seen on a transport stream.
struct TransportMessage {
  std::string id;      // transport-assigned id (Redis stream id / fake seqno)
  std::string payload; // serialized TaskEnvelope (opaque to the transport)
};

// Abstract durable task transport. All methods must be safe to call from
// multiple threads. Blocking calls honor std::stop_token for shutdown.
class TaskTransport {
 public:
  virtual ~TaskTransport() = default;

  // Idempotently create a consumer group for a stream. Returns true if the
  // group exists (created now or already present) after the call. `start_id`
  // selects the delivery cursor for a NEW group only: "$" delivers only new
  // messages (default), "0" replays from the beginning.
  virtual bool ensure_group(const std::string& stream_key,
                            const std::string& group,
                            const std::string& start_id = "$") = 0;

  // Append a payload to the stream. Returns the assigned message id, or
  // nullopt on failure (caller may retry with bounded backoff).
  virtual std::optional<std::string> publish(const std::string& stream_key,
                                             const std::string& payload) = 0;

  // Blocking read of the next undelivered message for (group, consumer).
  // Returns nullopt on timeout, on stop request, or on transport failure.
  // Delivery marks the message pending until ack() (at-least-once).
  virtual std::optional<TransportMessage> read(
      const std::string& stream_key, const std::string& group,
      const std::string& consumer, std::chrono::milliseconds timeout,
      std::stop_token st = std::stop_token{}) = 0;

  // Acknowledge a delivered message. Only the worker that processed the
  // message may call this (the scheduler never acks on behalf of workers).
  // Idempotent: acking an unknown/already-acked id returns true (harmless).
  virtual bool ack(const std::string& stream_key, const std::string& group,
                   const std::string& message_id) = 0;

  // Number of pending (delivered, not yet acked) entries for a group.
  virtual std::size_t pending_count(const std::string& stream_key,
                                    const std::string& group) = 0;

  // Total messages appended to the stream (diagnostic/test helper).
  virtual std::size_t stream_length(const std::string& stream_key) = 0;
};

// In-memory fake transport for scheduler-core tests. Mirrors Redis Streams
// semantics that matter to the scheduler: append-only streams, per-group
// delivery tracking, pending entries until ack, at-least-once redelivery of
// unacked messages via reclaim_pending(). Thread-safe.
class InMemoryTransport final : public TaskTransport {
 public:
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

  // Redeliver all pending (unacked) messages for a group, as Redis
  // XCLAIM/XPENDING recovery would. Returns the number reclaimed.
  std::size_t reclaim_pending(const std::string& stream_key,
                              const std::string& group,
                              const std::string& consumer);

 private:
  struct Stream {
    std::deque<TransportMessage> messages;
    // group -> next delivery index into messages
    std::map<std::string, std::size_t> group_cursor;
    // group -> pending message ids (delivered, unacked)
    std::map<std::string, std::vector<std::string>> pending;
    // group -> message ids queued for redelivery (reclaim_pending)
    std::map<std::string, std::deque<std::string>> redeliver;
  };

  Stream& stream_locked(const std::string& key);

  mutable std::mutex mu_;
  std::map<std::string, Stream> streams_;
  std::uint64_t next_seq_ = 1;
};

// Namespacing helpers: stream keys carry an explicit environment/project
// prefix so multiple stacks can share one Redis without collision.
std::string task_stream_key(const std::string& env_prefix);
std::string result_stream_key(const std::string& env_prefix);
std::string control_stream_key(const std::string& env_prefix);
// Normalized run events for UI consumers (Milestone 26).
std::string event_stream_key(const std::string& env_prefix);

}  // namespace evo
