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

    // Clean up
    MemoryBudget::AccrueCommitted(-static_cast<s64>(6ULL << 30));
    MemoryBudget::AccrueSubsystem(-static_cast<s64>(1ULL << 30));
    MemoryBudget::ResetPeak();
    std::cout << "  MemoryBudget Baseline PASS\n";
}

void TestMathematicalPartitionTheorem() {
    std::cout << "  Testing Mathematical Partition Theorem & Headroom Guarantees...\n";

    // 1. Validate Partition Plan constants sum strictly within 5,120 MiB cap
    const auto plan = MemoryBudget::GetPartitionPlan();
    NEMU_TEST_ASSERT(plan.total_cap_bytes == 5120ULL * 1024 * 1024, "5120 MiB cap");
    NEMU_TEST_ASSERT(plan.safety_margin_bytes == 400ULL * 1024 * 1024, "400 MiB safety margin");
    NEMU_TEST_ASSERT(plan.usable_ceiling_bytes == 4720ULL * 1024 * 1024, "4720 MiB usable ceiling");
    NEMU_TEST_ASSERT(plan.guest_emulation_cap_bytes == 3072ULL * 1024 * 1024, "3.0 GiB guest emulation cap");
    NEMU_TEST_ASSERT(plan.translation_headroom_bytes == 2048ULL * 1024 * 1024, "2.0 GiB translation headroom target");

    // Theoretical maximum sum:
    const u64 max_theoretical_sum = plan.guest_emulation_cap_bytes +
                                   plan.jit_code_cache_bytes +
                                   plan.jit_metadata_bytes +
                                   plan.shader_pso_cache_bytes +
                                   plan.audio_sysmodules_bytes +
                                   plan.host_fixed_overhead_bytes +
                                   plan.dynamic_texture_budget_at_cap_bytes;
    NEMU_TEST_ASSERT(max_theoretical_sum + plan.safety_margin_bytes <= plan.total_cap_bytes,
                     "Max theoretical sum + 400MB safety margin <= 5120MB ceiling");

    // 2. Test Fine-Grained Subsystem Accrual & Separation
    MemoryBudget::ResetPeak();
    MemoryBudget::AccrueJit(64ULL * 1024 * 1024);     // 64 MiB JIT code cache
    MemoryBudget::AccrueShader(16ULL * 1024 * 1024);  // 16 MiB PSOs
    MemoryBudget::AccrueBuffer(32ULL * 1024 * 1024);  // 32 MiB geometry buffers
    MemoryBudget::AccrueAudio(8ULL * 1024 * 1024);    // 8 MiB audio
    MemoryBudget::AccrueTexture(256ULL * 1024 * 1024);// 256 MiB textures

    NEMU_TEST_ASSERT(MemoryBudget::JitBytes() == 64ULL * 1024 * 1024, "JIT bytes 64MB");
    NEMU_TEST_ASSERT(MemoryBudget::ShaderBytes() == 16ULL * 1024 * 1024, "Shader bytes 16MB");
    NEMU_TEST_ASSERT(MemoryBudget::BufferBytes() == 32ULL * 1024 * 1024, "Buffer bytes 32MB");
    NEMU_TEST_ASSERT(MemoryBudget::AudioBytes() == 8ULL * 1024 * 1024, "Audio bytes 8MB");
    NEMU_TEST_ASSERT(MemoryBudget::TextureBytes() == 256ULL * 1024 * 1024, "Texture bytes 256MB");

    // 3. Mathematical Headroom Evaluation with 3.0 GiB guest commit
    constexpr u64 k3GiB = 3072ULL * 1024 * 1024;
    MemoryBudget::AccrueFastmem(k3GiB);
    NEMU_TEST_ASSERT(MemoryBudget::CommittedFastmem() == k3GiB, "Guest commit 3.0 GiB");
    NEMU_TEST_ASSERT(MemoryBudget::TranslationHeadroom() == (2048ULL * 1024 * 1024),
                     "Guaranteed 2.0+ GiB translation headroom preserved");

    // Dynamic texture budget must clamp safely:
    const u64 tex_budget = MemoryBudget::ComputeDynamicTextureBudget();
    NEMU_TEST_ASSERT(tex_budget >= MemoryBudget::kDynamicTextureBudgetFloor, "Texture budget above floor");
    NEMU_TEST_ASSERT(tex_budget <= MemoryBudget::kDynamicTextureBudgetCap, "Texture budget below cap");

    // Verify mathematical invariant holds
    NEMU_TEST_ASSERT(MemoryBudget::VerifyMathematicalInvariant(), "Invariant: Total + 400MB safety <= 5120MB");

    // 4. Test Extreme Edge Case: Maximum Retail App Limit (3,250 MiB)
    constexpr u64 k3250MiB = 3250ULL * 1024 * 1024;
    MemoryBudget::AccrueFastmem(static_cast<s64>(k3250MiB - k3GiB));
    NEMU_TEST_ASSERT(MemoryBudget::CommittedFastmem() == k3250MiB, "Guest commit 3250 MiB");
    NEMU_TEST_ASSERT(MemoryBudget::TranslationHeadroom() >= 1870ULL * 1024 * 1024,
                     "Headroom is > 1.8 GiB even at 3250 MiB retail app maximum");
    const u64 extreme_tex_budget = MemoryBudget::ComputeDynamicTextureBudget();
    NEMU_TEST_ASSERT(extreme_tex_budget >= MemoryBudget::kDynamicTextureBudgetFloor, "Texture budget safe at 3250MB");

    // Clean up
    MemoryBudget::AccrueFastmem(-static_cast<s64>(k3250MiB));
    MemoryBudget::AccrueJit(-static_cast<s64>(64ULL * 1024 * 1024));
    MemoryBudget::AccrueShader(-static_cast<s64>(16ULL * 1024 * 1024));
    MemoryBudget::AccrueBuffer(-static_cast<s64>(32ULL * 1024 * 1024));
    MemoryBudget::AccrueAudio(-static_cast<s64>(8ULL * 1024 * 1024));
    MemoryBudget::AccrueTexture(-static_cast<s64>(256ULL * 1024 * 1024));
    MemoryBudget::ResetPeak();

    std::cout << "  Mathematical Partition Theorem & Headroom Guarantees PASS\n";
}

int main() {
    std::cout << "== NEMU MemoryBudget test ==\n";
    TestBudgetAccounting();
    TestMathematicalPartitionTheorem();
    std::cout << "ALL MEMORYBUDGET TESTS PASSED\n";
    return 0;
}