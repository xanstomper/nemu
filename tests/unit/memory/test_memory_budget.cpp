// Xbox Dev Mode 5 GiB RAM budget tracker test (Tier-C3).
#include "core/memory/memory_budget.hpp"
#include "core/types.hpp"
#include <iostream>
#include <cstdlib>

#define NEMU_TEST_ASSERT(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "Assertion failed: " #cond " (" << (msg) << ") at " \
                      << __FILE__ << ":" << __LINE__ << std::endl; \
            std::exit(1); \
        } \
    } while (0)

using namespace nemu;
using namespace nemu::core::memory;

void TestBudgetAccounting() {
    MemoryBudget::ResetPeak();

    // Start empty.
    NEMU_TEST_ASSERT(MemoryBudget::TotalEstimated() == 0, "starts empty");
    NEMU_TEST_ASSERT(MemoryBudget::WithinCap(), "within cap at start");

    // Simulate committing ~2.5 GiB of guest RAM (typical real game footprint).
    constexpr u64 k2point5GiB = (5ULL * 1024 * 1024 * 1024) / 2;
    MemoryBudget::AccrueCommitted(static_cast<s64>(k2point5GiB));
    NEMU_TEST_ASSERT(MemoryBudget::CommittedFastmem() == k2point5GiB, "fastmem 2.5GiB");
    NEMU_TEST_ASSERT(MemoryBudget::TotalEstimated() == k2point5GiB, "total = 2.5GiB");
    NEMU_TEST_ASSERT(MemoryBudget::WithinCap(), "2.5GiB within 5GiB cap");

    // Add emulator subsystems (JIT + texture + buffer caches) ~1 GiB.
    MemoryBudget::AccrueSubsystem(static_cast<s64>(1ULL << 30)); // 1 GiB
    NEMU_TEST_ASSERT(MemoryBudget::SubsystemBytes() == (1ULL << 30), "subsystem 1GiB");
    const u64 total_q = k2point5GiB + (1ULL << 30);
    NEMU_TEST_ASSERT(MemoryBudget::TotalEstimated() == total_q, "total ~3.5GiB");
    NEMU_TEST_ASSERT(MemoryBudget::WithinCap(), "3.5GiB within 5GiB cap");
    NEMU_TEST_ASSERT(MemoryBudget::Headroom() == MemoryBudget::kXboxDevCapBytes - total_q,
                     "headroom = cap - total");

    // Peak tracking.
    NEMU_TEST_ASSERT(MemoryBudget::Peak() == total_q, "peak = total");

    // Decommit shrinks and resets below cap cleanly (no negative drift).
    MemoryBudget::AccrueCommitted(-static_cast<s64>(k2point5GiB));
    NEMU_TEST_ASSERT(MemoryBudget::CommittedFastmem() == 0, "fastmem freed");
    NEMU_TEST_ASSERT(MemoryBudget::TotalEstimated() == (1ULL << 30), "total = subsystem only");

    // Pushing past 5 GiB flags out-of-cap.
    MemoryBudget::ResetPeak();
    MemoryBudget::AccrueCommitted(static_cast<s64>(6ULL << 30)); // 6 GiB > cap
    NEMU_TEST_ASSERT(!MemoryBudget::WithinCap(), "over-cap flagged");
    NEMU_TEST_ASSERT(MemoryBudget::Headroom() == 0, "no headroom over cap");

    MemoryBudget::ResetPeak();
    std::cout << "  MemoryBudget PASS\n";
}

int main() {
    std::cout << "== NEMU MemoryBudget test ==\n";
    TestBudgetAccounting();
    std::cout << "ALL MEMORYBUDGET TESTS PASSED\n";
    return 0;
}