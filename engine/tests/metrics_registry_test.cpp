// M38 unit tests: Prometheus text format helpers + thread-safe metrics registry.
//
// Pure scheduler-core: no transport, no store, no threads (except one
// concurrency check). Verifies:
//   1. escape_label_value escapes backslash/quote/newline per the spec.
//   2. format_sample renders `name{k="v"} value` and bare `name value`.
//   3. format_family emits HELP + TYPE + samples.
//   4. MetricsRegistry counter_add accumulates; counter_set sets absolute.
//   5. MetricsRegistry gauge_set overwrites.
//   6. render() emits a valid multi-family document with HELP/TYPE per family.
//   7. Concurrent counter_add from many threads is race-free (TSan-checked).

#include <cstdio>
#include <string>
#include <thread>
#include <vector>

#include "evo/metrics_registry.hpp"
#include "evo/prometheus.hpp"

namespace {

int failures = 0;
void check(bool cond, const std::string& label) {
  if (cond) {
    printf("  ok   %s\n", label.c_str());
  } else {
    printf("  FAIL %s\n", label.c_str());
    ++failures;
  }
}

bool contains(const std::string& hay, const std::string& needle) {
  return hay.find(needle) != std::string::npos;
}

}  // namespace

int main() {
  // --- 1. Label-value escaping ---------------------------------------------
  check(evo::prometheus::escape_label_value("plain") == "plain",
        "plain label value unchanged");
  check(evo::prometheus::escape_label_value("a\"b") == "a\\\"b",
        "double-quote escaped");
  check(evo::prometheus::escape_label_value("a\\b") == "a\\\\b",
        "backslash escaped");
  check(evo::prometheus::escape_label_value("a\nb") == "a\\nb",
        "newline escaped");

  // --- 2. format_sample ----------------------------------------------------
  check(evo::prometheus::format_sample("m_total", {}, 5) == "m_total 5",
        "bare sample (no labels)");
  {
    const std::string s = evo::prometheus::format_sample(
        "m_total", {{"org", "org-a"}, {"mode", "local"}}, 3);
    check(s == "m_total{org=\"org-a\",mode=\"local\"} 3",
          "labeled sample renders key=\"value\" pairs");
  }

  // --- 3. format_family ----------------------------------------------------
  {
    const std::string fam = evo::prometheus::format_family(
        "counter", "m_total", "A test counter.", "m_total 5\n");
    check(contains(fam, "# HELP m_total A test counter."), "HELP line present");
    check(contains(fam, "# TYPE m_total counter"), "TYPE line present");
    check(contains(fam, "m_total 5"), "sample present");
  }

  // --- 4/5. Registry counter/gauge semantics -------------------------------
  {
    evo::metrics::MetricsRegistry reg;
    reg.counter_add("sub_total", "Submissions.", {}, 1.0);
    reg.counter_add("sub_total", "Submissions.", {}, 2.0);
    check(reg.value("sub_total", {}) == 3.0, "counter_add accumulates");

    reg.counter_set("gate_total", "Gate total.", {}, 10.0);
    reg.counter_set("gate_total", "Gate total.", {}, 12.0);
    check(reg.value("gate_total", {}) == 12.0,
          "counter_set sets absolute (no double-count)");

    reg.gauge_set("active", "Active runs.", {}, 4.0);
    reg.gauge_set("active", "Active runs.", {}, 2.0);
    check(reg.value("active", {}) == 2.0, "gauge_set overwrites");

    // Labeled series are distinct.
    reg.counter_add("term_total", "Terminal.", {{"outcome", "SUCCEEDED"}}, 1.0);
    reg.counter_add("term_total", "Terminal.", {{"outcome", "FAILED"}}, 1.0);
    reg.counter_add("term_total", "Terminal.", {{"outcome", "SUCCEEDED"}}, 1.0);
    check(reg.value("term_total", {{"outcome", "SUCCEEDED"}}) == 2.0,
          "labeled series accumulate independently (SUCCEEDED=2)");
    check(reg.value("term_total", {{"outcome", "FAILED"}}) == 1.0,
          "labeled series accumulate independently (FAILED=1)");
  }

  // --- 6. render() emits a valid multi-family document ---------------------
  {
    evo::metrics::MetricsRegistry reg;
    reg.counter_add("a_total", "Counter A.", {{"k", "v"}}, 7.0);
    reg.gauge_set("b_gauge", "Gauge B.", {}, 3.0);
    const std::string doc = reg.render();
    check(contains(doc, "# TYPE a_total counter"), "render: counter TYPE");
    check(contains(doc, "# TYPE b_gauge gauge"), "render: gauge TYPE");
    check(contains(doc, "a_total{k=\"v\"} 7"), "render: labeled counter sample");
    check(contains(doc, "b_gauge 3"), "render: gauge sample");
    // Every HELP has a matching TYPE (well-formed families).
    check(contains(doc, "# HELP a_total") && contains(doc, "# HELP b_gauge"),
          "render: HELP lines present");
  }

  // --- 7. Concurrent counter_add is race-free (TSan-checked) ---------------
  {
    evo::metrics::MetricsRegistry reg;
    constexpr int kThreads = 8;
    constexpr int kPerThread = 1000;
    std::vector<std::thread> ts;
    for (int t = 0; t < kThreads; ++t) {
      ts.emplace_back([&reg] {
        for (int i = 0; i < kPerThread; ++i) {
          reg.counter_add("conc_total", "Concurrent counter.", {}, 1.0);
        }
      });
    }
    for (auto& th : ts) th.join();
    check(reg.value("conc_total", {}) ==
              static_cast<double>(kThreads * kPerThread),
          "concurrent counter_add sums exactly (no lost updates)");
  }

  if (failures == 0) {
    printf("\nALL M38 METRICS TESTS PASSED!\n");
    return 0;
  }
  printf("\n%d FAILURE(S)\n", failures);
  return 1;
}
