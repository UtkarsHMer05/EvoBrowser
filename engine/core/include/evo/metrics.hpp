#pragma once

// Evidence-grade scheduler metrics (Milestone 13).
//
// Collects per-run aggregate counters and per-node timestamps using
// std::chrono::steady_clock (monotonic — never system_clock for durations, per
// the benchmark integrity contract §14 and C++ engineering contract §8). Metrics
// are decoupled from the ConcurrentRunLog presentation: counters are updated
// on the scheduler's hot path with atomics / cheap max updates under the
// dispatcher lock, and a structured JSON export is available for benchmark
// harnesses *after* the run completes.
//
// Per-node timestamps are recorded in ConcurrentNodeRun (ready_at / started_at
// / finished_at). RunMetrics holds the aggregate counters only.

#include <chrono>
#include <cstddef>
#include <string>

#include "evo/json.hpp"

namespace evo {

struct RunMetrics {
  // Wall-clock-ish (steady) markers for the run-level state machine
  // (ARCHITECTURE.md §6.1). Steady_clock::time_point{} means "not set".
  std::chrono::steady_clock::time_point run_start;
  std::chrono::steady_clock::time_point run_terminal;
  std::chrono::steady_clock::time_point cancel_requested;

  // Exact logical counters (not derived from timestamps).
  std::size_t dispatch_count = 0;    // nodes dispatched to the pool
  std::size_t completion_count = 0;  // nodes completed (success or failure)
  std::size_t max_in_flight = 0;     // high-water mark of in-flight tasks
  std::size_t max_queue_depth = 0;   // high-water mark of ready-queue length
  std::size_t retry_count = 0;       // reserved for M20+ retry model (placeholder)

  // Structured JSON export for benchmark harnesses. Uses milliseconds for
  // human-readable durations (derived from steady_clock, not system_clock).
  std::string to_json_string() const {
    json::Object o;
    using namespace std::chrono;
    auto dur = [this](const std::chrono::steady_clock::time_point& t) {
      if (t == std::chrono::steady_clock::time_point{}) {
        return -1.0;
      }
      return static_cast<double>(
          duration_cast<milliseconds>(t - run_start).count());
    };
    o.emplace("dispatch_count", json::Value(static_cast<double>(dispatch_count)));
    o.emplace("completion_count",
              json::Value(static_cast<double>(completion_count)));
    o.emplace("max_in_flight", json::Value(static_cast<double>(max_in_flight)));
    o.emplace("max_queue_depth",
              json::Value(static_cast<double>(max_queue_depth)));
    o.emplace("retry_count", json::Value(static_cast<double>(retry_count)));
    o.emplace("cancel_requested_at_ms",
              json::Value(dur(cancel_requested)));
    o.emplace("run_terminal_at_ms", json::Value(dur(run_terminal)));
    json::Object doc;
    doc.emplace("metrics", json::Value(std::move(o)));
    return json::serialize(json::Value(std::move(doc)));
  }
};

}  // namespace evo