#include "nvhost_ctrl.hpp"
#include "platform/logger.hpp"

namespace nemu::core::gpu::nvhost {

SyncpointManager::SyncpointManager() {
    for (size_t i = 0; i < NUM_SYNCPOINTS; ++i) {
        syncpoints_[i].store(0, std::memory_order_relaxed);
    }
}

u32 SyncpointManager::Read(u32 id) const noexcept {
    if (id >= NUM_SYNCPOINTS) return 0;
    return syncpoints_[id].load(std::memory_order_acquire);
}

u32 SyncpointManager::Increment(u32 id) noexcept {
    return Increment(id, 1);
}

u32 SyncpointManager::Increment(u32 id, u32 count) noexcept {
    if (id >= NUM_SYNCPOINTS || count == 0) return 0;
    const u32 new_val = syncpoints_[id].fetch_add(count, std::memory_order_acq_rel) + count;
    {
        std::unique_lock lock(cv_mutex_);
    }
    cv_.notify_all();
    return new_val;
}

void SyncpointManager::Set(u32 id, u32 val) noexcept {
    if (id >= NUM_SYNCPOINTS) return;
    syncpoints_[id].store(val, std::memory_order_release);
    {
        std::unique_lock lock(cv_mutex_);
    }
    cv_.notify_all();
}

bool SyncpointManager::IsSatisfied(u32 id, u32 threshold) const noexcept {
    if (id >= NUM_SYNCPOINTS) return false;
    const u32 cur = syncpoints_[id].load(std::memory_order_acquire);
    // Tegra modular comparison: (cur - threshold) <= (1U << 30)
    return static_cast<s32>(cur - threshold) >= 0;
}

bool SyncpointManager::Wait(u32 id, u32 threshold, s32 timeout_ms) noexcept {
    if (id >= NUM_SYNCPOINTS) return false;
    if (IsSatisfied(id, threshold)) return true;
    if (timeout_ms == 0) return false;

    std::unique_lock lock(cv_mutex_);
    if (timeout_ms < 0) {
        cv_.wait(lock, [this, id, threshold] {
            return IsSatisfied(id, threshold);
        });
        return true;
    } else {
        return cv_.wait_for(lock, std::chrono::milliseconds(timeout_ms), [this, id, threshold] {
            return IsSatisfied(id, threshold);
        });
    }
}

void SyncpointManager::RegisterUserEvent(u32 user_event_id, u32 syncpoint_id, u32 threshold) {
    std::unique_lock lock(cv_mutex_);
    user_events_[user_event_id] = UserEvent{syncpoint_id, threshold};
    NEMU_LOG_DEBUG("Syncpoint", "Registered user event 0x{:X} for syncpt {} thresh {}",
                   user_event_id, syncpoint_id, threshold);
}

bool SyncpointManager::CheckUserEvent(u32 user_event_id) {
    std::unique_lock lock(cv_mutex_);
    auto it = user_events_.find(user_event_id);
    if (it == user_events_.end()) return false;
    const bool satisfied = IsSatisfied(it->second.syncpoint_id, it->second.threshold);
    if (satisfied) {
        user_events_.erase(it);
    }
    return satisfied;
}

} // namespace nemu::core::gpu::nvhost
