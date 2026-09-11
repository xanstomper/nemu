#pragma once

#include "hid_types.hpp"
#include <array>
#include <mutex>

namespace nemu::core::hid {

class VibrationManager {
public:
    static constexpr size_t MAX_CONTROLLERS = 8;

    VibrationManager() = default;
    ~VibrationManager() = default;

    /// Translate Nintendo Switch HD rumble values to Xbox controller vibration state
    [[nodiscard]] static XboxVibrationState TranslateToXbox(
        const NpadVibrationValue& left_val,
        const NpadVibrationValue& right_val,
        bool enable_impulse_triggers = true
    ) noexcept;

    /// Set vibration values for a specific controller/player
    void SetVibration(size_t player_index, const NpadVibrationValue& left, const NpadVibrationValue& right);

    /// Get current Xbox vibration state for a player
    [[nodiscard]] XboxVibrationState GetVibration(size_t player_index) const;

    /// Silence all vibration motors across all controllers
    void StopAll();

private:
    mutable std::mutex vib_mutex_;
    std::array<XboxVibrationState, MAX_CONTROLLERS> controller_vibrations_{};
};

} // namespace nemu::core::hid
