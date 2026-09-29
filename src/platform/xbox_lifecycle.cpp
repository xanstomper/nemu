#include "xbox_lifecycle.hpp"
#include "logger.hpp"

namespace nemu::platform {

XboxLifecycleManager::XboxLifecycleManager() {
    NEMU_LOG_INFO("Lifecycle", "Initialized Xbox UWP application lifecycle governor");
}

AppLifecycleState XboxLifecycleManager::GetState() const noexcept {
    return state_.load(std::memory_order_relaxed);
}

void XboxLifecycleManager::RegisterSuspendingCallback(std::function<void()> cb) {
    std::lock_guard lock(hook_mutex_);
    suspending_hooks_.push_back(std::move(cb));
}

void XboxLifecycleManager::RegisterResumingCallback(std::function<void()> cb) {
    std::lock_guard lock(hook_mutex_);
    resuming_hooks_.push_back(std::move(cb));
}

void XboxLifecycleManager::OnSuspending() {
    state_.store(AppLifecycleState::Suspending, std::memory_order_relaxed);
    NEMU_LOG_INFO("Lifecycle", "Xbox OS suspension signal received. Executing pre-suspend tasks...");

    std::lock_guard lock(hook_mutex_);
    for (const auto& hook : suspending_hooks_) {
        if (hook) hook();
    }

    state_.store(AppLifecycleState::Suspended, std::memory_order_relaxed);
    NEMU_LOG_INFO("Lifecycle", "UWP application suspended safely");
}

void XboxLifecycleManager::OnResuming() {
    state_.store(AppLifecycleState::Resuming, std::memory_order_relaxed);
    NEMU_LOG_INFO("Lifecycle", "Xbox OS resumption signal received. Restoring emulation state...");

    std::lock_guard lock(hook_mutex_);
    for (const auto& hook : resuming_hooks_) {
        if (hook) hook();
    }

    state_.store(AppLifecycleState::Running, std::memory_order_relaxed);
    NEMU_LOG_INFO("Lifecycle", "UWP application resumed into active running state");
}

} // namespace nemu::platform
