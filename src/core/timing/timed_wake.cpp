#include "core/timing/timed_wake.hpp"
#include <chrono>

namespace nemu::core::timing {

void TimedWake::Initialize() {
    if (!wake_event_) {
        wake_event_ = timing_.RegisterEvent(
            [this](Tick now, s64 late) { OnWake(now, late); }, "timed-wake");
    }
    Reset();
}

void TimedWake::ScheduleWake(Tick wake_at_ticks) {
    if (!wake_event_) {
        Initialize();
    }
    const Tick now = timing_.GetTicks();
    Reset();

    // If the deadline is already past, wake immediately.
    if (wake_at_ticks <= now) {
        flag_.store(true, std::memory_order_release);
        return;
    }
    // Schedule a one-shot absolute event for the absolute deadline.
    timing_.ScheduleEvent(std::chrono::nanoseconds(wake_at_ticks), wake_event_, /*absolute=*/true);
}

} // namespace nemu::core::timing