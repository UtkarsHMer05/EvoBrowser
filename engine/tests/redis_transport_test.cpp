// M21 integration test: Redis Streams transport against the LOCAL Phase-2
// Redis container (scripts/phase2/up.sh). Exercises enqueue/read/pending/ack
// and duplicate message-id/payload handling, plus deterministic TaskEnvelope
// encoding round-trip through the transport.
//
// Skips (exit 0) when the local Redis is unreachable, so the engine CTest
// suite stays green on machines without the Phase-2 stack. Set
// EVO_PHASE2_REDIS=host:port to override (default 127.0.0.1:6390).

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <string>

#include "evo/execution.pb.h"
#include "evo/redis_transport.hpp"
#include "evo/transport.hpp"

using evo::RedisTransport;
using evo::RedisTransportConfig;
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

// Deterministic TaskEnvelope encoding (proto SerializeToString is byte-stable
// for TaskEnvelope: no map fields).
std::string encode_envelope(const std::string& run_id,
                            const std::string& node_id, int attempt) {
  evo::execution::v1::TaskEnvelope env;
  env.set_run_id(run_id);
  env.set_node_id(node_id);
  env.set_attempt_number(static_cast<unsigned>(attempt));
  env.set_org_id("org_m21");
  env.set_node_type("bench:sleep");
  env.set_node_payload_json("{\"ms\":3}");
  env.set_resource_class(evo::execution::v1::INTERNAL);
  return env.SerializeAsString();
}
}  // namespace

int main() {
  // Parse host:port from EVO_PHASE2_REDIS (default 127.0.0.1:6390).
  std::string endpoint = "127.0.0.1:6390";
  if (const char* env = std::getenv("EVO_PHASE2_REDIS")) endpoint = env;
  auto colon = endpoint.find(':');
  RedisTransportConfig cfg;
  cfg.host = endpoint.substr(0, colon);
  cfg.port = colon == std::string::npos ? 6390
                                        : std::atoi(endpoint.c_str() + colon + 1);
  cfg.max_retries = 1;  // fail fast for the reachability probe

  RedisTransport t(cfg);
  if (!t.connect()) {
    printf("SKIP: M21 redis_transport (local Redis unreachable at %s; run "
           "scripts/phase2/up.sh to enable)\n",
           endpoint.c_str());
    return 0;
  }
  printf("  ok   connected to Redis at %s\n", endpoint.c_str());

  // Use a unique namespaced stream per run so re-runs are hermetic.
  const std::string prefix =
      "evo:m21test:" + std::to_string(std::chrono::steady_clock::now()
                                          .time_since_epoch()
                                          .count());
  const std::string stream = evo::task_stream_key(prefix);
  const std::string group = "workers";

  // --- ensure_group idempotent --------------------------------------------
  check(t.ensure_group(stream, group), "ensure_group creates group");
  check(t.ensure_group(stream, group), "ensure_group idempotent (BUSYGROUP ok)");

  // --- enqueue: deterministic TaskEnvelope payloads ------------------------
  std::string p1 = encode_envelope("run-a", "n0", 1);
  std::string p2 = encode_envelope("run-a", "n1", 1);
  auto id1 = t.publish(stream, p1);
  auto id2 = t.publish(stream, p2);
  check(id1.has_value() && id2.has_value(), "XADD returns stream ids");
  check(*id1 != *id2, "stream ids distinct");
  check(t.stream_length(stream) == 2, "XLEN == 2");

  // Deterministic encoding: same envelope -> identical bytes.
  check(encode_envelope("run-a", "n0", 1) == p1,
        "TaskEnvelope encoding is deterministic");

  // --- read: at-least-once delivery, pending until ack ---------------------
  auto m1 = t.read(stream, group, "c1", 500ms);
  check(m1.has_value(), "XREADGROUP delivers a message");
  check(m1 && m1->payload == p1, "delivered payload matches envelope bytes");
  check(t.pending_count(stream, group) == 1, "XPENDING == 1 after read");

  // Decode round-trip through the transport.
  if (m1) {
    evo::execution::v1::TaskEnvelope decoded;
    check(decoded.ParseFromString(m1->payload), "envelope parses from payload");
    check(decoded.run_id() == "run-a" && decoded.node_id() == "n0",
          "decoded envelope fields preserved");
  }

  // --- duplicate payload handling: same bytes, distinct stream ids ---------
  auto dup_id = t.publish(stream, p1);  // identical payload to message 1
  check(dup_id.has_value() && *dup_id != *id1,
        "duplicate payload gets a NEW stream id (transport does not dedupe)");
  // App-level dedupe is by (run_id,node_id,attempt) inside the envelope, not
  // by transport id — assert the bytes are identical so the worker can dedupe.
  check(t.stream_length(stream) == 3, "XLEN == 3 after duplicate publish");

  // --- ack: removes pending; late/duplicate ack harmless -------------------
  check(t.ack(stream, group, m1->id), "XACK first message");
  check(t.pending_count(stream, group) == 0, "XPENDING == 0 after ack");
  check(t.ack(stream, group, m1->id), "duplicate ack harmless");
  check(t.ack(stream, group, "0-0"), "unknown/late ack harmless");

  // --- drain remaining (incl. duplicate) and ack all -----------------------
  int drained = 0;
  while (auto m = t.read(stream, group, "c1", 200ms)) {
    t.ack(stream, group, m->id);
    ++drained;
  }
  check(drained == 2, "drained remaining 2 messages");
  check(t.pending_count(stream, group) == 0, "no leaked pending after drain");

  // --- cleanup: drop the hermetic test stream ------------------------------
  // (DEL via a throwaway publish/read is not available; leave the namespaced
  //  stream — it is isolated under evo:m21test:* and harmless.)

  if (failures == 0) {
    printf("\nALL M21 REDIS TRANSPORT TESTS PASSED\n");
    return 0;
  }
  printf("\n%d M21 REDIS TRANSPORT TEST(S) FAILED\n", failures);
  return 1;
}
