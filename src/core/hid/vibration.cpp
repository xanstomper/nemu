#include "vibration.hpp"
#include <algorithm>
#include <cmath>

namespace nemu::core::hid {

XboxVibrationState VibrationManager::TranslateToXbox(
    const NpadVibrationValue& left_val,
    const NpadVibrationValue& right_val,
    bool enable_impulse_triggers
) noexcept {
    XboxVibrationState out{};

    // Switch HD Rumble frequency mapping to ERM/LRA motors:
    // Low frequency typically 160 Hz (peak human bass resonance)
    // High frequency typically 320 Hz (fine click/texture resonance)
    const float left_amp = std::clamp(std::max(left_val.amp_low, right_val.amp_low), 0.0f, 1.0f);
    const float right_amp = std::clamp(std::max(left_val.amp_high, right_val.amp_high), 0.0f, 1.0f);

    // ERM motor inertia scaling:
    out.left_motor = left_amp;
    out.right_motor = right_amp;

    if (enable_impulse_triggers) {
        // Xbox impulse triggers (left/right trigger vibration)
        out.left_trigger = std::clamp(left_val.amp_low * 0.7f, 0.0f, 1.0f);
        out.right_trigger = std::clamp(right_val.amp_high * 0.7f, 0.0f, 1.0f);
    }

    return out;
}

void VibrationManager::SetVibration(size_t player_index, const NpadVibrationValue& left, const NpadVibrationValue& right) {
    if (player_index >= MAX_CONTROLLERS) return;
    std::lock_guard lock(vib_mutex_);
    controller_vibrations_[player_index] = TranslateToXbox(left, right);
}

[[nodiscard]] XboxVibrationState VibrationManager::GetVibration(size_t player_index) const {
    if (player_index >= MAX_CONTROLLERS) return {};
    std::lock_guard lock(vib_mutex_);
    return controller_vibrations_[player_index];
}

void VibrationManager::StopAll() {
    std::lock_guard lock(vib_mutex_);
    for (auto& vib : controller_vibrations_) {
        vib = {};
    }
}

} // namespace nemu::core::hid
