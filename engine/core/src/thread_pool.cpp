#include "evo/thread_pool.hpp"

#include <stdexcept>

namespace evo {

ThreadPool::ThreadPool(std::size_t num_workers) {
  if (num_workers == 0) {
    throw std::invalid_argument("ThreadPool: num_workers must be > 0");
  }
  workers_.reserve(num_workers);
  for (std::size_t i = 0; i < num_workers; ++i) {
    workers_.emplace_back([this](std::stop_token st) { worker_loop(st); });
  }
}

ThreadPool::~ThreadPool() {
  drain();
}

bool ThreadPool::submit(Task task) {
  {
    std::lock_guard lock(mu_);
    if (draining_ || stopped_) {
      return false;
    }
    pending_.push(std::move(task));
    tasks_submitted_.fetch_add(1, std::memory_order_relaxed);
  }
  cv_.notify_one();
  return true;
}

void ThreadPool::drain() {
  {
    std::lock_guard lock(mu_);
    if (draining_) {
      return;  // already draining
    }
    draining_ = true;
    cv_.notify_all();
  }
  for (auto& w : workers_) {
    if (w.joinable()) {
      w.join();
    }
  }
}

void ThreadPool::stop() {
  {
    std::lock_guard lock(mu_);
    if (stopped_) {
      return;  // already stopped
    }
    stopped_ = true;
    cv_.notify_all();
  }
  for (auto& w : workers_) {
    if (w.joinable()) {
      w.request_stop();
    }
  }
  for (auto& w : workers_) {
    if (w.joinable()) {
      w.join();
    }
  }
}

std::vector<std::exception_ptr> ThreadPool::take_exceptions() {
  std::lock_guard lock(mu_);
  std::vector<std::exception_ptr> out;
  out.swap(exceptions_);
  return out;
}

void ThreadPool::worker_loop(std::stop_token st) {
  while (true) {
    Task task;
    {
      std::unique_lock lock(mu_);
      cv_.wait(lock, st, [&] {
        return !pending_.empty() || draining_ || st.stop_requested();
      });

      if (st.stop_requested()) {
        return;
      }

      if (pending_.empty() && draining_) {
        return;
      }

      if (pending_.empty()) {
        continue;  // spurious wakeup
      }

      task = std::move(pending_.front());
      pending_.pop();
    }

    active_workers_.fetch_add(1, std::memory_order_relaxed);
    try {
      task();
    } catch (...) {
      std::lock_guard lock(mu_);
      exceptions_.push_back(std::current_exception());
    }
    active_workers_.fetch_sub(1, std::memory_order_relaxed);
    tasks_completed_.fetch_add(1, std::memory_order_relaxed);
  }
}

}  // namespace evo