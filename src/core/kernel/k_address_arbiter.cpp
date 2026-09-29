#include "k_address_arbiter.hpp"
#include <chrono>

namespace nemu::core::kernel {

bool KAddressArbiter::WaitForAddressIfEqual(
    memory::VirtualMemory& vm,
    vaddr_t address,
    u32 expected_val,
    s64 timeout_ns
) {
    std::unique_lock<std::mutex> lock(mutex_);

    // Check current value in virtual memory
    if (vm.Read32(address) != expected_val) {
        return false;
    }

    auto& q = queues_[address];
    q.waiting_threads++;

    bool wait_success = true;
    if (timeout_ns < 0) {
        q.cv.wait(lock);
    } else {
        auto status = q.cv.wait_for(lock, std::chrono::nanoseconds(timeout_ns));
        if (status == std::cv_status::timeout) {
            wait_success = false;
        }
    }

    q.waiting_threads--;
    if (q.waiting_threads == 0) {
        queues_.erase(address);
    }

    return wait_success;
}

u32 KAddressArbiter::Signal(vaddr_t address, u32 count, bool lifo_wake) {
    (void)lifo_wake; // single-shared-CV HLE: ordering-neutral; documented refinement
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = queues_.find(address);
    if (it == queues_.end()) {
        return 0;
    }

    auto& q = it->second;
    u32 to_wake = std::min(count, q.waiting_threads);
    for (u32 i = 0; i < to_wake; ++i) {
        // std::condition_variable has no ordering guarantee; the notify_all +
        // wait-order model is already "relaxed" for FIFO/LIFO purposes. The
        // lifo_wake flag documents the per-title intent — with a single
        // shared cv (the current HLE model), both orderings wake the same
        // set; per-thread arrival tracking is the future refinement.
        q.cv.notify_one();
    }
    return to_wake;
}

bool KAddressArbiter::WaitForAddressIfLessThan(
    memory::VirtualMemory& vm,
    vaddr_t address,
    u32 compare_val,
    s64 timeout_ns
) {
    std::unique_lock<std::mutex> lock(mutex_);

    // Check if memory value is less than compare_val
    if (vm.Read32(address) >= compare_val) {
        return false;
    }

    auto& q = queues_[address];
    q.waiting_threads++;

    bool wait_success = true;
    if (timeout_ns < 0) {
        q.cv.wait(lock);
    } else {
        auto status = q.cv.wait_for(lock, std::chrono::nanoseconds(timeout_ns));
        if (status == std::cv_status::timeout) {
            wait_success = false;
        }
    }

    q.waiting_threads--;
    if (q.waiting_threads == 0) {
        queues_.erase(address);
    }

    return wait_success;
}

bool KAddressArbiter::DecrementAndWaitIfLessThan(
    memory::VirtualMemory& vm,
    vaddr_t address,
    u32 compare_val,
    s64 timeout_ns
) {
    std::unique_lock<std::mutex> lock(mutex_);

    const u32 cur = vm.Read32(address);
    vm.Write32(address, cur - 1);

    if (cur >= compare_val) {
        return false;
    }

    auto& q = queues_[address];
    q.waiting_threads++;

    bool wait_success = true;
    if (timeout_ns < 0) {
        q.cv.wait(lock);
    } else {
        auto status = q.cv.wait_for(lock, std::chrono::nanoseconds(timeout_ns));
        if (status == std::cv_status::timeout) {
            wait_success = false;
        }
    }

    q.waiting_threads--;
    if (q.waiting_threads == 0) {
        queues_.erase(address);
    }

    return wait_success;
}

u32 KAddressArbiter::SignalAndModifyByWaitingCount(
    memory::VirtualMemory& vm,
    vaddr_t address,
    u32 count,
    s32 value_modifier
) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = queues_.find(address);
    if (it == queues_.end()) {
        return 0;
    }

    auto& q = it->second;
    const u32 waiting = q.waiting_threads;
    if (waiting > 0 && value_modifier != 0) {
        const u32 cur = vm.Read32(address);
        vm.Write32(address, static_cast<u32>(static_cast<s32>(cur) + value_modifier));
    }

    u32 to_wake = std::min(count, waiting);
    for (u32 i = 0; i < to_wake; ++i) {
        q.cv.notify_one();
    }
    return to_wake;
}

} // namespace nemu::core::kernel
