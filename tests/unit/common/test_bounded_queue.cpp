#include "core/common/bounded_threadsafe_queue.hpp"
#include <atomic>
#include <thread>
#include <vector>
#include <iostream>
#include <cstdlib>

using namespace nemu::core::common;

// Use only the first argument as the condition so an optional message string
// does not silently become a comma-expression (always-true).
#define BT_ASSERT_FIRST_(a, ...) a
#define BT_ASSERT(...) \
    do { \
        if (!(BT_ASSERT_FIRST_(__VA_ARGS__))) { \
            std::cerr << "Assertion failed: " #__VA_ARGS__ << " at " \
                      << __FILE__ << ":" << __LINE__ << std::endl; \
            std::exit(1); \
        } \
    } while (0)

int main() {
    std::cout << "[Test: Bounded Thread-Safe Queue]" << std::endl;

    // 1. FIFO order + size accounting.
    {
        BoundedThreadsafeQueue<int> q(4);
        BT_ASSERT(q.Capacity() == 4 && q.Empty());
        q.Push(1);
        q.Push(2);
        q.Emplace(3); // for int, Emplace(int&&) == push
        BT_ASSERT(q.Size() == 3 && !q.Empty());
        BT_ASSERT(q.Pop() == 1 && q.Pop() == 2 && q.Pop() == 3);
        BT_ASSERT(q.Empty());
    }

    // 2. Non-blocking try-push respects capacity.
    {
        BoundedThreadsafeQueue<int> q(2);
        BT_ASSERT(q.TryPush(10) && q.TryPush(20));
        BT_ASSERT(!q.TryPush(30), "full -> try-push rejected");
        BT_ASSERT(q.TryPop().value() == 10);
        BT_ASSERT(q.TryPop().value() == 20);
        const auto no_more = q.TryPop();
        BT_ASSERT(!no_more.has_value() && q.Empty(), "empty -> try-pop rejected");
    }

    // 3. Multithreaded producer/consumer hand-off (correctness + no deadlock).
    {
        BoundedThreadsafeQueue<int> q(8);
        constexpr int kItems = 1000;
        constexpr int kProducers = 4;
        std::atomic<int> sum{0};
        std::atomic<int> received{0};

        std::vector<std::thread> producers;
        for (int p = 0; p < kProducers; ++p) {
            producers.emplace_back([&q, p] {
                for (int i = 0; i < kItems; ++i) {
                    q.Push(p * kItems + i);
                }
            });
        }
        std::thread consumer([&q, &sum, &received] {
            while (received.load(std::memory_order_relaxed) < kItems * kProducers) {
                const int v = q.Pop(); // blocking; deterministic totals
                sum.fetch_add(v, std::memory_order_relaxed);
                received.fetch_add(1, std::memory_order_relaxed);
            }
        });
        for (auto& t : producers) t.join();
        consumer.join();

        BT_ASSERT(received.load() == kItems * kProducers);
        // The union of p*kItems + 0..kItems-1 across p == exactly 0..(total-1).
        const int total_items = kItems * kProducers;
        const int expected_total = total_items * (total_items - 1) / 2;
        BT_ASSERT(sum.load() == expected_total, "all items received exactly once");
    }

    std::cout << "  - Bounded thread-safe queue tests: PASSED" << std::endl;
    std::cout << "[Test: Bounded Thread-Safe Queue PASSED]" << std::endl;
    return 0;
}