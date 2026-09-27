#pragma once

#include "core/types.hpp"
#include <atomic>

namespace nemu::core::memory {

// ---------------------------------------------------------------------------
// MemoryBudget — explicit Xbox Dev Mode RAM-cap governor (Tier-C3).
//
// Xbox Developer Mode caps the UWP process at ~5 GiB physical RAM. A Switch
// game sees ~2-2.5 GiB on real hardware, so the emulator's budget is:
//   guest committed RAM  ~2.5 GiB  (fastmem Commit() + virtual heap)
//   JIT cache            ~0.15 GiB (16 MiB reservable, commit on demand)
//   texture/buffer cache ~1.0 GiB  (LRU-evicted, ASTC->BC1 to keep it low)
//   shaders, misc        ~0.3 GiB
//   -------------------------------
//   total                ~3.95 GiB  < 5 GiB cap  (~1 GiB headroom)
//
// This tracker makes the physical footprint explicit and observable: it sums
// the committed fastmem pages (the dominant term) plus the subsystem budgets
// the backends register, and surfaces a live "used vs 5 GiB cap" figure for
// the Diagnostics screen. It does NOT page in anything — fastmem already
// reserve-vs-commits correctly; this is visibility + a soft guard.
// ---------------------------------------------------------------------------
class MemoryBudget {
public:
    static constexpr u64 kXboxDevCapBytes = 5ULL * 1024 * 1024 * 1024; // 5 GiB process cap
    static constexpr u64 kGuestTargetBytes = (5ULL * 1024 * 1024 * 1024) - (1ULL << 30); // 4 GiB guest reserve ideal

    /// Record fastmem pages committed for a guest range (positive delta on
    /// commit, negative on decommit). Call from FastmemManager::Commit/Decommit.
    static void AccrueCommitted(s64 bytes_delta) noexcept {
        UpdateTotal(committed_fastmem_.fetch_add(bytes_delta, std::memory_order_relaxed) + bytes_delta);
    }

    /// Register an external subsystem budget (JIT cache, texture cache bytes
    /// resident, buffer cache bytes). Cumulative deltas.
    static void AccrueSubsystem(s64 bytes_delta) noexcept {
        UpdateTotal(subsystem_bytes_.fetch_add(bytes_delta, std::memory_order_relaxed) + bytes_delta);
    }

private:
    /// Recompute the running total (fastmem + subsystem) and track peak.
    static void UpdateTotal(s64 _new_component) noexcept {
        (void)_new_component; // component value is already stored; recompute from both
        const s64 total = committed_fastmem_.load(std::memory_order_relaxed)
                        + subsystem_bytes_.load(std::memory_order_relaxed);
        if (total > peak_total_.load(std::memory_order_relaxed)) {
            peak_total_.store(total, std::memory_order_relaxed);
        }
    }

public:
    /// Bytes of guest physical RAM committed via fastmem (dominant term).
    [[nodiscard]] static u64 CommittedFastmem() noexcept {
        return static_cast<u64>(std::max<s64>(0, committed_fastmem_.load(std::memory_order_relaxed)));
    }

    /// Registered subsystem budgets (JIT + texture + buffer caches).
    [[nodiscard]] static u64 SubsystemBytes() noexcept {
        return static_cast<u64>(std::max<s64>(0, subsystem_bytes_.load(std::memory_order_relaxed)));
    }

    /// Total estimated physical footprint.
    [[nodiscard]] static u64 TotalEstimated() noexcept {
        return CommittedFastmem() + SubsystemBytes();
    }

    /// Headroom under the 5 GiB cap.
    [[nodiscard]] static u64 Headroom() noexcept {
        return TotalEstimated() < kXboxDevCapBytes ? (kXboxDevCapBytes - TotalEstimated()) : 0;
    }

    /// True when still inside the 5 GiB cap.
    [[nodiscard]] static bool WithinCap() noexcept { return TotalEstimated() < kXboxDevCapBytes; }

    /// Peak estimated footprint seen so far (for Diagnostics).
    [[nodiscard]] static u64 Peak() noexcept {
        return static_cast<u64>(std::max<s64>(0, peak_total_.load(std::memory_order_relaxed)));
    }

    /// Reset peak (called on title load).
    static void ResetPeak() noexcept {
        peak_total_.store(0, std::memory_order_relaxed);
    }

private:
    static inline std::atomic<s64> committed_fastmem_{0};
    static inline std::atomic<s64> subsystem_bytes_{0};
    static inline std::atomic<s64> peak_total_{0};
};

} // namespace nemu::core::memory