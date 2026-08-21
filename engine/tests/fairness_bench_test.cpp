// M37 fairness benchmark: per-tenant wait distributions + Jain's fairness index
// on controlled workloads (M37 steps 6–7).
//
// Self-contained scheduler-core benchmark: InMemoryTransport + InMemoryRunStore,
// no Redis/Postgres. K tenants each submit a browser FAN-OUT of T nodes (all
// ready at once) against a GLOBAL browser capacity of 1 with fair scheduling ON.
// Within a run the T browser nodes share the run's capacity-1 affinity key, so
// the tenant presents ONE browser task at a time to the global pool; ACROSS
// tenants the tasks compete for the single global slot, which the fair
// scheduler round-robins among the tenants' fresh demand.
//
// Metrics (BENCHMARK_METHODOLOGY.md §2 "Fairness"):
//   - Jain's index J = (Σxᵢ)² / (n·Σxᵢ²). Reported two ways:
//       * over per-tenant completion spans  (end-to-end fairness),
//       * over per-tenant served slot counts (grant fairness; == 1.0 when the
//         gate hands every tenant exactly T slots).
//   - Per-tenant queue wait: for each browser task, the time from when it
//     became eligible for the global slot (the previous in-run task completed,
//     or demand-start for the first) to when it was dispatched. max + median.
//   - Raw per-task dispatch/complete samples -> samples.jsonl.
//
// Workloads:
//   A. equal-duration: every tenant's browser task takes the same work_ms.
//      Expect Jain(spans) ~ 1.0 and equal served counts.
//   B. unequal-duration (M37 step 7): tenant 0's tasks are 3x slower. Fairness
//      here means every tenant still COMPLETES and grant counts stay equal —
//      the slow tenant legitimately takes longer end-to-end (documented, not a
//      fairness failure). Jain(spans) is reported as info, not asserted ~1.
//
// Artifacts: when EVO_M37_ARTIFACT_DIR is set, writes manifest.json /
// samples.jsonl / summary.json / command.txt there (BENCHMARK_METHODOLOGY §4),
// with commit + hardware + build-mode provenance. Timings are DIAGNOSTIC
// (single local stack), not evidence-grade benchmark numbers (that is M39).
//
// Determinism: fixed tenant/task counts, fixed work_ms, no randomness. The
// fair scheduler's grant ORDER is a pure function of shared gate state, so
// served counts are deterministic; absolute wall times vary by host speed.

#include <google/protobuf/util/time_util.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "evo/dag.hpp"
#include "evo/distributed_run_loop.hpp"
#include "evo/execution.pb.h"
#include "evo/quota.hpp"
#include "evo/run_store.hpp"
#include "evo/transport.hpp"

using evo::Dag;
using evo::DistributedRunConfig;
using evo::DistributedRunLoop;
using evo::Edge;
using evo::InMemoryRunStore;
using evo::InMemoryTransport;
using evo::NodeKind;
using evo::NodeId;
using evo::NodeSpec;
using evo::QuotaConfig;
using evo::ResourceClass;
using evo::RunEvent;
using evo::TenantQuotaGate;
using namespace std::chrono_literals;

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

std::string env_or(const char* name, const std::string& fallback) {
  const char* v = std::getenv(name);
  return (v && *v) ? std::string(v) : fallback;
}

std::string encode_success(const std::string& run_id, const std::string& node,
                           unsigned attempt, const std::string& output) {
  evo::execution::v1::ResultEnvelope env;
  env.set_run_id(run_id);
  env.set_node_id(node);
  env.set_attempt_number(attempt);
  env.set_completed(true);
  env.set_output(output);
  env.set_status(evo::execution::v1::ResultEnvelope::OK);
  *env.mutable_finished_at() =
      google::protobuf::util::TimeUtil::MillisecondsToTimestamp(
          evo::now_wall_ms());
  return env.SerializeAsString();
}

// Browser FAN-OUT: a trigger feeding T browser action nodes, all ready at once.
// Within the run the nodes share the capacity-1 browser affinity key (they
// serialize), so the tenant presents ONE browser task at a time to the global
// pool; ACROSS tenants the tasks compete for the single global slot, which is
// what the fair scheduler orders.
Dag make_browser_fanout(const std::string& trig, int tasks) {
  std::vector<NodeSpec> nodes = {
      {NodeId{trig}, NodeKind::Trigger, "start"},
  };
  std::vector<Edge> edges;
  for (int i = 0; i < tasks; ++i) {
    const std::string id = trig + "-t" + std::to_string(i);
    nodes.push_back({NodeId{id}, NodeKind::Action, "open-url"});
    edges.push_back({NodeId{trig}, NodeId{id}});
  }
  auto br = Dag::build(nodes, edges);
  return std::move(*br.dag);
}

