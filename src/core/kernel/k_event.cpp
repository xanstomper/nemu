#include "k_event.hpp"

namespace nemu::core::kernel {

KEvent::KEvent(bool auto_clear)
    : KAutoObject(HandleType::Event), auto_clear_(auto_clear) {}

void KEvent::Signal() {
    std::lock_guard lock(mutex_);
    signaled_ = true;
    cv_.notify_all();
}

void KEvent::Clear() {
    std::lock_guard lock(mutex_);
    signaled_ = false;
}

bool KEvent::IsSignaled() const {
    std::lock_guard lock(mutex_);
    return signaled_;
}

bool KEvent::Wait(std::chrono::nanoseconds timeout) {
    std::unique_lock lock(mutex_);
    if (signaled_) {
        if (auto_clear_) signaled_ = false;
        return true;
    }

    if (timeout == std::chrono::nanoseconds::zero()) {
        return false;
    }

    if (timeout == std::chrono::nanoseconds::max()) {
        cv_.wait(lock, [this] { return signaled_; });
        if (auto_clear_) signaled_ = false;
        return true;
    }

    const bool acquired = cv_.wait_for(lock, timeout, [this] { return signaled_; });
    if (acquired && auto_clear_) {
        signaled_ = false;
    }
    return acquired;
}

} // namespace nemu::core::kernel
