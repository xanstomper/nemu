#pragma once

// Clean-room implementation of a future-event scheduler for emulated CPU time.
//
// This is the standard "core timing" technique used across Switch/Wii 3DS
// emulators (the yuzu/Eden lineage, Dolphin, Citra): a scheduler that fires
// registered event callbacks at emulated-cycles-into-the-future deadlines.
// This file is an original reimplementation in Nemu's own style; it mirrors the
// general design (register event type + callback, schedule at a tick deadline,
// advance time and fire due events, loop events auto-reschedule, track late
// drift) but contains no copied source text from any project.

#include "core/types.hpp"
#include <chrono>
#include <functional>
#include <memory>
#include <mutex>
#include <queue>
#include <string>
#include <vector>

namespace nemu::core::timing {

using Tick = u64;                                   ///< Emulated time in CPU cycles.
using Nanoseconds = std::chrono::nanoseconds;

/// A callback scheduled to run at a future tick. Receives the current tick the
/// event fired at and how late (in ticks) it was, so repeating events can
/// compensate for drift on the next schedule.
using TimedCallback = std::function<void(Tick now, s64 ns_late)>;

/// Identifies a class of event. Registers a callback and a human-readable name.
struct EventType {
    EventType(TimedCallback callback_, std::string name_)
        : callback(std::move(callback_)), name(std::move(name_)), seq(0) {}

    TimedCallback callback;
    std::string name;
    u64 seq; // bumped whenever an event of this type is scheduled, to detect stalls
};

/// The core timing scheduler.
///
/// Time is an abstract "tick" counter. Callers register event types, then
/// schedule/loop/unschedule events at absolute or relative tick deadlines.
/// Advancing the scheduler by calling AddTicks(n) (or AdvanceTo(tick)) fires
/// every event whose deadline is now due, in deadline order.
class CoreTiming {
public:
    CoreTiming() = default;
    ~CoreTiming() = default;
    CoreTiming(const CoreTiming&) = delete;
    CoreTiming& operator=(const CoreTiming&) = delete;

    /// Register a new event type. The returned handle is stable for the
    /// lifetime of the scheduler.
    std::shared_ptr<EventType> RegisterEvent(TimedCallback callback, std::string name);

    /// Schedule `event` to fire `into_future` ticks from now (or at an
    /// absolute tick when absolute_time is true).
    void ScheduleEvent(Nanoseconds into_future, const std::shared_ptr<EventType>& event,
                       bool absolute_time = false);

    /// Schedule `event` to fire at start_time, then repeatedly reschedule
    /// itself every `resched_time` until unscheduled.
    void ScheduleLoopingEvent(Nanoseconds start_time, Nanoseconds resched_time,
                              const std::shared_ptr<EventType>& event,
                              bool absolute_time = false);

    /// Remove all pending occurrences of `event`.
    void UnscheduleEvent(const std::shared_ptr<EventType>& event);

    /// Advance the scheduler by `ticks` and fire any events now due.
    void AddTicks(u64 ticks);

    /// Fire any events due at or before the current time without moving time.
    void FireDueEvents();

    /// Set the current tick to a specific value (used by tests / save+restore).
    void ResetTicks(u64 value = 0) { global_timer_ = value; }

    /// Get the current emulated tick.
    [[nodiscard]] u64 GetTicks() const noexcept { return global_timer_; }

    /// Remove all pending events (call on shutdown only).
    void ClearPendingEvents();

    /// Number of pending scheduled events.
    [[nodiscard]] std::size_t GetPendingEventCount() const noexcept { return queue_.size(); }

private:
    struct EventEntry {
        u64 deadline;                         ///< tick at which the event fires
        u64 reported_time;                    ///< the timer value when fired (for ns_late)
        std::shared_ptr<EventType> type;
        bool is_looping;
        u64 resched_ticks;                    ///< for looping events
        u64 seq;
    };

    struct DeadlineOrder {
        bool operator()(const EventEntry& a, const EventEntry& b) const noexcept {
            // priority_queue is a max-heap; invert to get earliest-deadline-first.
            return a.deadline > b.deadline;
        }
    };

    /// Convert an ns duration into emulated tick units (constant 1 ns == 1 tick).
    [[nodiscard]] static u64 TicksFromNs(Nanoseconds ns) noexcept {
        const auto n = ns.count();
        return n > 0 ? static_cast<u64>(n) : 0u;
    }

    u64 global_timer_{0};
    u64 event_seq_{0};
    std::priority_queue<EventEntry, std::vector<EventEntry>, DeadlineOrder> queue_;
    std::mutex mutex_;
};

} // namespace nemu::core::timing