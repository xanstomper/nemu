#pragma once

// Clean-room reimplementation of a lock-free single-producer / single-consumer
// bounded ring buffer (the classic technique used for the hot audio/GPU worker
// path in Switch/Wii emulators). Atomic head/tail indices avoid any lock; the
// capacity is a power of two so wrapping is a mask. Written fresh in Nemu's
// style; no source text copied.

#include "core/types.hpp"
#include <array>
#include <atomic>
#include <cassert>

namespace nemu::core::common {

/// Lock-free, single-producer / single-consumer ring of `N` elements.
/// `N` MUST be a power of two and >= 1. Concurrent writes are only allowed from
/// one producer and reads from one consumer (classic SPSC contract).
template <typename T, std::size_t N>
class SpscRingBuffer {
    static_assert((N & (N - 1)) == 0, "SpscRingBuffer capacity must be a power of two");
    static_assert(N > 0, "capacity must be > 0");

public:
    static constexpr std::size_t kCapacity = N;
    static constexpr std::size_t kMask = N - 1;

    /// Try to enqueue `value`; returns false if full. Producer-thread only.
    bool Push(const T& value) {
        const std::size_t head = head_.load(std::memory_order_relaxed);
        const std::size_t tail = tail_.load(std::memory_order_acquire);
        if (head - tail == N) {
            return false; // full
        }
        slots_[head & kMask] = value;
        head_.store(head + 1, std::memory_order_release);
        return true;
    }

    /// Try to dequeue into `out`; returns false if empty. Consumer-thread only.
    bool Pop(T& out) {
        const std::size_t tail = tail_.load(std::memory_order_relaxed);
        const std::size_t head = head_.load(std::memory_order_acquire);
        if (tail == head) {
            return false; // empty
        }
        out = slots_[tail & kMask];
        tail_.store(tail + 1, std::memory_order_release);
        return true;
    }

    [[nodiscard]] bool Empty() const noexcept {
        return Size() == 0;
    }

    [[nodiscard]] bool Full() const noexcept {
        return Size() == N;
    }

    [[nodiscard]] std::size_t Size() const noexcept {
        return head_.load(std::memory_order_relaxed) - tail_.load(std::memory_order_relaxed);
    }

    /// Reset both indices (not thread-safe against concurrent access; stop the
    /// workers first).
    void Reset() noexcept {
        head_.store(0, std::memory_order_relaxed);
        tail_.store(0, std::memory_order_relaxed);
    }

private:
    std::array<T, N> slots_{};
    alignas(64) std::atomic<std::size_t> head_{0};
    alignas(64) std::atomic<std::size_t> tail_{0};
};

} // namespace nemu::core::common