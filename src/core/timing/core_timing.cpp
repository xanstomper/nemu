#include "core/timing/core_timing.hpp"

namespace nemu::core::timing {

std::shared_ptr<EventType> CoreTiming::RegisterEvent(TimedCallback callback, std::string name) {
    return std::make_shared<EventType>(std::move(callback), std::move(name));
}

void CoreTiming::ScheduleEvent(Nanoseconds into_future, const std::shared_ptr<EventType>& event,
                               bool absolute_time) {
    if (!event) {
        return;
    }
    std::lock_guard lock(mutex_);
    const u64 ticks = TicksFromNs(into_future);
    EventEntry entry;
    entry.deadline = absolute_time ? ticks : (global_timer_ + ticks);
    entry.reported_time = global_timer_;
    entry.type = event;
    entry.is_looping = false;
    entry.resched_ticks = 0;
    entry.seq = ++event_seq_;
    event->seq = entry.seq;
    queue_.push(std::move(entry));
}

void CoreTiming::ScheduleLoopingEvent(Nanoseconds start_time, Nanoseconds resched_time,
                                      const std::shared_ptr<EventType>& event,
                                      bool absolute_time) {
    if (!event) {
        return;
    }
    std::lock_guard lock(mutex_);
    const u64 start_ticks = TicksFromNs(start_time);
    const u64 resched_ticks = TicksFromNs(resched_time);
    EventEntry entry;
    entry.deadline = absolute_time ? start_ticks : (global_timer_ + start_ticks);
    entry.reported_time = global_timer_;
    entry.type = event;
    entry.is_looping = true;
    entry.resched_ticks = resched_ticks > 0 ? resched_ticks : 1;
    entry.seq = ++event_seq_;
    event->seq = entry.seq;
    queue_.push(std::move(entry));
}

void CoreTiming::UnscheduleEvent(const std::shared_ptr<EventType>& event) {
    if (!event) {
        return;
    }
    // Drop every queued entry whose type matches. Because entries are stored in
    // a heap we cannot erase in place cheaply; rebuild the heap with survivors.
    std::lock_guard lock(mutex_);
    std::priority_queue<EventEntry, std::vector<EventEntry>, DeadlineOrder> survivors;
    while (!queue_.empty()) {
        EventEntry e = std::move(const_cast<EventEntry&>(queue_.top()));
        queue_.pop();
        if (e.type != event) {
            survivors.push(std::move(e));
        }
    }
    queue_.swap(survivors);
}

void CoreTiming::AddTicks(u64 ticks) {
    // Advance the global clock and fire every event whose deadline is now due.
    // Collect due callbacks under the lock, then invoke them after release so a
    // callback that itself schedules more events does not deadlock.
    std::vector<std::tuple<std::shared_ptr<EventType>, Tick, s64>> due;
    {
        std::lock_guard lock(mutex_);
        global_timer_ += ticks;
        const Tick now = global_timer_;
        while (!queue_.empty()) {
            EventEntry e = const_cast<EventEntry&>(queue_.top());
            if (e.deadline > now) {
                break; // earliest pending event is still in the future
            }
            queue_.pop();
            const s64 ns_late = static_cast<s64>(now - e.deadline);
            // Looping events re-arm themselves before the callback so their next
            // iteration lands relative to the nominal period (drift compensated).
            if (e.is_looping) {
                EventEntry rearmed;
                rearmed.deadline = e.deadline + e.resched_ticks;
                rearmed.reported_time = now;
                rearmed.type = e.type;
                rearmed.is_looping = true;
                rearmed.resched_ticks = e.resched_ticks;
                rearmed.seq = ++event_seq_;
                queue_.push(std::move(rearmed));
            }
            due.emplace_back(std::move(e.type), now, ns_late);
        }
    }
    for (auto& [type, now, late] : due) {
        if (type) {
            type->callback(now, late);
        }
    }
}

void CoreTiming::FireDueEvents() {
    std::vector<std::tuple<std::shared_ptr<EventType>, Tick, s64>> due;
    {
        std::lock_guard lock(mutex_);
        const Tick now = global_timer_;
        while (!queue_.empty()) {
            EventEntry e = const_cast<EventEntry&>(queue_.top());
            if (e.deadline > now) {
                break;
            }
            queue_.pop();
            const s64 ns_late = static_cast<s64>(now - e.deadline);
            if (e.is_looping) {
                EventEntry rearmed;
                rearmed.deadline = e.deadline + e.resched_ticks;
                rearmed.reported_time = now;
                rearmed.type = e.type;
                rearmed.is_looping = true;
                rearmed.resched_ticks = e.resched_ticks;
                rearmed.seq = ++event_seq_;
                queue_.push(std::move(rearmed));
            }
            due.emplace_back(std::move(e.type), now, ns_late);
        }
    }
    for (auto& [type, now, late] : due) {
        if (type) {
            type->callback(now, late);
        }
    }
}

void CoreTiming::ClearPendingEvents() {
    std::lock_guard lock(mutex_);
    while (!queue_.empty()) {
        queue_.pop();
    }
}

} // namespace nemu::core::timing