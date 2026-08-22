// Milestone 38: Prometheus text exposition format helpers. See prometheus.hpp.

#include "evo/prometheus.hpp"

#include <cstdio>
#include <string>

namespace evo::prometheus {

std::string escape_label_value(const std::string& value) {
  std::string out;
  out.reserve(value.size());
  for (char c : value) {
    switch (c) {
      case '\\': out += "\\\\"; break;
      case '"': out += "\\\""; break;
      case '\n': out += "\\n"; break;
      default: out += c; break;
    }
  }
  return out;
}

std::string format_sample(const std::string& name, const Labels& labels,
                          double value) {
  std::string line = name;
  if (!labels.empty()) {
    line += "{";
    for (size_t i = 0; i < labels.size(); ++i) {
      if (i > 0) line += ",";
      line += labels[i].first;
      line += "=\"";
      line += escape_label_value(labels[i].second);
      line += "\"";
    }
    line += "}";
  }
  // Integral counters/gauges render without a decimal point; keep it simple
  // and deterministic.
  char buf[64];
  std::snprintf(buf, sizeof(buf), "%g", value);
  line += " ";
  line += buf;
  return line;
}

std::string format_family(const std::string& type, const std::string& name,
                          const std::string& help,
                          const std::string& samples) {
  std::string out;
  out += "# HELP " + name + " " + help + "\n";
  out += "# TYPE " + name + " " + type + "\n";
  out += samples;
  if (!samples.empty() && samples.back() != '\n') out += "\n";
  return out;
}

}  // namespace evo::prometheus
