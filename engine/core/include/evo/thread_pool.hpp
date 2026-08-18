#pragma once

// ThreadPool — bounded worker pool using std::jthread (Milestone 09).
//
// Features:
// - Fixed-size pool of std::jthread workers
// - stop_token-aware shutdown with graceful drain and immediate stop
// - Exception capture at thread boundaries (exception_ptr channel)
// - Active worker and task counters for metrics
// - No new thread per task; workers loop over a shared queue

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <exception>
#include <functional>
#include <mutex>
#include <queue>
#include <stop_token>
#include <thread>
#include <vector>

namespace evo {

class ThreadPool {
public:
  using Task = std::function<void()>;

  // num_workers must be > 0. Throws std::invalid_argument if 0.
  explicit ThreadPool(std::size_t num_workers);

  ~ThreadPool();

  ThreadPool(const ThreadPool&) = delete;
  ThreadPool& operator=(const ThreadPool&) = delete;

  // Submit a task for execution.
  // Returns false if the pool is draining or stopped; task is not queued.
  // Exceptions thrown by the task are captured and available via take_exceptions().
  bool submit(Task task);

  // Graceful shutdown: finish all pending tasks, then exit workers.
  // Idempotent; calling after stop() has no effect.
  void drain();

  // Immediate shutdown: wake all workers, do not execute pending tasks.
  // Workers exit on next loop iteration. Idempotent.
  void stop();

  // Drain and return all exceptions captured from submitted tasks.
  // Clears the internal exception queue.
  std::vector<std::exception_ptr> take_exceptions();

  // Number of configured workers (constant after construction).
  std::size_t num_workers() const { return workers_.size(); }

  // Number of workers currently executing a task (not idle).
  std::size_t active_workers() const { return active_workers_.load(); }

  // Total tasks submitted (whether completed, pending, or dropped).
  std::size_t num_tasks_submitted() const { return tasks_submitted_.load(); }

  // Total tasks whose execution has finished (success or exception).
  std::size_t num_tasks_completed() const { return tasks_completed_.load(); }

private:
  void worker_loop(std::stop_token st);

  mutable std::mutex mu_;
  std::condition_variable_any cv_;
  std::queue<Task> pending_;
  std::vector<std::jthread> workers_;
  std::atomic<std::size_t> tasks_submitted_{0};
  std::atomic<std::size_t> tasks_completed_{0};
  std::atomic<std::size_t> active_workers_{0};
  std::atomic<bool> draining_{false};
  bool stopped_ = false;  // guarded by mu_
  std::vector<std::exception_ptr> exceptions_;  // guarded by mu_
};

}  // namespace evo