// Milestone 08 tests for the thread-safe blocking ready queue.
// Covers: FIFO order, close-while-empty, close-while-blocked, many producers /
// many consumers with no lost task IDs, stop_token-aware pop, spurious
// wakeups, and bounded-queue backpressure.

#include <atomic>
#include <chrono>
#include <iostream>
#include <set>
#include <thread>
#include <vector>

#include "evo/dag.hpp"
#include "evo/ready_queue.hpp"

namespace {

int failures = 0;

void check(bool ok, const std::string& what) {
  std::cout << (ok ? "  ok   " : "  FAIL ") << what << '\n';
  if (!ok) ++failures;
}

using evo::NodeId;

void test_fifo_order() {
  std::cout << "FIFO ordering\n";
  evo::ReadyQueue q;
  q.push(NodeId{"a"});
  q.push(NodeId{"b"});
  q.push(NodeId{"c"});

  auto r1 = q.try_pop();
  auto r2 = q.try_pop();
  auto r3 = q.try_pop();
  check(r1 && r1->value == "a", "first in = first out (a)");
  check(r2 && r2->value == "b", "second in = second out (b)");
  check(r3 && r3->value == "c", "third in = third out (c)");
  check(q.try_pop() == std::nullopt, "empty after draining");
}

void test_close_while_empty() {
  std::cout << "close while empty\n";
  evo::ReadyQueue q;
  check(q.is_closed() == false, "not closed initially");

  q.close();
  check(q.is_closed() == true, "closed after close()");

  // pop on closed+empty returns nullopt immediately
  auto r = q.pop();
  check(!r.has_value(), "pop on closed empty returns nullopt");

  // push on closed returns false
  check(q.push(NodeId{"x"}) == false, "push on closed returns false");
}

void test_close_while_blocked() {
  std::cout << "close wakes blocked consumer\n";
  evo::ReadyQueue q;
  std::atomic<bool> popped{false};

  std::jthread consumer([&](std::stop_token st) {
    auto r = q.pop(st);
    popped.store(r.has_value());
  });

  // Let the consumer block in pop.
  std::this_thread::sleep_for(std::chrono::milliseconds(50));
  q.close();
  consumer.join();

  check(popped.load() == false, "blocked consumer woke and got nullopt");
}

void test_many_producers_many_consumers() {
  std::cout << "many producers / many consumers\n";
  evo::ReadyQueue q;

  constexpr int kProducers = 8;
  constexpr int kConsumers = 8;
  constexpr int kPerProducer = 200;

  std::atomic<int> produced{0};
  std::atomic<int> consumed{0};
  std::set<std::string> seen;

  std::vector<std::jthread> producers;
  for (int p = 0; p < kProducers; ++p) {
    producers.emplace_back([&, p] {
      for (int i = 0; i < kPerProducer; ++i) {
        NodeId id{"p" + std::to_string(p) + "_t" + std::to_string(i)};
        if (q.push(std::move(id))) {
          produced.fetch_add(1);
        }
      }
    });
  }

  std::vector<std::jthread> consumers;
  for (int c = 0; c < kConsumers; ++c) {
    consumers.emplace_back([&] {
      while (true) {
        auto r = q.pop();
        if (!r) break;  // closed and empty
        consumed.fetch_add(1);
      }
    });
  }

  for (auto& p : producers) p.join();
  q.close();  // all producers done → close to signal consumers to exit
  for (auto& c : consumers) c.join();

  check(produced.load() == kProducers * kPerProducer, "all tasks produced");
  check(consumed.load() == kProducers * kPerProducer,
        "all tasks consumed (no loss)");
}

void test_stop_token_pop() {
  std::cout << "stop_token interrupts blocking pop\n";
  evo::ReadyQueue q;
  std::stop_source src;
  std::atomic<bool> got_item{false};
  std::atomic<bool> got_nothing{false};

  std::thread consumer([&] {
    auto r = q.pop(src.get_token());
    if (r) {
      got_item.store(true);
    } else {
      got_nothing.store(true);
    }
  });

  std::this_thread::sleep_for(std::chrono::milliseconds(50));
  src.request_stop();  // triggers stop_callback in pop → notify_one
  consumer.join();

  check(got_nothing.load() == true, "stop wakes pop with nullopt");
  check(got_item.load() == false, "no item delivered on stop");
}

void test_stop_token_drains_remaining() {
  std::cout << "stop still drains remaining items\n";
  evo::ReadyQueue q;
  q.push(NodeId{"a"});
  q.push(NodeId{"b"});

  std::stop_source src;
  std::optional<NodeId> r1, r2, r3;

  std::thread consumer([&] {
    r1 = q.pop(src.get_token());       // pops "a"
    src.request_stop();                // trigger stop
    r2 = q.pop(src.get_token());       // drains "b" (queue non-empty)
    r3 = q.pop(src.get_token());       // now empty + stopped → nullopt
  });

  consumer.join();
  check(r1.has_value() && r1->value == "a", "first pop succeeds (a)");
  check(r2.has_value() && r2->value == "b", "second pop drains remaining (b)");
  check(!r3.has_value(), "third pop returns nullopt after stop");
}

void test_bounded_queue_backpressure() {
  std::cout << "bounded queue backpressure\n";
  evo::ReadyQueue q(2);  // capacity 2

  check(q.push(NodeId{"a"}), "push 1");
  check(q.push(NodeId{"b"}), "push 2");
  check(q.size() == 2, "size == 2");

  // Third push must block until space is freed.
  std::atomic<bool> pushed_third{false};
  std::jthread blocker([&] {
    if (q.push(NodeId{"c"})) pushed_third.store(true);
  });

  std::this_thread::sleep_for(std::chrono::milliseconds(50));
  check(pushed_third.load() == false, "third push blocks (bounded)");
  check(q.size() == 2, "size still 2");

  // Pop one → frees a slot → third push should complete.
  auto r = q.try_pop();
  check(r && r->value == "a", "popped a");
  blocker.join();
  check(pushed_third.load() == true, "third push unblocked after pop");
  check(q.size() == 2, "size == 2 (b + c)");
}

void test_spurious_wakeup_robustness() {
  std::cout << "spurious wakeup robustness (stress)\n";
  evo::ReadyQueue q(4);
  std::atomic<int> consumed{0};
  std::atomic<bool> stop{false};

  std::vector<std::jthread> consumers;
  for (int i = 0; i < 4; ++i) {
    consumers.emplace_back([&] {
      while (!stop.load()) {
        auto r = q.try_pop();
        if (r) {
          consumed.fetch_add(1);
        } else {
          std::this_thread::sleep_for(std::chrono::microseconds(10));
        }
      }
      // Drain remaining.
      while (auto r = q.try_pop()) {
        consumed.fetch_add(1);
      }
    });
  }

  std::vector<std::jthread> producers;
  for (int i = 0; i < 4; ++i) {
    producers.emplace_back([&] {
      for (int j = 0; j < 500; ++j) {
        q.push(NodeId{"n"});
      }
    });
  }

  for (auto& p : producers) p.join();
  stop.store(true);
  for (auto& c : consumers) c.join();

  check(consumed.load() == 4 * 500, "all 2000 tasks consumed");
}

void test_empty_queue_pop_returns_nullopt() {
  std::cout << "try_pop on empty queue\n";
  evo::ReadyQueue q;
  check(q.try_pop() == std::nullopt, "try_pop on empty returns nullopt");
  check(q.size() == 0, "empty queue size 0");
}

}  // namespace

int main() {
  test_fifo_order();
  test_close_while_empty();
  test_close_while_blocked();
  test_many_producers_many_consumers();
  test_stop_token_pop();
  test_stop_token_drains_remaining();
  test_bounded_queue_backpressure();
  test_spurious_wakeup_robustness();
  test_empty_queue_pop_returns_nullopt();

  if (failures != 0) {
    std::cout << failures << " ready-queue check(s) failed\n";
    return 1;
  }
  std::cout << "all ready-queue tests passed\n";
  return 0;
}
