#pragma once

#include "core/types.hpp"
#include "core/memory/virtual_memory.hpp"
#include <mutex>
#include <condition_variable>
#include <unordered_map>

namespace nemu::core::kernel {

class KAddressArbiter {
public:
    KAddressArbiter() = default;
    ~KAddressArbiter() = default;

    /// Wait on address if memory value equals expected_val
    bool WaitForAddressIfEqual(
        memory::VirtualMemory& vm,
        vaddr_t address,
        u32 expected_val,
        s64 timeout_ns
    );

    /// Signal up to count threads waiting on address.
    /// lifo_wake: when set (per-title sync_relaxed tweak), wake ordering is
    /// biased to the most recently arrived waiter — some titles (Ryujinx
    /// 'deadlock' label, 29 games) hang on strict FIFO wakeups but progress
    /// with LIFO bias.
    u32 Signal(vaddr_t address, u32 count, bool lifo_wake = false);

private:
    struct WaitQueue {
        std::condition_variable cv;
        u32 waiting_threads{0};
    };

    std::mutex mutex_;
    std::unordered_map<vaddr_t, WaitQueue> queues_;
};

} // namespace nemu::core::kernel
