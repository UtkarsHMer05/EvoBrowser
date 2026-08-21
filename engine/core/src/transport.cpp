#include "evo/transport.hpp"

#include <algorithm>
#include <condition_variable>
#include <cstdint>

namespace evo {

namespace {
// Blocking reads poll under a condition variable so shutdown/stop is honored
// without busy-spinning. The fake has a single global cv; good enough for
// tests.
std::condition_variable_any g_cv;
}  // namespace

std::string task_stream_key(const std::string& env_prefix) {
  return env_prefix + ":tasks";
}
std::string result_stream_key(const std::string& env_prefix) {
  return env_prefix + ":results";
}
std::string control_stream_key(const std::string& env_prefix) {
  return env_prefix + ":control";
}
std::string event_stream_key(const std::string& env_prefix) {
  return env_prefix + ":events";
}

InMemoryTransport::Stream& InMemoryTransport::stream_locked(
    const std::string& key) {
  return streams_[key];
}

bool InMemoryTransport::ensure_group(const std::string& stream_key,
                                     const std::string& group,
                                     const std::string& start_id) {
  (void)start_id;  // the fake always delivers from the beginning
  std::lock_guard lock(mu_);
  auto& s = stream_locked(stream_key);
  s.group_cursor.try_emplace(group, 0);
  s.pending.try_emplace(group);
  return true;
}

std::optional<std::string> InMemoryTransport::publish(
    const std::string& stream_key, const std::string& payload) {
  std::lock_guard lock(mu_);
  auto& s = stream_locked(stream_key);
  TransportMessage msg;
  msg.id = std::to_string(next_seq_++);
  msg.payload = payload;
  s.messages.push_back(msg);
  g_cv.notify_all();
  return msg.id;
}

std::optional<TransportMessage> InMemoryTransport::read(
    const std::string& stream_key, const std::string& group,
    const std::string& consumer, std::chrono::milliseconds timeout,
    std::stop_token st) {
  (void)consumer;  // delivery attribution is not modeled in the fake
  std::unique_lock lock(mu_);
  auto deadline = std::chrono::steady_clock::now() + timeout;
  while (true) {
    if (st.stop_requested()) return std::nullopt;
    auto it = streams_.find(stream_key);
    if (it != streams_.end()) {
      auto& s = it->second;
      // Redeliveries (reclaimed pending messages) take priority, mirroring
      // a worker claiming stale pending entries before reading new work.
      auto red = s.redeliver.find(group);
      if (red != s.redeliver.end() && !red->second.empty()) {
        std::string id = red->second.front();
        red->second.pop_front();
        for (const auto& msg : s.messages) {
          if (msg.id == id) {
            s.pending[group].push_back(id);
            return msg;
          }
        }
        continue;  // message vanished; try next
      }
      auto cur = s.group_cursor.find(group);
      if (cur != s.group_cursor.end() && cur->second < s.messages.size()) {
        const TransportMessage& msg = s.messages[cur->second];
        cur->second++;
        s.pending[group].push_back(msg.id);
        return msg;
      }
    }
    if (g_cv.wait_until(lock, deadline, [&] {
          if (st.stop_requested()) return true;
          auto wit = streams_.find(stream_key);
          if (wit == streams_.end()) return false;
          auto& ws = wit->second;
          auto wr = ws.redeliver.find(group);
          if (wr != ws.redeliver.end() && !wr->second.empty()) return true;
          auto wc = ws.group_cursor.find(group);
          return wc != ws.group_cursor.end() &&
                 wc->second < ws.messages.size();
        })) {
      continue;  // re-check and deliver
    }
    return std::nullopt;  // timeout
  }
}

bool InMemoryTransport::ack(const std::string& stream_key,
                            const std::string& group,
                            const std::string& message_id) {
  std::lock_guard lock(mu_);
  auto it = streams_.find(stream_key);
  if (it == streams_.end()) return true;  // unknown stream: late ack harmless
  auto pend = it->second.pending.find(group);
  if (pend == it->second.pending.end()) return true;
  auto& ids = pend->second;
  auto pos = std::find(ids.begin(), ids.end(), message_id);
  if (pos == ids.end()) return true;  // already acked: idempotent
  ids.erase(pos);
  return true;
}

std::optional<TransportMessage> InMemoryTransport::read_pending(
    const std::string& stream_key, const std::string& group,
    const std::string& consumer, std::stop_token st) {
  (void)consumer;  // delivery attribution is not modeled in the fake
  if (st.stop_requested()) return std::nullopt;
  std::lock_guard lock(mu_);
  auto it = streams_.find(stream_key);
  if (it == streams_.end()) return std::nullopt;
  auto& s = it->second;
  auto pend = s.pending.find(group);
  if (pend == s.pending.end() || pend->second.empty()) return std::nullopt;
  // Return the first pending (delivered, unacked) message. It REMAINS pending
  // until ack(); a recovery loop drains by read_pending -> apply -> ack.
  const std::string& id = pend->second.front();
  for (const auto& msg : s.messages) {
    if (msg.id == id) return msg;
  }
  return std::nullopt;  // pending id vanished from the stream
}

std::size_t InMemoryTransport::pending_count(const std::string& stream_key,
                                             const std::string& group) {
  std::lock_guard lock(mu_);
  auto it = streams_.find(stream_key);
  if (it == streams_.end()) return 0;
  auto pend = it->second.pending.find(group);
  if (pend == it->second.pending.end()) return 0;
  return pend->second.size();
}

std::size_t InMemoryTransport::stream_length(const std::string& stream_key) {
  std::lock_guard lock(mu_);
  auto it = streams_.find(stream_key);
  if (it == streams_.end()) return 0;
  return it->second.messages.size();
}

std::size_t InMemoryTransport::reclaim_pending(const std::string& stream_key,
                                               const std::string& group,
                                               const std::string& consumer) {
  (void)consumer;
  std::lock_guard lock(mu_);
  auto it = streams_.find(stream_key);
  if (it == streams_.end()) return 0;
  auto& s = it->second;
  auto pend = s.pending.find(group);
  if (pend == s.pending.end()) return 0;
  // Rewind the group cursor so pending (unacked) messages are redelivered.
  // Find the earliest pending message index and set the cursor there; pending
  // ids stay pending until acked (they will be re-added on redelivery, so
  // dedupe by clearing first).
  std::size_t reclaimed = pend->second.size();
  if (reclaimed == 0) return 0;
  std::size_t earliest = s.messages.size();
  for (const auto& id : pend->second) {
    for (std::size_t i = 0; i < s.messages.size(); ++i) {
      if (s.messages[i].id == id) {
        earliest = std::min(earliest, i);
        break;
      }
    }
  }
  pend->second.clear();
  s.group_cursor[group] = earliest;
  return reclaimed;
}

}  // namespace evo
