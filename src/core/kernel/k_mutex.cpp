#include "k_mutex.hpp"

namespace nemu::core::kernel {

KMutex::KMutex() : KAutoObject(HandleType::Unknown) {}

bool KMutex::TryLock(u64 tid) {
    std::unique_lock<std::mutex> lock(mutex_);
    if (owner_tid_ == 0) {
        owner_tid_ = tid;
        recursive_count_ = 1;
        return true;
    }
    if (owner_tid_ == tid) {
        recursive_count_++;
        return true;
    }
    return false;
}

void KMutex::Lock(u64 tid) {
    std::unique_lock<std::mutex> lock(mutex_);
    cv_.wait(lock, [&]() {
        return owner_tid_ == 0 || owner_tid_ == tid;
    });
    owner_tid_ = tid;
    recursive_count_++;
}

bool KMutex::Unlock(u64 tid) {
    std::unique_lock<std::mutex> lock(mutex_);
    if (owner_tid_ != tid) {
        return false;
    }
    recursive_count_--;
    if (recursive_count_ == 0) {
        owner_tid_ = 0;
        cv_.notify_one();
    }
    return true;
}

bool KMutex::IsLocked() const noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    return owner_tid_ != 0;
}

u64 KMutex::GetOwnerTid() const noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    return owner_tid_;
}

u32 KMutex::GetRecursiveCount() const noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    return recursive_count_;
}

} // namespace nemu::core::kernel
