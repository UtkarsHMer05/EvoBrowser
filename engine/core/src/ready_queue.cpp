#include "evo/ready_queue.hpp"

namespace evo {

ReadyQueue::ReadyQueue(std::size_t max_size) : max_size_(max_size) {}

bool ReadyQueue::push(NodeId id) {
  std::unique_lock lock(mu_);
  if (closed_) return false;
  if (max_size_ > 0) {
    cv_push_.wait(lock, [&] { return queue_.size() < max_size_ || closed_; });
    if (closed_) return false;
  }
  queue_.push_back(std::move(id));
  cv_pop_.notify_one();
  return true;
}

std::optional<NodeId> ReadyQueue::pop(std::stop_token st) {
  std::unique_lock lock(mu_);

  // Register a callback so that stop_request wakes the condition variable.
  // When stop is requested the callback fires notify_one; the predicate
  // then admits the wait and we return nullopt (or drain a remaining item).
  std::stop_callback cb(st, [&] { cv_pop_.notify_one(); });

  cv_pop_.wait(lock, [&] {
    return !queue_.empty() || closed_ || st.stop_requested();
  });

  if (queue_.empty()) {
    // Closed+empty, or stopped with no items left.
    return std::nullopt;
  }

  NodeId id = std::move(queue_.front());
  queue_.pop_front();
  cv_push_.notify_one();  // freed a slot in bounded mode
  return id;
}

std::optional<NodeId> ReadyQueue::try_pop() {
  std::lock_guard lock(mu_);
  if (queue_.empty()) return std::nullopt;
  NodeId id = std::move(queue_.front());
  queue_.pop_front();
  cv_push_.notify_one();
  return id;
}

void ReadyQueue::close() {
  std::lock_guard lock(mu_);
  closed_ = true;
  cv_pop_.notify_all();
  cv_push_.notify_all();
}

bool ReadyQueue::is_closed() const {
  std::lock_guard lock(mu_);
  return closed_;
}

std::size_t ReadyQueue::size() const {
  std::lock_guard lock(mu_);
  return queue_.size();
}

}  // namespace evo
