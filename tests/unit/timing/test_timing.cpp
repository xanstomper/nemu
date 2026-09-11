#include "core/timing/core_timing.hpp"
#include <iostream>
#include <cstdlib>

#define NEMU_TEST_ASSERT(...) \
    do { \
        if (!(__VA_ARGS__)) { \
            std::cerr << "Assertion failed: " #__VA_ARGS__ << " at " \
                      << __FILE__ << ":" << __LINE__ << std::endl; \
            std::exit(1); \
        } \
    } while (0)

using namespace nemu;
using namespace nemu::core::timing;

static int fired_count = 0;
static int loop_count = 0;
static s64 last_late = 0;
static u64 last_now = 0;

static void OnOneShot(u64 now, s64 late) {
    fired_count++;
    last_now = now;
    last_late = late;
}

static void OnLoop(u64 now, s64 late) {
    loop_count++;
    last_now = now;
    last_late = late;
}

int main() {
    std::cout << "[Test: Core Timing Scheduler]" << std::endl;

    CoreTiming timing;
    NEMU_TEST_ASSERT(timing.GetPendingEventCount() == 0);
    NEMU_TEST_ASSERT(timing.GetTicks() == 0);

    // 1. Register event types.
    auto one_shot = timing.RegisterEvent(OnOneShot, "one-shot");
    auto loop = timing.RegisterEvent(OnLoop, "loop");
    NEMU_TEST_ASSERT(one_shot != nullptr && loop != nullptr);

    // 2. Schedule a one-shot event 100 ticks in the future.
    timing.ScheduleEvent(std::chrono::nanoseconds(100), one_shot);
    timing.AddTicks(99);
    NEMU_TEST_ASSERT(fired_count == 0);
    timing.AddTicks(1); // now at tick 100
    NEMU_TEST_ASSERT(fired_count == 1);
    NEMU_TEST_ASSERT(last_now == 100);
    NEMU_TEST_ASSERT(last_late == 0);

    // 3. Late firing is reported as positive ns_late.
    timing.ScheduleEvent(std::chrono::nanoseconds(20), one_shot); // due at 120
    timing.AddTicks(50); // now at 150, 30 ticks late
    NEMU_TEST_ASSERT(fired_count == 2);
    NEMU_TEST_ASSERT(last_now == 150);
    NEMU_TEST_ASSERT(last_late == 30);

    // 4. Looping event reschedules itself (drift-compensated by resched period).
    timing.ScheduleLoopingEvent(std::chrono::nanoseconds(10), std::chrono::nanoseconds(10), loop);
    timing.AddTicks(35); // fires at 10, 20, 30 => 3 fires
    NEMU_TEST_ASSERT(loop_count == 3);
    // It should still be pending (auto-rescheduled).
    NEMU_TEST_ASSERT(timing.GetPendingEventCount() >= 1);

    // 5. Unschedule stops a looping event.
    const int before = loop_count;
    timing.UnscheduleEvent(loop);
    timing.AddTicks(100);
    NEMU_TEST_ASSERT(loop_count == before);

    // 6. Absolute scheduling works against a reset clock.
    timing.ResetTicks(1000);
    timing.ScheduleEvent(std::chrono::nanoseconds(2000), one_shot, true); // absolute at 2000
    timing.AddTicks(500); // now at 1500
    NEMU_TEST_ASSERT(fired_count == 2);
    timing.AddTicks(600); // now at 2100
    NEMU_TEST_ASSERT(fired_count == 3);

    // 7. ClearPendingEvents empties the queue.
    timing.ClearPendingEvents();
    NEMU_TEST_ASSERT(timing.GetPendingEventCount() == 0);

    std::cout << "  - Core timing scheduler tests: PASSED" << std::endl;
    std::cout << "[Test: Core Timing Scheduler PASSED]" << std::endl;
    return 0;
}