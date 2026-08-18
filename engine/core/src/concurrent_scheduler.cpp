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
      tasks_(std::move(tasks)),
      config_(config),
      state_(dag_),
      pool_(config_.num_workers > 0 ? config_.num_workers : 1),
      ready_queue_(config_.ready_queue_capacity) {}

ConcurrentScheduler::~ConcurrentScheduler() {
  pool_.drain();
}

void ConcurrentScheduler::cancel() {
  canceled_.store(true, std::memory_order_relaxed);
  state_.cancel_run();
  ready_queue_.close();
  pool_.stop();
  // Wake the dispatcher loop if it is waiting.
  dispatch_cv_.notify_all();
}

void ConcurrentScheduler::wait() {
  // Synchronous in M10; run() blocks until done.
}

ConcurrentRunLog ConcurrentScheduler::run() {
  run_start_time_ = std::chrono::steady_clock::now();
  run_started_.store(true, std::memory_order_relaxed);

  state_.start_run();

  // Main dispatch loop: pop ready nodes from the queue (fed by the initial
  // push below and by completing workers) and submit them to the pool. The
  // loop terminates when no work is in flight and the queue is empty, or when
  // cancellation is requested.
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
      break;
    }

    // Pop and dispatch as many ready nodes as are currently available.
    std::optional<NodeId> id_opt;
    while ((id_opt = ready_queue_.try_pop())) {
      const NodeId id = *id_opt;
      state_.dispatch_node(id);
      if (state_.node_state(id) != NodeState::Dispatched) {
        continue;
      }
      const auto ready_at = std::chrono::steady_clock::now();
      in_flight_.fetch_add(1, std::memory_order_relaxed);
      pool_.submit([this, id, ready_at]() { worker_task(id, ready_at); });
    }

    // Termination: nothing in flight and nothing left to dispatch.
    if (in_flight_.load(std::memory_order_relaxed) == 0 &&
        ready_queue_.size() == 0) {
      break;
    }
  }

  // Wait for any in-flight tasks (graceful drain).
  pool_.drain();
  finalize_and_collect();

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

void ConcurrentScheduler::dispatch_ready_nodes() {
  std::lock_guard lock(dispatch_mu_);
  dispatch_ready_nodes_locked();
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
      result = it->second(*spec);
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
  std::vector<NodeId> newly_ready = state_.complete_node(id, result);

  if (canceled_.load(std::memory_order_relaxed)) {
    return;
  }

  {
    std::lock_guard lock(dispatch_mu_);
    for (const auto& nid : newly_ready) {
      ready_queue_.push(nid);
    }
  }
  // Decrement in-flight and wake the dispatcher (it may now be done or have
  // new work to pull).
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