#pragma once

#include "core/types.hpp"
#include "controller_mapping.hpp"
#include <array>
#include <optional>
#include <string_view>

namespace nemu::core::hid {

class XboxControllerDriver {
public:
    static constexpr size_t MAX_XBOX_CONTROLLERS = 4;

    XboxControllerDriver();
    ~XboxControllerDriver();

    XboxControllerDriver(const XboxControllerDriver&) = delete;
    XboxControllerDriver& operator=(const XboxControllerDriver&) = delete;

    /// Poll state for a specific controller index (0..3)
    [[nodiscard]] std::optional<XboxGamepadState> Poll(size_t player_index);

    /// Check if a controller is currently connected
    [[nodiscard]] bool IsConnected(size_t player_index) const noexcept;

    /// Set standard 2-motor vibration/rumble on an Xbox controller
    bool SetVibration(size_t player_index, float low_freq_motor, float high_freq_motor);

    /// Set 4-motor vibration on an Xbox Wireless Controller (Left/Right body + Left/Right Impulse Triggers)
    bool SetVibration4(size_t player_index, float left_motor, float right_motor, float left_trigger, float right_trigger);

    /// Get a thread-safe snapshot of the controller state without holding COM/driver locks
    [[nodiscard]] std::optional<XboxGamepadState> GetSnapshot(size_t player_index) const noexcept;

    /// Query last vibration values set on a controller (left_motor, right_motor, left_trigger, right_trigger)
    [[nodiscard]] std::array<float, 4> GetLastVibration(size_t player_index) const noexcept;

    /// Software test injection (useful for unit tests and headless environments)
    void InjectState(size_t player_index, const XboxGamepadState& state);
    void SetConnected(size_t player_index, bool connected);
    void ClearInjectedState(size_t player_index);

    [[nodiscard]] bool IsXInputAvailable() const noexcept { return xinput_available_; }

private:
    bool InitializeXInput();

    bool xinput_available_{false};
    void* xinput_module_{nullptr};
    void* fn_get_state_{nullptr};
    void* fn_set_state_{nullptr};
    void* fn_set_state_ex_{nullptr};

    std::array<bool, MAX_XBOX_CONTROLLERS> connected_{};
    std::array<XboxGamepadState, MAX_XBOX_CONTROLLERS> injected_state_{};
    std::array<bool, MAX_XBOX_CONTROLLERS> has_injected_{};
    std::array<std::array<float, 4>, MAX_XBOX_CONTROLLERS> last_vibration_{};
};

} // namespace nemu::core::hid
