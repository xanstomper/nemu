#pragma once

#include "core/types.hpp"
#include <functional>
#include <vector>
#include <mutex>
#include <atomic>
#include <string_view>

namespace nemu::platform {

enum class AppLifecycleState : u32 {
    Running = 0,
    Suspending = 1,
    Suspended = 2,
    Resuming = 3
};

class XboxLifecycleManager {
public:
    XboxLifecycleManager();
    ~XboxLifecycleManager() = default;

    /// Current lifecycle state of the UWP/Xbox application.
    [[nodiscard]] AppLifecycleState GetState() const noexcept;

    /// Register callback invoked when the OS requests application suspension.
    void RegisterSuspendingCallback(std::function<void()> cb);

    /// Register callback invoked when the OS resumes the application from suspension.
    void RegisterResumingCallback(std::function<void()> cb);

    /// Trigger suspension sequence (flushes saves, trims DXGI, pauses emulation threads).
    void OnSuspending();

    /// Trigger resumption sequence (re-acquires audio endpoints, resumes timing clocks).
    void OnResuming();

private:
    std::atomic<AppLifecycleState> state_{AppLifecycleState::Running};
    std::mutex hook_mutex_;
    std::vector<std::function<void()>> suspending_hooks_;
    std::vector<std::function<void()>> resuming_hooks_;
};

} // namespace nemu::platform
