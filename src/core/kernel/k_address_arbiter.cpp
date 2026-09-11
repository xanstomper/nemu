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

u32 KAddressArbiter::Signal(vaddr_t address, u32 count) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = queues_.find(address);
    if (it == queues_.end()) {
        return 0;
    }

    auto& q = it->second;
    u32 to_wake = std::min(count, q.waiting_threads);
    for (u32 i = 0; i < to_wake; ++i) {
        q.cv.notify_one();
    }
    return to_wake;
}

} // namespace nemu::core::kernel
