// M21 unit tests for the in-memory transport fake (evo/transport.hpp).
// Scheduler-core tests use this fake so they never need a live Redis. These
// tests pin the Redis-Streams semantics the fake must mirror: append-only
// publish, per-group FIFO delivery, pending-until-ack, idempotent ack, and
// pending reclaim (redelivery).

#include <atomic>
#include <chrono>
#include <cstdio>
#include <string>
#include <thread>

#include "evo/transport.hpp"

using evo::InMemoryTransport;
using evo::TransportMessage;
using namespace std::chrono_literals;

namespace {
int failures = 0;
void check(bool cond, const char* label) {
  if (cond) {
    printf("  ok   %s\n", label);
  } else {
    printf("  FAIL %s\n", label);
    ++failures;
  }
}
}  // namespace

int main() {
  InMemoryTransport t;
  const std::string stream = evo::task_stream_key("evo:test");
  const std::string group = "workers";

  // --- Namespacing helpers -------------------------------------------------
  check(evo::task_stream_key("evo:dev") == "evo:dev:tasks",
        "task_stream_key namespaced");
  check(evo::result_stream_key("evo:dev") == "evo:dev:results",
        "result_stream_key namespaced");
  check(evo::control_stream_key("evo:dev") == "evo:dev:control",
        "control_stream_key namespaced");

  // --- ensure_group idempotent --------------------------------------------
  check(t.ensure_group(stream, group), "ensure_group first call");
  check(t.ensure_group(stream, group), "ensure_group idempotent second call");

  // --- publish assigns ids, grows stream ----------------------------------
  auto id1 = t.publish(stream, "payload-1");
  auto id2 = t.publish(stream, "payload-2");
  check(id1.has_value() && id2.has_value(), "publish returns ids");
  check(*id1 != *id2, "publish ids are distinct");
  check(t.stream_length(stream) == 2, "stream_length == 2 after two publishes");

  // --- read delivers FIFO and marks pending --------------------------------
  auto m1 = t.read(stream, group, "c1", 100ms);
  check(m1.has_value() && m1->payload == "payload-1", "read delivers FIFO #1");
  check(t.pending_count(stream, group) == 1, "pending == 1 after first read");
  auto m2 = t.read(stream, group, "c1", 100ms);
  check(m2.has_value() && m2->payload == "payload-2", "read delivers FIFO #2");
  check(t.pending_count(stream, group) == 2, "pending == 2 after second read");

  // --- read on empty stream times out (no busy-spin hang) ------------------
  auto m3 = t.read(stream, group, "c1", 50ms);
  check(!m3.has_value(), "read on empty stream returns nullopt (timeout)");

  // --- ack removes from pending; idempotent --------------------------------
  check(t.ack(stream, group, m1->id), "ack first message");
  check(t.pending_count(stream, group) == 1, "pending == 1 after ack");
  check(t.ack(stream, group, m1->id), "double ack is harmless (idempotent)");
  check(t.pending_count(stream, group) == 1, "pending unchanged by double ack");
  check(t.ack(stream, group, "nonexistent-id"), "late/unknown ack harmless");

  // --- reclaim_pending redelivers unacked ----------------------------------
  std::size_t reclaimed = t.reclaim_pending(stream, group, "c2");
  check(reclaimed == 1, "reclaim_pending returns 1 unacked");
  auto redelivered = t.read(stream, group, "c2", 100ms);
  check(redelivered.has_value() && redelivered->payload == "payload-2",
        "reclaimed message redelivered");
  check(t.ack(stream, group, redelivered->id), "ack redelivered message");
  check(t.pending_count(stream, group) == 0, "pending == 0 after full ack");

  // --- read honors stop_token ----------------------------------------------
  std::stop_source src;
  src.request_stop();
  auto stopped = t.read(stream, group, "c1", 5s, src.get_token());
  check(!stopped.has_value(), "read returns nullopt when stop requested");

  // --- concurrent publish/read is race-free (smoke) -------------------------
  {
    InMemoryTransport ct;
    const std::string cs = evo::task_stream_key("evo:conc");
    ct.ensure_group(cs, "g");
    std::atomic<int> produced{0};
    std::atomic<int> consumed{0};
    std::stop_source stop;
    std::thread producer([&] {
      for (int i = 0; i < 200; ++i) {
        if (ct.publish(cs, "m" + std::to_string(i))) produced++;
      }
      stop.request_stop();
    });
    std::thread consumer([&] {
      while (consumed < 200) {
        auto msg = ct.read(cs, "g", "c", 20ms, stop.get_token());
        if (msg) {
          ct.ack(cs, "g", msg->id);
          consumed++;
        } else if (stop.stop_requested() &&
                   ct.pending_count(cs, "g") == 0 &&
                   consumed < 200) {
          // Producer done; drain any remaining.
          auto tail = ct.read(cs, "g", "c", 20ms);
          if (!tail) break;
          ct.ack(cs, "g", tail->id);
          consumed++;
        }
      }
    });
    producer.join();
    consumer.join();
    check(produced == 200 && consumed == 200,
          "concurrent publish/read: 200/200 delivered+acked");
    check(ct.pending_count(cs, "g") == 0, "concurrent: no leaked pending");
  }

  if (failures == 0) {
    printf("\nALL TRANSPORT (IN-MEMORY) TESTS PASSED\n");
    return 0;
  }
  printf("\n%d TRANSPORT TEST(S) FAILED\n", failures);
  return 1;
}
