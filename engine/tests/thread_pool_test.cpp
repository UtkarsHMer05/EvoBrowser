// Milestone 09 tests for the bounded std::jthread worker pool.
// Covers: 1/N worker counts, task exceptions, stop during wait,
// repeated construction/destruction, zero leaked/hanging threads.

#include <atomic>
#include <chrono>
#include <iostream>
#include <set>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include "evo/thread_pool.hpp"

namespace {

int failures = 0;

void check(bool ok, const std::string& what) {
  std::cout << (ok ? "  ok   " : "  FAIL ") << what << '\n';
  if (!ok) ++failures;
}

void test_worker_counts() {
  std::cout << "worker counts (1, 2, 4, 8)\n";
  for (std::size_t n : {1, 2, 4, 8}) {
    evo::ThreadPool pool(n);
    check(pool.num_workers() == n, "num_workers = " + std::to_string(n));
    check(pool.active_workers() == 0, "initially no active workers");

    std::atomic<int> counter{0};
    for (int i = 0; i < 100; ++i) {
      pool.submit([&counter]() { counter.fetch_add(1); });
    }
    pool.drain();

    check(counter.load() == 100, "all 100 tasks executed with " + std::to_string(n) + " workers");
    check(pool.num_tasks_submitted() == 100, "tasks_submitted = 100");
    check(pool.num_tasks_completed() == 100, "tasks_completed = 100");
    check(pool.active_workers() == 0, "all workers idle after drain");
  }
}

void test_task_exception() {
  std::cout << "task exception capture\n";
  evo::ThreadPool pool(2);
  pool.submit([]() { throw std::runtime_error("test error"); });
  pool.submit([]() { throw std::logic_error("second error"); });
  pool.drain();

  auto exceptions = pool.take_exceptions();
  check(exceptions.size() == 2, "two exceptions captured");

  bool has_runtime = false, has_logic = false;
  for (auto& ep : exceptions) {
    try {
      std::rethrow_exception(ep);
    } catch (const std::runtime_error& e) {
      if (std::string(e.what()) == "test error") has_runtime = true;
    } catch (const std::logic_error& e) {
      if (std::string(e.what()) == "second error") has_logic = true;
    }
  }
  check(has_runtime, "runtime_error captured");
  check(has_logic, "logic_error captured");

  // Second take_exceptions should return empty
  auto again = pool.take_exceptions();
  check(again.empty(), "second take_exceptions returns empty");
}

void test_stop_during_wait() {
  std::cout << "stop during wait (workers idle)\n";
  evo::ThreadPool pool(4);
  check(pool.active_workers() == 0, "initially idle");
  check(pool.num_tasks_submitted() == 0, "no tasks submitted");
  check(pool.num_tasks_completed() == 0, "no tasks completed");

  pool.stop();

  check(pool.active_workers() == 0, "still idle after stop");
  check(pool.num_tasks_submitted() == 0, "still no tasks");
  check(pool.num_tasks_completed() == 0, "still no completed");
}

void test_stop_with_pending_tasks() {
  std::cout << "stop abandons pending tasks\n";
  evo::ThreadPool pool(2);
  std::atomic<int> executed{0};

  // Submit tasks that increment a counter
  for (int i = 0; i < 100; ++i) {
    pool.submit([&executed]() {
      executed.fetch_add(1);
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    });
  }

  // Give some time for tasks to start
  std::this_thread::sleep_for(std::chrono::milliseconds(50));

  int before_stop = executed.load();
  pool.stop();  // should abandon remaining tasks

  // The stop should have prevented some tasks from executing
  // but already-running tasks complete
  int after_stop = executed.load();
  check(before_stop > 0, "some tasks started before stop");
  check(after_stop < 100, "not all 100 tasks executed (some abandoned)");
  check(pool.num_tasks_completed() == static_cast<std::size_t>(after_stop), "completed count matches executed");
}

void test_repeated_construction_destruction() {
  std::cout << "repeated construction/destruction (100 pools)\n";
  for (int i = 0; i < 100; ++i) {
    evo::ThreadPool pool(4);
    for (int j = 0; j < 10; ++j) {
      pool.submit([i, j]() {
        volatile int x = i + j;  // prevent optimization
        (void)x;
      });
    }
    pool.drain();
  }
  check(true, "100 pools created/destroyed without hanging");
}

void test_submit_after_shutdown_rejected() {
  std::cout << "submit after drain/stop rejected\n";
  evo::ThreadPool pool(2);
  pool.drain();
  check(!pool.submit([]() {}), "submit after drain returns false");

  evo::ThreadPool pool2(2);
  pool2.stop();
  check(!pool2.submit([]() {}), "submit after stop returns false");
}

void test_active_workers_counter() {
  std::cout << "active_workers counter\n";
  evo::ThreadPool pool(4);
  std::atomic<int> started{0};
  std::atomic<int> finished{0};

  for (int i = 0; i < 4; ++i) {
    pool.submit([&]() {
      started.fetch_add(1);
      std::this_thread::sleep_for(std::chrono::milliseconds(50));
      finished.fetch_add(1);
    });
  }

  // Wait a bit for tasks to start
  std::this_thread::sleep_for(std::chrono::milliseconds(20));
  check(pool.active_workers() > 0, "active_workers > 0 while tasks running");

  pool.drain();
  check(pool.active_workers() == 0, "active_workers == 0 after drain");
  check(started.load() == 4, "all 4 tasks started");
  check(finished.load() == 4, "all 4 tasks finished");
}

void test_zero_leaked_threads() {
  std::cout << "zero leaked/hanging threads (stress)\n";
  // Rapid create/destroy with tasks that could block
  for (int i = 0; i < 50; ++i) {
    evo::ThreadPool pool(8);
    std::atomic<int> done{0};

    for (int j = 0; j < 200; ++j) {
      pool.submit([&done]() {
        std::this_thread::sleep_for(std::chrono::microseconds(100));
        done.fetch_add(1);
      });
    }

    pool.drain();
    check(done.load() == 200, "all 200 tasks completed in iteration " + std::to_string(i));
  }
  check(true, "50 rapid pools with 200 tasks each - no leaks/hangs");
}

void test_drain_idempotent() {
  std::cout << "drain idempotent\n";
  evo::ThreadPool pool(2);
  pool.submit([]() {});
  pool.drain();
  pool.drain();  // second call should be no-op
  check(true, "double drain succeeds");
}

void test_stop_idempotent() {
  std::cout << "stop idempotent\n";
  evo::ThreadPool pool(2);
  pool.stop();
  pool.stop();  // second call should be no-op
  check(true, "double stop succeeds");
}

void test_drain_after_stop_noop() {
  std::cout << "drain after stop is no-op\n";
  evo::ThreadPool pool(2);
  pool.stop();
  pool.drain();  // should not hang
  check(true, "drain after stop returns immediately");
}

void test_exception_in_destructor_drain() {
  std::cout << "exception in task during destructor drain\n";
  {
    evo::ThreadPool pool(2);
    pool.submit([]() { throw std::runtime_error("from destructor test"); });
    pool.submit([]() { /* ok */ });
    // Destructor calls drain - exceptions should be captured internally
  }
  check(true, "destructor with exceptions does not terminate process");
}

}  // namespace

int main() {
  test_worker_counts();
  test_task_exception();
  test_stop_during_wait();
  test_stop_with_pending_tasks();
  test_repeated_construction_destruction();
  test_submit_after_shutdown_rejected();
  test_active_workers_counter();
  test_zero_leaked_threads();
  test_drain_idempotent();
  test_stop_idempotent();
  test_drain_after_stop_noop();
  test_exception_in_destructor_drain();

  if (failures != 0) {
    std::cout << failures << " thread-pool check(s) failed\n";
    return 1;
  }
  std::cout << "all thread-pool tests passed\n";
  return 0;
}