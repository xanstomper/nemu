#pragma once

// Clean-room reimplementation of a core-timing-paced sleep (the technique used
// by Switch/Wii emulators to make a guest thread "sleep until a future time" by
// scheduling a timed wake-up on the timing scheduler instead of sleeping the
// host thread). Written fresh in Nemu's style; no source text copied.

#include "core/timing/core_timing.hpp"
#include <atomic>
#include <chrono>
#include <memory>

namespace nemu::core::timing {

/// A single timed wakeup driven by a CoreTiming scheduler. A guest thread that
/// "sleeps" until an absolute tick registers itself with this object; the emulator
/// advances CoreTiming, which fires the wake event and sets the flag. The thread
/// then observes the flag. This never blocks a host thread on sleep_for.
class TimedWake {
public:
    explicit TimedWake(CoreTiming& timing) : timing_(timing) {}

    TimedWake(const TimedWake&) = delete;
    TimedWake& operator=(const TimedWake&) = delete;

    /// Arrange for the wake flag to be set when the scheduler reaches
    /// `wake_at_ticks`. Safe to call multiple times; only the latest schedule
    /// is active.
    void ScheduleWake(Tick wake_at_ticks);

    /// Register the event type with the timing scheduler (call once, before use).
    void Initialize();

    /// Clear the wake flag.
    void Reset() noexcept { flag_.store(false, std::memory_order_relaxed); }

    /// True once the scheduled deadline has passed and the wake fired.
    bool HasWoken() const noexcept { return flag_.load(std::memory_order_acquire); }

private:
    void OnWake(Tick /*now*/, s64 /*late*/) { flag_.store(true, std::memory_order_release); }

    CoreTiming& timing_;
    std::shared_ptr<EventType> wake_event_;
    std::atomic<bool> flag_{false};
};

} // namespace nemu::core::timing