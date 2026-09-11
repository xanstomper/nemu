#pragma once

#include "core/types.hpp"
#include <array>
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <unordered_map>
#include <chrono>

namespace nemu::core::gpu::nvhost {

constexpr size_t NUM_SYNCPOINTS = 192;

struct SyncpointWaiter {
    u32 id{0};
    u32 threshold{0};
    bool satisfied{false};
};

class SyncpointManager {
public:
    SyncpointManager();
    ~SyncpointManager() = default;

    SyncpointManager(const SyncpointManager&) = delete;
    SyncpointManager& operator=(const SyncpointManager&) = delete;

    /// Read current value of a syncpoint.
    [[nodiscard]] u32 Read(u32 id) const noexcept;

    /// Increment syncpoint by 1, notifying any waiters whose threshold is satisfied.
    u32 Increment(u32 id) noexcept;

    /// Increment syncpoint by a specific amount.
    u32 Increment(u32 id, u32 count) noexcept;

    /// Set syncpoint value directly.
    void Set(u32 id, u32 val) noexcept;

    /// Wait until syncpoint reaches or exceeds threshold, or timeout expires (timeout_ms < 0 means infinite).
    /// Returns true if condition satisfied, false if timed out.
    bool Wait(u32 id, u32 threshold, s32 timeout_ms = -1) noexcept;

    /// Check whether syncpoint has reached threshold without blocking.
    [[nodiscard]] bool IsSatisfied(u32 id, u32 threshold) const noexcept;

    /// Register a user event ID bound to a syncpoint threshold.
    void RegisterUserEvent(u32 user_event_id, u32 syncpoint_id, u32 threshold);

    /// Check and unregister user event.
    bool CheckUserEvent(u32 user_event_id);

private:
    std::array<std::atomic<u32>, NUM_SYNCPOINTS> syncpoints_{};
    mutable std::mutex cv_mutex_;
    std::condition_variable cv_;

    struct UserEvent {
        u32 syncpoint_id{0};
        u32 threshold{0};
    };
    std::unordered_map<u32, UserEvent> user_events_;
};

} // namespace nemu::core::gpu::nvhost
