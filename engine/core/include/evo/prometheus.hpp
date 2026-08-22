#pragma once

// Milestone 38: Prometheus text exposition format helpers.
//
// The scheduler exposes its counters/gauges at an HTTP /metrics endpoint in
// the Prometheus text format (text/plain; version=0.0.4). These helpers build
// correctly-escaped sample lines and metric families so the exposition is
// valid and machine-parseable by a Prometheus scraper.
//
// Format reference (Prometheus exposition):
//   # HELP <name> <help>
//   # TYPE <name> <counter|gauge>
//   <name>{label="value",...} <number>
//
// Label values escape backslash, double-quote, and newline per the spec.

#include <string>
#include <utility>
#include <vector>

namespace evo::prometheus {

using Labels = std::vector<std::pair<std::string, std::string>>;

// Escape a label value per the Prometheus exposition spec:
// '\\' -> "\\\\", '"' -> "\\\"", '\n' -> "\\n".
std::string escape_label_value(const std::string& value);

// Render one sample line: `name{k="v",...} value` (no labels => `name value`).
// `value` is rendered as a plain decimal (counters/gauges are integral here).
std::string format_sample(const std::string& name, const Labels& labels,
                          double value);

// Render a full metric family: HELP + TYPE lines followed by the samples.
// `type` is "counter" or "gauge". `samples` is the pre-rendered sample lines
// (one per line, no trailing newline needed).
std::string format_family(const std::string& type, const std::string& name,
                          const std::string& help,
                          const std::string& samples);

}  // namespace evo::prometheus
