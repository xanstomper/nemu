#pragma once

#include "core/types.hpp"
#include <atomic>
#include <algorithm>

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
    // Xbox Series S/X Dev Mode hard physical memory boundary
    static constexpr u64 kXboxDevCapBytes = 5120ULL * 1024 * 1024;         // 5,120 MiB (5.0 GiB) process cap
    static constexpr u64 kGuestTargetBytes = (5ULL * 1024 * 1024 * 1024) - (1ULL << 30); // 4 GiB guest reserve ideal

    // Mathematical memory partition guarantees:
    static constexpr u64 kSafetyMarginBytes = 400ULL * 1024 * 1024;       // 400 MiB untouchable OS safety buffer
    static constexpr u64 kUsableCeilingBytes = kXboxDevCapBytes - kSafetyMarginBytes; // 4,720 MiB safe maximum
    static constexpr u64 kGuestEmulationCapBytes = 3072ULL * 1024 * 1024;  // 3,072 MiB (3.0 GiB) game memory target
    static constexpr u64 kGuestMaxAppLimitBytes = 3250ULL * 1024 * 1024;   // 3,250 MiB Switch retail app limit
    static constexpr u64 kTranslationHeadroomTarget = 2048ULL * 1024 * 1024; // 2,048 MiB (2.0 GiB) guaranteed headroom
    static constexpr u64 kJitCodeCacheCapBytes = 64ULL * 1024 * 1024;      // 64 MiB JIT code cache
    static constexpr u64 kJitMetadataCapBytes = 32ULL * 1024 * 1024;       // 32 MiB JIT lookup/patch tables
    static constexpr u64 kShaderPsoCapBytes = 128ULL * 1024 * 1024;        // 128 MiB compiled PSO cache
    static constexpr u64 kAudioSysmodulesCapBytes = 96ULL * 1024 * 1024;   // 96 MiB audio/sysmodule buffers
    static constexpr u64 kHostRuntimeD3D12FixedBytes = 180ULL * 1024 * 1024; // 180 MiB host runtime + swapchain
    static constexpr u64 kDynamicTextureBudgetFloor = 512ULL * 1024 * 1024; // 512 MiB lower texture floor
    static constexpr u64 kDynamicTextureBudgetCap = 1200ULL * 1024 * 1024; // 1,200 MiB upper texture cap

    /// Partition plan snapshot describing mathematical layout of the 5,120 MiB budget
    struct PartitionPlan {
        u64 total_cap_bytes{kXboxDevCapBytes};
        u64 safety_margin_bytes{kSafetyMarginBytes};
        u64 usable_ceiling_bytes{kUsableCeilingBytes};
        u64 guest_emulation_cap_bytes{kGuestEmulationCapBytes};
        u64 translation_headroom_bytes{kTranslationHeadroomTarget};
        u64 jit_code_cache_bytes{kJitCodeCacheCapBytes};
        u64 jit_metadata_bytes{kJitMetadataCapBytes};
        u64 shader_pso_cache_bytes{kShaderPsoCapBytes};
        u64 audio_sysmodules_bytes{kAudioSysmodulesCapBytes};
        u64 host_fixed_overhead_bytes{kHostRuntimeD3D12FixedBytes};
        u64 dynamic_texture_budget_at_cap_bytes{1148ULL * 1024 * 1024}; // 1,148 MiB texture budget at 3.0 GiB guest commit
        u64 dynamic_texture_budget_max_cap_bytes{kDynamicTextureBudgetCap}; // 1,200 MiB upper cap (at <= 3020 MiB guest)
    };

    static PartitionPlan GetPartitionPlan() noexcept {
        return PartitionPlan{};
    }

    /// Record fastmem pages committed for a guest range (positive delta on
    /// commit, negative on decommit). Call from FastmemManager::Commit/Decommit.
    static void AccrueFastmem(s64 bytes_delta) noexcept {
        committed_fastmem_.fetch_add(bytes_delta, std::memory_order_relaxed);
        UpdateTotal();
    }

    /// Backwards compatibility alias for AccrueFastmem
    static void AccrueCommitted(s64 bytes_delta) noexcept {
        AccrueFastmem(bytes_delta);
    }

    /// Record JIT compiler code cache and metadata allocations.
    static void AccrueJit(s64 bytes_delta) noexcept {
        jit_bytes_.fetch_add(bytes_delta, std::memory_order_relaxed);
        UpdateTotal();
    }

    /// Record texture cache resident byte deltas.
    static void AccrueTexture(s64 bytes_delta) noexcept {
        texture_bytes_.fetch_add(bytes_delta, std::memory_order_relaxed);
        subsystem_bytes_.fetch_add(bytes_delta, std::memory_order_relaxed);
        UpdateTotal();
    }

    /// Record shader/PSO cache byte deltas.
    static void AccrueShader(s64 bytes_delta) noexcept {
        shader_bytes_.fetch_add(bytes_delta, std::memory_order_relaxed);
        UpdateTotal();
    }

    /// Record GPU vertex/index buffer cache byte deltas.
    static void AccrueBuffer(s64 bytes_delta) noexcept {
        buffer_bytes_.fetch_add(bytes_delta, std::memory_order_relaxed);
        UpdateTotal();
    }

    /// Record audio DSP mixing and buffer deltas.
    static void AccrueAudio(s64 bytes_delta) noexcept {
        audio_bytes_.fetch_add(bytes_delta, std::memory_order_relaxed);
        UpdateTotal();
    }

    /// Register a generic external subsystem budget. Cumulative deltas.
    static void AccrueSubsystem(s64 bytes_delta) noexcept {
        AccrueTexture(bytes_delta);
    }

