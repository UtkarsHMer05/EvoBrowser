#pragma once

// Milestone 38: a small thread-safe metrics registry for the scheduler service.
//
// Holds named counters and gauges (optionally labeled) and renders them as a
// complete Prometheus text exposition document. The gRPC scheduler service
// increments counters on its hot path (submit/accept/reject/terminal) and sets
// gauges (active runs), then serves the rendered text at an HTTP /metrics
// endpoint (engine/app/metrics_http.hpp).
//
// Design notes:
//   - One mutex guards the whole registry. Metrics updates are cheap map
//     inserts/increments off the dispatch hot path; contention is negligible.
//   - Metric families are declared (type + help) on first use; render() emits
//     HELP/TYPE once per family followed by all its labeled samples.
//   - Counters only ever increase; gauges may be set to any value.

#include <map>
#include <mutex>
#include <string>
#include <vector>

#include "evo/prometheus.hpp"

namespace evo::metrics {

class MetricsRegistry {
 public:
  // Increment a counter by `delta` (>= 0). Declares the family on first use.
  void counter_add(const std::string& name, const std::string& help,
                   const prometheus::Labels& labels, double delta = 1.0);

  // Set a counter to an ABSOLUTE cumulative value. For sources that already
  // track a monotonically-increasing total (e.g. the quota gate's counters),
  // so they render as Prometheus counters without double-counting deltas.
  void counter_set(const std::string& name, const std::string& help,
                   const prometheus::Labels& labels, double value);

  // Set a gauge to `value`. Declares the family on first use.
  void gauge_set(const std::string& name, const std::string& help,
                 const prometheus::Labels& labels, double value);

  // Render the full Prometheus text exposition document (all families).
  std::string render() const;

  // Current value of a (name, labels) series, or 0 if absent. Test helper.
  double value(const std::string& name, const prometheus::Labels& labels) const;

 private:
  struct Family {
    std::string type;  // "counter" | "gauge"
    std::string help;
    // Keyed by the serialized label set so each (name, labels) is one series.
    std::map<std::string, double> series;
    std::map<std::string, prometheus::Labels> series_labels;
  };

  static std::string label_key(const prometheus::Labels& labels);

  mutable std::mutex mu_;
  std::map<std::string, Family> families_;
};

}  // namespace evo::metrics
