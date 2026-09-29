#pragma once

#include "core/types.hpp"
#include <functional>
#include <string>
#include <string_view>
#include <atomic>
#include <mutex>
#include <vector>

namespace nemu::platform {

/// Memory pressure state tiers matching Xbox Series S Dev Mode constraints (5120 MB ceiling).
enum class MemoryPressureLevel : u32 {
    Nominal = 0,    ///< Usage < 3840 MB: unconstrained execution, background shader work enabled.
    Elevated = 1,   ///< Usage 3840 MB - 4096 MB: hysteresis state, throttle aggressive precaching.
    Critical = 2    ///< Usage >= 4096 MB: memory guard active, trigger DXGI trim & texture purge.
};

class XboxMemoryGovernor {
public:
    // Xbox Series S Dev Mode memory thresholds (bytes)
    static constexpr u64 kHardCeiling = 5120ULL * 1024 * 1024;        // 5120 MB (5 GB)
    static constexpr u64 kCriticalThreshold = 4096ULL * 1024 * 1024;  // 4096 MB (4 GB)
    static constexpr u64 kHysteresisThreshold = 3968ULL * 1024 * 1024;// 3968 MB (Hysteresis exit)
    static constexpr u64 kNominalThreshold = 3840ULL * 1024 * 1024;   // 3840 MB (3.75 GB)

    XboxMemoryGovernor();
    ~XboxMemoryGovernor() = default;

    /// Query current process committed memory (bytes).
    [[nodiscard]] u64 GetCommittedBytes() const noexcept;

    /// Query current memory pressure level based on the 3-tier threshold model.
    [[nodiscard]] MemoryPressureLevel GetPressureLevel() const noexcept;

    /// Formats a high-density status string for the Switch Horizon UI HUD (e.g. "[RAM: 2.1GB / 5.1GB • NOMINAL]").
    [[nodiscard]] std::string FormatHudString() const;

    /// Register a callback invoked when entering Critical memory state to purge transient textures.
    void RegisterTextureTrimCallback(std::function<void()> cb);

    /// Register a callback invoked when entering Critical memory state to invoke IDXGIDevice3::Trim().
    void RegisterDxgiTrimCallback(std::function<void()> cb);

    /// Register a callback invoked to toggle background shader compilation deferral.
    void RegisterShaderDeferralCallback(std::function<void(bool defer)> cb);

    /// Polls current memory usage and executes trimming callbacks if entering Critical state.
    /// Returns true if a trim was triggered.
    bool EvaluateAndEnforce();

    /// For testing and headless validation: simulate specific committed byte counts.
    void SimulateCommitBytes(u64 bytes) noexcept;
    void ResetSimulatedCommit() noexcept;

private:
    std::atomic<MemoryPressureLevel> current_level_{MemoryPressureLevel::Nominal};
    std::atomic<bool> simulated_active_{false};
    std::atomic<u64> simulated_bytes_{0};

    std::mutex callback_mutex_;
    std::vector<std::function<void()>> texture_trim_cbs_;
    std::vector<std::function<void()>> dxgi_trim_cbs_;
    std::vector<std::function<void(bool)>> shader_defer_cbs_;
};

} // namespace nemu::platform
