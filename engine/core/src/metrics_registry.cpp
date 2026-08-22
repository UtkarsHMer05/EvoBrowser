// Milestone 38: thread-safe metrics registry. See metrics_registry.hpp.

#include "evo/metrics_registry.hpp"

#include <string>

namespace evo::metrics {

std::string MetricsRegistry::label_key(const prometheus::Labels& labels) {
  std::string key;
  for (const auto& [k, v] : labels) {
    key += k;
    key += '\x1f';  // unit separator: cannot appear in label names/values here
    key += v;
    key += '\x1e';  // record separator between pairs
  }
  return key;
}

void MetricsRegistry::counter_add(const std::string& name,
                                  const std::string& help,
                                  const prometheus::Labels& labels,
                                  double delta) {
  std::lock_guard lock(mu_);
  auto& fam = families_[name];
  if (fam.type.empty()) {
    fam.type = "counter";
    fam.help = help;
  }
  const std::string key = label_key(labels);
  fam.series[key] += delta;
  fam.series_labels[key] = labels;
}

void MetricsRegistry::counter_set(const std::string& name,
                                  const std::string& help,
                                  const prometheus::Labels& labels,
                                  double value) {
  std::lock_guard lock(mu_);
  auto& fam = families_[name];
  if (fam.type.empty()) {
    fam.type = "counter";
    fam.help = help;
  }
  const std::string key = label_key(labels);
  fam.series[key] = value;
  fam.series_labels[key] = labels;
}

void MetricsRegistry::gauge_set(const std::string& name,
                                const std::string& help,
                                const prometheus::Labels& labels, double value) {
  std::lock_guard lock(mu_);
  auto& fam = families_[name];
  if (fam.type.empty()) {
    fam.type = "gauge";
    fam.help = help;
  }
  const std::string key = label_key(labels);
  fam.series[key] = value;
  fam.series_labels[key] = labels;
}

std::string MetricsRegistry::render() const {
  std::lock_guard lock(mu_);
  std::string out;
  for (const auto& [name, fam] : families_) {
    std::string samples;
    for (const auto& [key, value] : fam.series) {
      auto lit = fam.series_labels.find(key);
      const prometheus::Labels& labels =
          lit != fam.series_labels.end() ? lit->second : prometheus::Labels{};
      samples += prometheus::format_sample(name, labels, value);
      samples += "\n";
    }
    out += prometheus::format_family(fam.type, name, fam.help, samples);
  }
  return out;
}

double MetricsRegistry::value(const std::string& name,
                              const prometheus::Labels& labels) const {
  std::lock_guard lock(mu_);
  auto fit = families_.find(name);
  if (fit == families_.end()) return 0.0;
  auto sit = fit->second.series.find(label_key(labels));
  return sit == fit->second.series.end() ? 0.0 : sit->second;
}

}  // namespace evo::metrics
