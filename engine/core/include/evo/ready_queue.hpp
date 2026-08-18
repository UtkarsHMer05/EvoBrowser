#pragma once

// Thread-safe blocking ready queue (Milestone 08).
//
// Single-producer/single-consumer-capable (SPSC) but designed for multi-
// producer / multi-consumer use. Uses std::mutex + std::condition_variable_any
// (the _any variant is required for stop_token-aware waits in C++20). FIFO
// ordering. Bounded or unbounded (max_size == 0 means unbounded).
//
// Ownership: each task is a NodeId. The queue owns no heap beyond its deque;
// NodeIds are value types. Shutdown is explicit via close().

#include <condition_variable>
#include <cstddef>
#include <deque>
#include <optional>
#include <stop_token>

#include "evo/dag.hpp"

namespace evo {

class ReadyQueue {
 public:
  // max_size == 0 => unbounded. max_size > 0 => push blocks when full.
  explicit ReadyQueue(std::size_t max_size = 0);

  // No copying: the queue owns a mutex + condition variables.
  ReadyQueue(const ReadyQueue&) = delete;
  ReadyQueue& operator=(const ReadyQueue&) = delete;

  // Enqueue a task. Blocks if the queue is full (bounded mode). Returns
  // false if the queue is closed (task was not added).
  bool push(NodeId id);

  // Blocking pop. Returns the next NodeId in FIFO order, or std::nullopt
  // when the queue is closed and empty. Honors stop_token: if stop is
  // requested, returns nullopt (possibly after popping a remaining item).
  std::optional<NodeId> pop(std::stop_token st = std::stop_token{});

  // Immediate (non-blocking) try-pop. Returns nullopt if empty (or closed
  // and empty). Never blocks.
  std::optional<NodeId> try_pop();

  // Close the queue: no further push calls accepted, all blocked/ future
  // pop callers are woken. Idempotent.
  void close();

  bool is_closed() const;
  std::size_t size() const;

 private:
  mutable std::mutex mu_;
  std::condition_variable_any cv_pop_;   // signaled on push or close
  std::condition_variable_any cv_push_;  // signaled on pop (space freed)
  std::deque<NodeId> queue_;
  std::size_t max_size_;
  bool closed_ = false;
};

}  // namespace evo