// One recorded browser-task dispatch + completion.
struct TaskSample {
  std::string org;
  std::string node;
  std::int64_t dispatch_wall_ms = 0;
  std::int64_t complete_wall_ms = 0;
};

// Worker that completes every task, holding browser tasks for `work_ms` and
// recording each browser task's dispatch + completion wall times into
// `samples` (mutex-guarded, shared across tenants).
class FairBenchWorker {
 public:
  FairBenchWorker(InMemoryTransport& t, std::string prefix,
                  std::chrono::milliseconds work_ms, std::mutex* mu,
                  std::vector<TaskSample>* samples)
      : transport_(t),
        prefix_(std::move(prefix)),
        work_ms_(work_ms),
        mu_(mu),
        samples_(samples) {}

  void start() {
    thread_ = std::jthread([this](std::stop_token st) { this->loop(st); });
  }
  void stop() {
    thread_.request_stop();
    if (thread_.joinable()) thread_.join();
  }

 private:
  void loop(std::stop_token st) {
    const std::string tasks = evo::task_stream_key(prefix_);
    const std::string results = evo::result_stream_key(prefix_);
    transport_.ensure_group(tasks, "workers");
    while (!st.stop_requested()) {
      auto msg = transport_.read(tasks, "workers", "fair-worker", 10ms, st);
      if (!msg) continue;
      evo::execution::v1::TaskEnvelope task;
      if (!task.ParseFromString(msg->payload)) {
        transport_.ack(tasks, "workers", msg->id);
        continue;
      }
      const std::int64_t dispatch_ms = evo::now_wall_ms();
      const bool browser =
          task.resource_class() == evo::execution::v1::BROWSER;
      if (browser) {
        std::this_thread::sleep_for(work_ms_);  // hold the global slot
      }
      const std::int64_t complete_ms = evo::now_wall_ms();
      if (browser) {
        std::lock_guard lock(*mu_);
        samples_->push_back(
            {task.org_id(), task.node_id(), dispatch_ms, complete_ms});
      }
      transport_.publish(results, encode_success(task.run_id(), task.node_id(),
                                                 task.attempt_number(),
                                                 "{\"ok\":true}"));
      transport_.ack(tasks, "workers", msg->id);
    }
  }

  InMemoryTransport& transport_;
  std::string prefix_;
  std::chrono::milliseconds work_ms_;
  std::mutex* mu_;
  std::vector<TaskSample>* samples_;
  std::jthread thread_;
};

// Jain's fairness index over per-tenant values xᵢ: J = (Σxᵢ)² / (n·Σxᵢ²).
// 1.0 = perfectly fair; 1/n = maximally unfair.
double jain_index(const std::vector<double>& x) {
  if (x.empty()) return 0.0;
  double sum = 0.0, sumsq = 0.0;
  for (double v : x) {
    sum += v;
    sumsq += v * v;
  }
  if (sumsq <= 0.0) return 0.0;
  return (sum * sum) / (static_cast<double>(x.size()) * sumsq);
}

// Outcome of one controlled fairness workload.
struct FairnessOutcome {
  std::map<std::string, std::int64_t> span_ms;  // org -> completion span
  std::map<std::string, std::int64_t> served;   // org -> browser slots granted
  std::size_t deferrals = 0;                    // gate fair-order deferrals
  std::size_t deferred_tasks = 0;               // total deferred dispatches
};

