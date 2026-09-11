#include "core/common/spsc_ring_buffer.hpp"
#include "core/types.hpp"
#include <atomic>
#include <thread>
#include <vector>
#include <iostream>
#include <cstdlib>

using namespace nemu;
using namespace nemu::core::common;

#define R_ASSERT_FIRST_(a, ...) a
#define R_ASSERT(...) \
    do { \
        if (!(R_ASSERT_FIRST_(__VA_ARGS__))) { \
            std::cerr << "Assertion failed: " #__VA_ARGS__ << " at " \
                      << __FILE__ << ":" << __LINE__ << std::endl; \
            std::exit(1); \
        } \
    } while (0)

int main() {
    std::cout << "[Test: Lock-Free SPSC Ring Buffer]" << std::endl;

    // 1. Bounded push/pop and full/empty accounting (single-threaded).
    {
        SpscRingBuffer<int, 4> ring;
        R_ASSERT(ring.Empty() && !ring.Full());
        R_ASSERT(ring.Push(1) && ring.Push(2) && ring.Push(3) && ring.Push(4));
        R_ASSERT(ring.Full() && ring.Size() == 4);
        R_ASSERT(!ring.Push(5), "full -> push rejected");
        int v = 0;
        R_ASSERT(ring.Pop(v) && v == 1, "FIFO order");
        R_ASSERT(ring.Pop(v) && v == 2);
        R_ASSERT(ring.Size() == 2);
        R_ASSERT(ring.Push(9) && !ring.Full());
        R_ASSERT(ring.Pop(v) && v == 3);
        R_ASSERT(ring.Pop(v) && v == 4);
        R_ASSERT(ring.Pop(v) && v == 9);
        R_ASSERT(ring.Empty());
        R_ASSERT(!ring.Pop(v), "empty -> pop rejected");
    }

    // 2. Multithreaded SPSC: one producer, one consumer, exactly-once.
    {
        SpscRingBuffer<int, 128> ring;
        constexpr int kItems = 5000;
        std::atomic<int> sum{0};
        std::atomic<int> received{0};

        std::thread producer([&] {
            for (int i = 0; i < kItems; ++i) {
                while (!ring.Push(i)) {
                    std::this_thread::yield(); // ring full, wait for consumer
                }
            }
        });
        std::thread consumer([&] {
            int v = 0;
            while (received.load(std::memory_order_relaxed) < kItems) {
                if (ring.Pop(v)) {
                    sum.fetch_add(v, std::memory_order_relaxed);
                    received.fetch_add(1, std::memory_order_relaxed);
                }
            }
        });
        producer.join();
        consumer.join();

        R_ASSERT(received.load() == kItems);
        const int expected_total = kItems * (kItems - 1) / 2;
        R_ASSERT(sum.load() == expected_total, "all items received exactly once");
    }

    // 3. Reset clears accounting.
    {
        SpscRingBuffer<int, 8> ring;
        ring.Push(1);
        ring.Push(2);
        R_ASSERT(ring.Size() == 2);
        ring.Reset();
        R_ASSERT(ring.Empty() && ring.Size() == 0);
    }

    std::cout << "  - Lock-free SPSC ring buffer tests: PASSED" << std::endl;
    std::cout << "[Test: Lock-Free SPSC Ring Buffer PASSED]" << std::endl;
    return 0;
}