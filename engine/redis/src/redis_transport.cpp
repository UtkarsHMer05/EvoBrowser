#include "evo/redis_transport.hpp"

#include <cstring>
#include <thread>

#include <hiredis/hiredis.h>

namespace evo {

namespace {

// hiredis reply helpers. All replies are freed by the caller via
// freeReplyObject; these only read.
bool reply_is_error(const redisReply* r) {
  return r != nullptr && r->type == REDIS_REPLY_ERROR;
}

// BUSYGROUP means the consumer group already exists — that is success for an
// idempotent ensure_group.
bool reply_is_busygroup(const redisReply* r) {
  return reply_is_error(r) && r->str != nullptr &&
         std::strstr(r->str, "BUSYGROUP") != nullptr;
}

}  // namespace

RedisTransport::RedisTransport(RedisTransportConfig config)
    : config_(std::move(config)) {}

RedisTransport::~RedisTransport() {
  std::lock_guard lock(mu_);
  if (ctx_ != nullptr) {
    redisFree(ctx_);
    ctx_ = nullptr;
  }
}

bool RedisTransport::connect() {
  std::lock_guard lock(mu_);
  return ensure_connected_locked();
}

bool RedisTransport::connected() const {
  std::lock_guard lock(mu_);
  return ctx_ != nullptr && ctx_->err == 0;
}

bool RedisTransport::ensure_connected_locked() {
  if (ctx_ != nullptr && ctx_->err == 0) return true;
  if (ctx_ != nullptr) {
    redisFree(ctx_);
    ctx_ = nullptr;
  }
  struct timeval tv;
  tv.tv_sec = static_cast<long>(config_.connect_timeout.count() / 1000);
  tv.tv_usec = static_cast<long>((config_.connect_timeout.count() % 1000) * 1000);
  ctx_ = redisConnectWithTimeout(config_.host.c_str(), config_.port, tv);
  if (ctx_ == nullptr || ctx_->err != 0) {
    if (ctx_ != nullptr) {
      redisFree(ctx_);
      ctx_ = nullptr;
    }
    return false;
  }
  return true;
}

namespace {
// Central retry loop. On a connection-level error (null reply / ctx->err),
// drop the connection and retry after bounded exponential backoff. On a
// server-side error reply (non-null), return it to the caller without
// retrying — the command reached Redis. Returns nullptr only when retries are
// exhausted.
template <typename Fn>
void* run_with_retry(const RedisTransportConfig& config, redisContext*& ctx,
                     Fn&& fn) {
  auto backoff = config.backoff_base;
  for (int attempt = 0; attempt <= config.max_retries; ++attempt) {
    // Ensure a live connection.
    if (ctx == nullptr || ctx->err != 0) {
      if (ctx != nullptr) {
        redisFree(ctx);
        ctx = nullptr;
      }
      struct timeval tv;
      tv.tv_sec = static_cast<long>(config.connect_timeout.count() / 1000);
      tv.tv_usec =
          static_cast<long>((config.connect_timeout.count() % 1000) * 1000);
      ctx = redisConnectWithTimeout(config.host.c_str(), config.port, tv);
      if (ctx == nullptr || ctx->err != 0) {
        if (ctx != nullptr) {
          redisFree(ctx);
          ctx = nullptr;
        }
        if (attempt == config.max_retries) return nullptr;
        std::this_thread::sleep_for(backoff);
        backoff = std::min(backoff * 2, config.backoff_cap);
        continue;
      }
    }

    void* reply = fn(ctx);
    if (reply != nullptr) {
      // A non-null reply means Redis answered (possibly with an error reply).
      // Connection is healthy; return to caller.
      return reply;
    }
    // Null reply => connection error. Drop and back off.
    if (ctx != nullptr) {
      redisFree(ctx);
      ctx = nullptr;
    }
    if (attempt == config.max_retries) return nullptr;
    std::this_thread::sleep_for(backoff);
    backoff = std::min(backoff * 2, config.backoff_cap);
  }
  return nullptr;
}
}  // namespace

bool RedisTransport::ensure_group(const std::string& stream_key,
                                  const std::string& group,
                                  const std::string& start_id) {
  std::lock_guard lock(mu_);
  void* raw = run_with_retry(config_, ctx_, [&](redisContext* c) {
    return static_cast<void*>(redisCommand(
        c, "XGROUP CREATE %s %s %s MKSTREAM", stream_key.c_str(),
        group.c_str(), start_id.c_str()));
  });
  if (raw == nullptr) return false;
  auto* reply = static_cast<redisReply*>(raw);
  const bool ok =
      reply->type == REDIS_REPLY_STATUS || reply_is_busygroup(reply);
  freeReplyObject(raw);
  return ok;
}

std::optional<std::string> RedisTransport::publish(const std::string& stream_key,
                                                   const std::string& payload) {
  std::lock_guard lock(mu_);
  // Binary-safe XADD via argv with explicit lengths.
  const char* argv[5] = {"XADD", stream_key.c_str(), "*", "payload",
                         payload.c_str()};
  size_t argvlen[5] = {4, stream_key.size(), 1, 7, payload.size()};
  void* raw = run_with_retry(config_, ctx_, [&](redisContext* c) {
    return redisCommandArgv(c, 5, argv, argvlen);
  });
  if (raw == nullptr) return std::nullopt;
  auto* reply = static_cast<redisReply*>(raw);
  std::optional<std::string> id;
  if (reply->type == REDIS_REPLY_STRING && reply->str != nullptr) {
    id = std::string(reply->str, reply->len);
  }
  freeReplyObject(raw);
  return id;
}

std::optional<TransportMessage> RedisTransport::read(
    const std::string& stream_key, const std::string& group,
    const std::string& consumer, std::chrono::milliseconds timeout,
    std::stop_token st) {
  // XREADGROUP blocks server-side; to honor stop_token we use short block
  // slices and poll the token between them.
  const auto slice = std::chrono::milliseconds(100);
  auto deadline = std::chrono::steady_clock::now() + timeout;
  while (true) {
    if (st.stop_requested()) return std::nullopt;
    auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
        deadline - std::chrono::steady_clock::now());
    if (remaining <= std::chrono::milliseconds::zero()) return std::nullopt;
    auto block = std::min(slice, remaining);

    std::optional<TransportMessage> got;
    {
      std::lock_guard lock(mu_);
      void* raw = run_with_retry(config_, ctx_, [&](redisContext* c) {
        return static_cast<void*>(redisCommand(
            c, "XREADGROUP GROUP %s %s COUNT 1 BLOCK %lld STREAMS %s >",
            group.c_str(), consumer.c_str(),
            static_cast<long long>(block.count()), stream_key.c_str()));
      });
      if (raw == nullptr) {
        // Connection failure after retries: surface as no-message; caller may
        // retry. Avoid tight loop by sleeping one slice.
        std::this_thread::sleep_for(slice);
        continue;
      }
      auto* reply = static_cast<redisReply*>(raw);
      // Reply shape: array of streams -> [ [stream, [ [id, [f,v,...]] ] ] ]
      if (reply->type == REDIS_REPLY_ARRAY && reply->elements > 0) {
        redisReply* stream_entry = reply->element[0];
        if (stream_entry->type == REDIS_REPLY_ARRAY &&
            stream_entry->elements >= 2) {
          redisReply* messages = stream_entry->element[1];
          if (messages->type == REDIS_REPLY_ARRAY && messages->elements > 0) {
            redisReply* msg = messages->element[0];
            if (msg->type == REDIS_REPLY_ARRAY && msg->elements >= 2 &&
                msg->element[0]->type == REDIS_REPLY_STRING) {
              TransportMessage m;
              m.id = std::string(msg->element[0]->str, msg->element[0]->len);
              // fields: [ "payload", <bytes> ]
              redisReply* fields = msg->element[1];
              if (fields->type == REDIS_REPLY_ARRAY &&
                  fields->elements >= 2 &&
                  fields->element[1]->type == REDIS_REPLY_STRING) {
                m.payload = std::string(fields->element[1]->str,
                                        fields->element[1]->len);
              }
              got = std::move(m);
            }
          }
        }
      }
      freeReplyObject(raw);
    }
    if (got.has_value()) return got;
    // No message in this slice; loop honors stop_token + deadline.
  }
}

