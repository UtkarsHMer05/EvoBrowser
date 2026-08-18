#include "evo/concurrent_scheduler.hpp"

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <map>
#include <mutex>
#include <string>
#include <vector>

#include "evo/json.hpp"

namespace evo {

std::string ConcurrentRunLog::to_json_string() const {
  json::Array arr;
  for (const auto& r : runs) {
    json::Object o;
    o.emplace("id", json::Value(r.id.value));
    o.emplace("type", json::Value(r.type));
    o.emplace("seq", json::Value(static_cast<double>(r.sequence)));
    o.emplace("ok", json::Value(r.ok()));
    o.emplace("output", json::Value(r.result.output));
    using namespace std::chrono;
    auto ready_ms = duration_cast<milliseconds>(r.ready_at.time_since_epoch()).count();
    auto start_ms = duration_cast<milliseconds>(r.started_at.time_since_epoch()).count();
    auto finish_ms = duration_cast<milliseconds>(r.finished_at.time_since_epoch()).count();
    o.emplace("ready_at_ms", json::Value(static_cast<double>(ready_ms)));
    o.emplace("started_at_ms", json::Value(static_cast<double>(start_ms)));
    o.emplace("finished_at_ms", json::Value(static_cast<double>(finish_ms)));
    arr.push_back(json::Value(std::move(o)));
  }
  json::Object doc;
  doc.emplace("runs", json::Value(std::move(arr)));
  return json::serialize(json::Value(std::move(doc)));
}

ConcurrentScheduler::ConcurrentScheduler(Dag dag,
                                         std::map<std::string, TaskFn> tasks,
                                         ConcurrentConfig config)
    : dag_(std::move(dag)),
      config_(config),
      policy_(config_.run_id),
      state_(dag_),
      pool_(config_.num_workers > 0 ? config_.num_workers : 1),
      ready_queue_(config_.ready_queue_capacity) {
  // Adapt plain TaskFn into stop_token-aware tasks that ignore the token.
  for (auto& [type, fn] : tasks) {
    tasks_[type] = [fn = std::move(fn)](const NodeSpec& spec,
                                        std::stop_token) { return fn(spec); };
  }
  init();
}

ConcurrentScheduler::ConcurrentScheduler(
    Dag dag, std::map<std::string, ConcurrentTaskFn> tasks,
    ConcurrentConfig config)
    : dag_(std::move(dag)),
      tasks_(std::move(tasks)),
      config_(config),
      policy_(config_.run_id),
      state_(dag_),
      pool_(config_.num_workers > 0 ? config_.num_workers : 1),
      ready_queue_(config_.ready_queue_capacity) {
  init();
}

void ConcurrentScheduler::init() {
  cancel_requested_at_ = std::chrono::steady_clock::time_point{};
  run_terminal_at_ = std::chrono::steady_clock::time_point{};
}

ResourcePolicy ConcurrentScheduler::policy_for_node(const NodeId& id) const {
  const NodeSpec* spec = dag_.node(id);
  if (!spec) {
    return ResourcePolicy{};  // Internal / unbounded fallback
  }
  return policy_.policy_for(*spec);
}

ConcurrentScheduler::~ConcurrentScheduler() {
  pool_.drain();
}

void ConcurrentScheduler::cancel() {
  bool expected = false;
  if (!canceled_.compare_exchange_strong(expected, true,
                                         std::memory_order_relaxed)) {
    return;  // already canceled
  }
  cancel_requested_at_ = std::chrono::steady_clock::now();

  // Commit the run to a terminal CANCELED state: non-terminal nodes become
  // CANCELED in the state machine (covers pending/blocked nodes).
  state_.cancel_run();

  // Signal in-flight tasks to abort cooperatively.
  stop_source_.request_stop();

  // Wake the dispatcher loop if it is waiting on the condition variable.
  dispatch_cv_.notify_all();
}

void ConcurrentScheduler::wait() {
  // Synchronous in M10/M11; run() blocks until done.
}

ConcurrentRunLog ConcurrentScheduler::run() {
  run_start_time_ = std::chrono::steady_clock::now();
  run_started_.store(true, std::memory_order_relaxed);

  // If cancellation was already requested before run(), start canceled.
  if (canceled_.load(std::memory_order_relaxed)) {
    state_.cancel_run();
    finalize_and_collect();
    run_terminal_at_ = std::chrono::steady_clock::now();
    run_finished_.store(true, std::memory_order_relaxed);
    return ConcurrentRunLog{std::move(log_)};
  }

  state_.start_run();

  {
    std::lock_guard lock(dispatch_mu_);
    dispatch_ready_nodes_locked();
  }
  dispatch_cv_.notify_one();

  while (true) {
    std::unique_lock lock(dispatch_mu_);
    dispatch_cv_.wait(lock, [this] {
      return canceled_.load(std::memory_order_relaxed) ||
             ready_queue_.size() > 0 ||
             in_flight_.load(std::memory_order_relaxed) == 0;
    });

    if (canceled_.load(std::memory_order_relaxed)) {
      // Stop dispatching new work immediately. Allow in-flight tasks to finish
      // (cooperatively observing the stop_token) during drain below.
      break;
    }

    // Retry resource-blocked nodes first; their resource may now be free.
    drain_resource_blocked();

    std::optional<NodeId> id_opt;
    while ((id_opt = ready_queue_.try_pop())) {
      const NodeId id = *id_opt;
      if (state_.node_state(id) != NodeState::Ready) {
        continue;
      }
      // Resource gate (M12): acquire capacity before dispatching.
      ResourcePolicy rp = policy_for_node(id);
      int& used = resource_usage_[rp.affinity_key];
      if (used >= rp.capacity) {
        // No capacity: defer (node stays READY; retried when a slot frees).
        resource_blocked_.push_back(id);
        continue;
      }
      used++;
      state_.dispatch_node(id);
      in_flight_.fetch_add(1, std::memory_order_relaxed);
      const auto ready_at = std::chrono::steady_clock::now();
      pool_.submit([this, id, ready_at]() { worker_task(id, ready_at); });
    }

    if (in_flight_.load(std::memory_order_relaxed) == 0 &&
        ready_queue_.size() == 0 && resource_blocked_.empty()) {
      break;
    }
  }

  // Drain running tasks. Cooperatively-canceling tasks return promptly after
  // observing the stop_token; oblivious tasks run to completion. No new tasks
  // are dispatched because the loop above has exited on cancellation.
  pool_.drain();

  finalize_and_collect();
  run_terminal_at_ = std::chrono::steady_clock::now();
  run_finished_.store(true, std::memory_order_relaxed);
  return ConcurrentRunLog{std::move(log_)};
}

void ConcurrentScheduler::dispatch_ready_nodes_locked() {
  std::vector<NodeId> ready = state_.ready_nodes();
  for (const auto& id : ready) {
    if (!ready_queue_.push(id)) {
      break;
    }
  }
}

void ConcurrentScheduler::drain_resource_blocked() {
  for (auto it = resource_blocked_.begin(); it != resource_blocked_.end();) {
    ResourcePolicy rp = policy_for_node(*it);
    int& used = resource_usage_[rp.affinity_key];
    if (used < rp.capacity) {
      used++;
      state_.dispatch_node(*it);
      in_flight_.fetch_add(1, std::memory_order_relaxed);
      const auto ready_at = std::chrono::steady_clock::now();
      NodeId id = *it;
      pool_.submit([this, id, ready_at]() { worker_task(id, ready_at); });
      it = resource_blocked_.erase(it);
    } else {
      ++it;
    }
  }
}

void ConcurrentScheduler::worker_task(const NodeId& id,
                                      std::chrono::steady_clock::time_point ready_at) {
  auto started_at = std::chrono::steady_clock::now();

  state_.start_node(id);

  const NodeSpec* spec = dag_.node(id);
  TaskResult result{false, "unknown node"};

  if (spec) {
    auto it = tasks_.find(spec->type);
    if (it != tasks_.end()) {
      // Pass the run's stop_token so the task can abort cooperatively.
      result = it->second(*spec, stop_source_.get_token());
    } else {
      result = TaskResult{false, "unregistered task type: " + spec->type};
    }
  }

  auto finished_at = std::chrono::steady_clock::now();

  ConcurrentNodeRun rec;
  rec.id = id;
  rec.type = spec ? spec->type : std::string{};
  rec.sequence = sequence_counter_.fetch_add(1, std::memory_order_relaxed);
  rec.ready_at = ready_at;
  rec.started_at = started_at;
  rec.finished_at = finished_at;
  rec.result = result;

  {
    std::lock_guard lock(log_mu_);
    log_.push_back(std::move(rec));
  }

  on_node_complete(id, result);
}

void ConcurrentScheduler::on_node_complete(const NodeId& id, const TaskResult& result) {
  // Release the resource this node held (M12).
  ResourcePolicy rp = policy_for_node(id);
  {
    std::lock_guard lock(dispatch_mu_);
    auto it = resource_usage_.find(rp.affinity_key);
    if (it != resource_usage_.end() && it->second > 0) {
      it->second--;
    }
    // Re-attempt deferred nodes now that a resource slot freed.
    drain_resource_blocked();
  }

  // If cancellation is in progress, do not unlock successors — pending/blocked
  // nodes are already CANCELED by cancel_run().
  if (!canceled_.load(std::memory_order_relaxed)) {
    std::vector<NodeId> newly_ready = state_.complete_node(id, result);
    {
      std::lock_guard lock(dispatch_mu_);
      for (const auto& nid : newly_ready) {
        ready_queue_.push(nid);
      }
    }
  }

  in_flight_.fetch_sub(1, std::memory_order_relaxed);
  dispatch_cv_.notify_one();
}

void ConcurrentScheduler::finalize_and_collect() {
  state_.finalize_run();

  std::sort(log_.begin(), log_.end(),
            [](const ConcurrentNodeRun& a, const ConcurrentNodeRun& b) {
              return a.sequence < b.sequence;
            });
}

}  // namespace evo