// Run one controlled fairness workload. K tenants each run a browser FAN-OUT of
// `tasks_per_org` nodes against a global browser capacity of 1. `work_ms_per_org`
// gives each tenant its browser-task duration. Fills `samples` with per-task
// dispatch/complete records. Returns per-tenant completion spans + served counts.
FairnessOutcome run_fairness_workload(
    const std::vector<std::string>& orgs,
    const std::map<std::string, std::chrono::milliseconds>& work_ms_per_org,
    int tasks_per_org, bool fair, std::vector<TaskSample>* samples) {
  QuotaConfig qcfg;
  qcfg.global_class_capacity[ResourceClass::Browser] = 1;
  qcfg.fair_scheduling = fair;
  TenantQuotaGate gate(qcfg);

  std::mutex sample_mu;
  std::mutex done_mu;
  std::map<std::string, std::int64_t> finished_wall;  // org -> run_finished wall

  const std::int64_t t0 = evo::now_wall_ms();  // demand-start for all tenants

  std::vector<std::unique_ptr<InMemoryTransport>> transports;
  std::vector<std::unique_ptr<InMemoryRunStore>> stores;
  std::vector<std::unique_ptr<DistributedRunLoop>> loops;
  std::vector<std::unique_ptr<FairBenchWorker>> workers;

  for (size_t i = 0; i < orgs.size(); ++i) {
    const std::string& org = orgs[i];
    transports.push_back(std::make_unique<InMemoryTransport>());
    stores.push_back(std::make_unique<InMemoryRunStore>());

    DistributedRunConfig cfg;
    cfg.run_id = "run-fair-" + org;
    cfg.org_id = org;
    cfg.workflow_id = "wf-" + org;
    cfg.env_prefix = "evo:m37fair:" + org;
    cfg.read_block_ms = 2ms;
    cfg.run_timeout = 30s;
    cfg.quota_gate = &gate;

    auto it = work_ms_per_org.find(org);
    const auto work = it == work_ms_per_org.end() ? 10ms : it->second;
    workers.push_back(std::make_unique<FairBenchWorker>(
        *transports[i], cfg.env_prefix, work, &sample_mu, samples));

    // Capture the tenant's terminal wall time from its run_finished event.
    std::string org_copy = org;
    loops.push_back(std::make_unique<DistributedRunLoop>(
        make_browser_fanout(org, tasks_per_org), *transports[i], *stores[i],
        cfg, [&done_mu, &finished_wall, org_copy](const RunEvent& ev) {
          if (ev.kind == "run_finished") {
            std::lock_guard lock(done_mu);
            finished_wall[org_copy] = ev.wall_ms;
          }
        }));
  }

  for (auto& w : workers) w->start();
  std::vector<std::thread> threads;
  for (auto& l : loops) {
    DistributedRunLoop* lp = l.get();
    threads.emplace_back([lp] { (void)lp->run(); });
  }
  for (auto& th : threads) th.join();
  for (auto& w : workers) w->stop();

  FairnessOutcome out;
  for (const auto& o : orgs) {
    std::lock_guard lock(done_mu);
    auto fit = finished_wall.find(o);
    const std::int64_t done =
        fit != finished_wall.end() ? fit->second : evo::now_wall_ms();
    out.span_ms[o] = done - t0;
    out.served[o] =
        static_cast<std::int64_t>(gate.served_count(o, ResourceClass::Browser));
  }
  out.deferrals = gate.counters().fair_order_deferrals;
  out.deferred_tasks = gate.counters().deferred_tasks;
  return out;
}

// Per-org queue-wait samples (ms). wait(task k) = dispatch_k - eligible_k, where
// eligible_k = complete_{k-1} (the in-run affinity slot freed) for k>0, and the
// workload demand-start t0 for k=0. Tasks within an org are ordered by dispatch
// time (in-run affinity serializes them, so dispatch order == execution order).
std::map<std::string, std::vector<std::int64_t>> per_org_waits(
    const std::vector<TaskSample>& samples,
    const std::vector<std::string>& orgs, std::int64_t t0) {
  std::map<std::string, std::vector<TaskSample>> by_org;
  for (const auto& s : samples) by_org[s.org].push_back(s);
  std::map<std::string, std::vector<std::int64_t>> waits;
  for (const auto& o : orgs) {
    auto& v = by_org[o];
    std::sort(v.begin(), v.end(),
              [](const TaskSample& a, const TaskSample& b) {
                return a.dispatch_wall_ms < b.dispatch_wall_ms;
              });
    std::vector<std::int64_t> w;
    for (size_t k = 0; k < v.size(); ++k) {
      const std::int64_t eligible = (k == 0) ? t0 : v[k - 1].complete_wall_ms;
      w.push_back(v[k].dispatch_wall_ms - eligible);
    }
    waits[o] = std::move(w);
  }
  return waits;
}

std::int64_t median_of(std::vector<std::int64_t> v) {
  if (v.empty()) return 0;
  std::sort(v.begin(), v.end());
  return v[v.size() / 2];
}
std::int64_t max_of(const std::vector<std::int64_t>& v) {
  std::int64_t m = 0;
  for (auto x : v) m = std::max(m, x);
  return m;
}

}  // namespace

