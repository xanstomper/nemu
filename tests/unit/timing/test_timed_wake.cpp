#include "core/timing/core_timing.hpp"
#include "core/timing/timed_wake.hpp"
#include <chrono>
#include <iostream>
#include <cstdlib>

using namespace nemu;
using namespace nemu::core::timing;

#define NT_ASSERT(...) \
    do { \
        if (!(__VA_ARGS__)) { \
            std::cerr << "Assertion failed: " #__VA_ARGS__ << " at " \
                      << __FILE__ << ":" << __LINE__ << std::endl; \
            std::exit(1); \
        } \
    } while (0)

int main() {
    std::cout << "[Test: Core-Timing-Paced Timed Wakeup]" << std::endl;

    {
        CoreTiming timing;
        TimedWake wake(timing);
        wake.Initialize();

        // Sleep until tick 1000; verify not woken before, woken after the
        // scheduler passes the deadline.
        wake.ScheduleWake(1000);
        NT_ASSERT(!wake.HasWoken());
        timing.AddTicks(999);
        NT_ASSERT(!wake.HasWoken(), "not woken before deadline");
        timing.AddTicks(1); // now == 1000
        NT_ASSERT(wake.HasWoken(), "woken at deadline");
    }

    {
        CoreTiming timing;
        TimedWake wake(timing);
        wake.Initialize();

        // Scheduling a wake already in the past fires immediately.
        wake.ScheduleWake(500);
        timing.ResetTicks(900);
        wake.ScheduleWake(500); // already past current 900
        NT_ASSERT(wake.HasWoken(), "past deadline wakes immediately");
    }

    {
        CoreTiming timing;
        TimedWake wake(timing);
        wake.Initialize();

        // Re-scheduling a later wake after an earlier one:
        // the earlier schedule is superseded (no spurious wake).
        wake.ScheduleWake(1000);
        wake.ScheduleWake(2000);
        timing.AddTicks(1500); // only the 2000 deadline should still be pending
        NT_ASSERT(!wake.HasWoken(), "earlier schedule superseded");
        timing.AddTicks(600); // now == 2100
        NT_ASSERT(wake.HasWoken(), "later deadline fires");
    }

    std::cout << "  - Core-timing-paced timed wakeup tests: PASSED" << std::endl;
    std::cout << "[Test: Timed Wakeup PASSED]" << std::endl;
    return 0;
}