bool RedisTransport::ack(const std::string& stream_key, const std::string& group,
                         const std::string& message_id) {
  std::lock_guard lock(mu_);
  void* raw = run_with_retry(config_, ctx_, [&](redisContext* c) {
    return static_cast<void*>(redisCommand(c, "XACK %s %s %s",
                                           stream_key.c_str(), group.c_str(),
                                           message_id.c_str()));
  });
  if (raw == nullptr) return false;
  auto* reply = static_cast<redisReply*>(raw);
  // XACK returns the count of acked entries (0 or 1). A late/unknown ack
  // returns 0 and is harmless; treat both as success.
  const bool ok = reply->type == REDIS_REPLY_INTEGER;
  freeReplyObject(raw);
  return ok;
}

std::size_t RedisTransport::pending_count(const std::string& stream_key,
                                          const std::string& group) {
  std::lock_guard lock(mu_);
  void* raw = run_with_retry(config_, ctx_, [&](redisContext* c) {
    return static_cast<void*>(
        redisCommand(c, "XPENDING %s %s", stream_key.c_str(), group.c_str()));
  });
  if (raw == nullptr) return 0;
  auto* reply = static_cast<redisReply*>(raw);
  std::size_t count = 0;
  // XPENDING summary: [ count, min-id, max-id, [ [consumer, n] ... ] ]
  if (reply->type == REDIS_REPLY_ARRAY && reply->elements >= 1 &&
      reply->element[0]->type == REDIS_REPLY_INTEGER) {
    count = static_cast<std::size_t>(reply->element[0]->integer);
  }
  freeReplyObject(raw);
  return count;
}

std::size_t RedisTransport::stream_length(const std::string& stream_key) {
  std::lock_guard lock(mu_);
  void* raw = run_with_retry(config_, ctx_, [&](redisContext* c) {
    return static_cast<void*>(redisCommand(c, "XLEN %s", stream_key.c_str()));
  });
  if (raw == nullptr) return 0;
  auto* reply = static_cast<redisReply*>(raw);
  std::size_t len = 0;
  if (reply->type == REDIS_REPLY_INTEGER) {
    len = static_cast<std::size_t>(reply->integer);
  }
  freeReplyObject(raw);
  return len;
}

}  // namespace evo