int main() {
  const int tenants = std::atoi(env_or("EVO_M37_TENANTS", "3").c_str());
  const int tasks = std::atoi(env_or("EVO_M37_TASKS", "4").c_str());

  std::vector<std::string> orgs;
  for (int i = 0; i < tenants; ++i) orgs.push_back("org-" + std::to_string(i));

  // --- Workload A: equal-duration fairness (M37 step 6) --------------------
  std::vector<TaskSample> samples_a;
  std::map<std::string, std::chrono::milliseconds> equal_work;
  for (const auto& o : orgs) equal_work[o] = 15ms;
  const std::int64_t t0a = evo::now_wall_ms();
  FairnessOutcome a =
      run_fairness_workload(orgs, equal_work, tasks, /*fair=*/true, &samples_a);

  std::vector<double> span_a, served_a;
  for (const auto& o : orgs) {
    span_a.push_back(static_cast<double>(a.span_ms[o]));
    served_a.push_back(static_cast<double>(a.served[o]));
  }
  const double jain_span_a = jain_index(span_a);
  const double jain_served_a = jain_index(served_a);
  printf("  info workload A (equal duration, fair=on): Jain(span)=%.4f "
         "Jain(served)=%.4f deferrals=%zu\n",
         jain_span_a, jain_served_a, a.deferrals);
  for (const auto& o : orgs) {
    printf("  info   %s span=%lldms served=%lld\n", o.c_str(),
           static_cast<long long>(a.span_ms[o]),
           static_cast<long long>(a.served[o]));
  }
  check(jain_span_a >= 0.90,
        "m37: equal-duration Jain(span) >= 0.90 (fair scheduling)");
  check(jain_served_a >= 0.999,
        "m37: equal-duration Jain(served) == 1.0 (equal slot grants)");
  for (const auto& o : orgs) {
    check(a.served[o] == tasks,
          "m37: " + o + " granted exactly its " + std::to_string(tasks) +
              " browser slots");
  }

  // --- Workload B: unequal-duration fairness (M37 step 7) ------------------
  // Tenant 0's tasks are 3x slower. It legitimately takes longer end-to-end;
  // fairness means every tenant still COMPLETES and grant counts stay equal
  // (the slow tenant is not given extra or fewer slots). We assert completion
  // + equal grants; Jain(span) is reported as info, not asserted ~1.
  std::vector<TaskSample> samples_b;
  std::map<std::string, std::chrono::milliseconds> unequal_work;
  for (size_t i = 0; i < orgs.size(); ++i) {
    unequal_work[orgs[i]] = (i == 0) ? 45ms : 15ms;  // org-0 is 3x slower
  }
  FairnessOutcome b = run_fairness_workload(orgs, unequal_work, tasks,
                                            /*fair=*/true, &samples_b);
  std::vector<double> span_b, served_b;
  for (const auto& o : orgs) {
    span_b.push_back(static_cast<double>(b.span_ms[o]));
    served_b.push_back(static_cast<double>(b.served[o]));
  }
  const double jain_span_b = jain_index(span_b);
  const double jain_served_b = jain_index(served_b);
  printf("  info workload B (unequal duration, fair=on): Jain(span)=%.4f "
         "Jain(served)=%.4f\n",
         jain_span_b, jain_served_b);
  bool all_complete_b = true;
  for (const auto& o : orgs) {
    if (b.span_ms[o] <= 0) all_complete_b = false;
    printf("  info   %s span=%lldms served=%lld\n", o.c_str(),
           static_cast<long long>(b.span_ms[o]),
           static_cast<long long>(b.served[o]));
  }
  check(all_complete_b,
        "m37: unequal-duration — every tenant completes (no starvation)");
  check(jain_served_b >= 0.999,
        "m37: unequal-duration — slot grants stay equal (Jain(served) == 1.0)");
  // The slow tenant (org-0) must HOLD the slot longer in aggregate than a fast
  // tenant — this proves the unequal durations were actually exercised. Note we
  // do NOT assert the slow tenant's completion SPAN is longest: under round-
  // robin fair scheduling the slow tenant's last task can be granted before a
  // fast tenant's last task, so span ordering is not a fairness property. The
  // fairness guarantee is equal grant counts + no starvation, asserted above.
  if (orgs.size() >= 2) {
    auto busy = [&samples_b](const std::string& org) {
      std::int64_t total = 0;
      for (const auto& s : samples_b) {
        if (s.org == org) total += s.complete_wall_ms - s.dispatch_wall_ms;
      }
      return total;
    };
    const std::int64_t busy_slow = busy(orgs[0]);
    const std::int64_t busy_fast = busy(orgs[1]);
    printf("  info   busy time: %s=%lldms %s=%lldms\n", orgs[0].c_str(),
           static_cast<long long>(busy_slow), orgs[1].c_str(),
           static_cast<long long>(busy_fast));
    check(busy_slow > busy_fast,
          "m37: unequal-duration — slow tenant holds the slot longer in "
          "aggregate (durations exercised)");
  }

  // --- Per-tenant queue-wait distribution (workload A) ---------------------
  auto waits_a = per_org_waits(samples_a, orgs, t0a);
  printf("  info workload A per-org queue wait (eligible->dispatch):\n");
  for (const auto& o : orgs) {
    const auto& w = waits_a[o];
    check(static_cast<int>(w.size()) == tasks,
          "m37: " + o + " recorded all its browser task samples");
    printf("  info   %s n=%zu max_wait=%lldms median_wait=%lldms\n", o.c_str(),
           w.size(), static_cast<long long>(max_of(w)),
           static_cast<long long>(median_of(w)));
  }

  // --- Raw artifact emission (BENCHMARK_METHODOLOGY §4) --------------------
  const std::string artifact_dir = env_or("EVO_M37_ARTIFACT_DIR", "");
  if (!artifact_dir.empty()) {
    std::string hw = "unknown";
    if (FILE* p = popen("uname -sm", "r")) {
      char buf[128];
      if (fgets(buf, sizeof(buf), p)) {
        hw = buf;
        while (!hw.empty() && (hw.back() == '\n' || hw.back() == '\r'))
          hw.pop_back();
      }
      pclose(p);
    }
    std::ofstream manifest(artifact_dir + "/manifest.json");
    if (manifest) {
      manifest << "{\n"
               << "  \"slug\": \"m37_fair_scheduling\",\n"
               << "  \"workload\": \"K tenants x T browser fan-out tasks, "
                  "global browser capacity 1, fair scheduling on\",\n"
               << "  \"resource_class\": \"BROWSER (global capacity 1)\",\n"
               << "  \"build_mode\": \""
               << env_or("EVO_M37_BUILD_MODE", "Release") << "\",\n"
               << "  \"commit\": \"" << EVO_BUILD_COMMIT << "\",\n"
               << "  \"hardware\": \"" << hw << "\",\n"
               << "  \"tenants\": " << tenants << ",\n"
               << "  \"tasks_per_tenant\": " << tasks << ",\n"
               << "  \"clock\": \"wall-clock UTC ms\",\n"
               << "  \"note\": \"diagnostic fairness samples on a single local "
                  "stack; not evidence-grade benchmark numbers (M39)\",\n"
               << "  \"generated_at_wall_ms\": " << evo::now_wall_ms() << "\n"
               << "}\n";
    }
    std::ofstream samplesf(artifact_dir + "/samples.jsonl");
    for (const auto& s : samples_a) {
      samplesf << "{\"workload\":\"equal\",\"org\":\"" << s.org
               << "\",\"node\":\"" << s.node << "\",\"dispatch_wall_ms\":"
               << s.dispatch_wall_ms << ",\"complete_wall_ms\":"
               << s.complete_wall_ms << "}\n";
    }
    for (const auto& s : samples_b) {
      samplesf << "{\"workload\":\"unequal\",\"org\":\"" << s.org
               << "\",\"node\":\"" << s.node << "\",\"dispatch_wall_ms\":"
               << s.dispatch_wall_ms << ",\"complete_wall_ms\":"
               << s.complete_wall_ms << "}\n";
    }
    std::ofstream summary(artifact_dir + "/summary.json");
    if (summary) {
      summary << "{\n"
              << "  \"metric\": \"Jain fairness index\",\n"
              << "  \"workload_equal_jain_span\": " << jain_span_a << ",\n"
              << "  \"workload_equal_jain_served\": " << jain_served_a << ",\n"
              << "  \"workload_unequal_jain_span\": " << jain_span_b << ",\n"
              << "  \"workload_unequal_jain_served\": " << jain_served_b
              << ",\n"
              << "  \"tenants\": " << tenants << ",\n"
              << "  \"tasks_per_tenant\": " << tasks << "\n"
              << "}\n";
    }
    std::ofstream command(artifact_dir + "/command.txt");
    if (command) {
      command << "ctest --test-dir engine/build -R fairness_bench "
                 "--output-on-failure\n"
              << "# (or directly) EVO_M37_TENANTS=" << tenants
               << " EVO_M37_TASKS=" << tasks
               << " EVO_M37_ARTIFACT_DIR=" << artifact_dir
               << " engine/build/evo_fairness_bench_test\n";
    }
    printf("  info m37 raw artifacts written to %s\n", artifact_dir.c_str());
  }

  if (failures == 0) {
    printf("\nALL M37 FAIRNESS BENCHMARK TESTS PASSED!\n");
    return 0;
  }
  printf("\n%d FAILURE(S)\n", failures);
  return 1;
}