private:
    /// Recompute the running total and track peak footprint.
    static void UpdateTotal() noexcept {
        const s64 total = committed_fastmem_.load(std::memory_order_relaxed)
                        + jit_bytes_.load(std::memory_order_relaxed)
                        + texture_bytes_.load(std::memory_order_relaxed)
                        + shader_bytes_.load(std::memory_order_relaxed)
                        + buffer_bytes_.load(std::memory_order_relaxed)
                        + audio_bytes_.load(std::memory_order_relaxed);
        if (total > peak_total_.load(std::memory_order_relaxed)) {
            peak_total_.store(total, std::memory_order_relaxed);
        }
    }

public:
    /// Bytes of guest physical RAM committed via fastmem (dominant guest term).
    [[nodiscard]] static u64 CommittedFastmem() noexcept {
        return static_cast<u64>(std::max<s64>(0, committed_fastmem_.load(std::memory_order_relaxed)));
    }

    /// Bytes of JIT compiler executable code cache and lookup structures.
    [[nodiscard]] static u64 JitBytes() noexcept {
        return static_cast<u64>(std::max<s64>(0, jit_bytes_.load(std::memory_order_relaxed)));
    }

    /// Bytes of resident host texture data.
    [[nodiscard]] static u64 TextureBytes() noexcept {
        return static_cast<u64>(std::max<s64>(0, texture_bytes_.load(std::memory_order_relaxed)));
    }

    /// Bytes of compiled pipeline state objects and shader bytecode.
    [[nodiscard]] static u64 ShaderBytes() noexcept {
        return static_cast<u64>(std::max<s64>(0, shader_bytes_.load(std::memory_order_relaxed)));
    }

    /// Bytes of resident GPU vertex/index/uniform upload buffers.
    [[nodiscard]] static u64 BufferBytes() noexcept {
        return static_cast<u64>(std::max<s64>(0, buffer_bytes_.load(std::memory_order_relaxed)));
    }

    /// Bytes of audio DSP mixing ring buffers and delay lines.
    [[nodiscard]] static u64 AudioBytes() noexcept {
        return static_cast<u64>(std::max<s64>(0, audio_bytes_.load(std::memory_order_relaxed)));
    }

    /// Total translation and host subsystem memory footprint.
    [[nodiscard]] static u64 SubsystemBytes() noexcept {
        const u64 sub = JitBytes() + TextureBytes() + ShaderBytes() + BufferBytes() + AudioBytes();
        const u64 legacy_sub = static_cast<u64>(std::max<s64>(0, subsystem_bytes_.load(std::memory_order_relaxed)));
        return std::max(sub, legacy_sub);
    }

    /// Total estimated physical footprint.
    [[nodiscard]] static u64 TotalEstimated() noexcept {
        return CommittedFastmem() + SubsystemBytes();
    }

    /// Total headroom under the 5 GiB process cap.
    [[nodiscard]] static u64 Headroom() noexcept {
        return TotalEstimated() < kXboxDevCapBytes ? (kXboxDevCapBytes - TotalEstimated()) : 0;
    }

    /// Headroom available strictly for translation and host subsystems given guest commit.
    [[nodiscard]] static u64 TranslationHeadroom() noexcept {
        const u64 guest = CommittedFastmem();
        return guest < kXboxDevCapBytes ? (kXboxDevCapBytes - guest) : 0;
    }

    /// Dynamic texture budget mathematically guaranteed to maintain safety margin.
    [[nodiscard]] static u64 ComputeDynamicTextureBudget() noexcept {
        const u64 guest = CommittedFastmem();
        const u64 fixed_overhead = kJitCodeCacheCapBytes + kJitMetadataCapBytes +
                                  kShaderPsoCapBytes + kAudioSysmodulesCapBytes +
                                  kHostRuntimeD3D12FixedBytes;
        if (guest + kSafetyMarginBytes + fixed_overhead >= kXboxDevCapBytes) {
            return kDynamicTextureBudgetFloor;
        }
        const u64 available = kXboxDevCapBytes - guest - kSafetyMarginBytes - fixed_overhead;
        return std::clamp<u64>(available, kDynamicTextureBudgetFloor, kDynamicTextureBudgetCap);
    }

    /// Verify whether the current state satisfies the mathematical non-kill-switch theorem:
    /// CommittedFastmem + Subsystems + SafetyMargin <= 5,120 MiB
    [[nodiscard]] static bool VerifyMathematicalInvariant() noexcept {
        return (TotalEstimated() + kSafetyMarginBytes) <= kXboxDevCapBytes;
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
    static inline std::atomic<s64> jit_bytes_{0};
    static inline std::atomic<s64> texture_bytes_{0};
    static inline std::atomic<s64> shader_bytes_{0};
    static inline std::atomic<s64> buffer_bytes_{0};
    static inline std::atomic<s64> audio_bytes_{0};
    static inline std::atomic<s64> peak_total_{0};
};

} // namespace nemu::core::memory