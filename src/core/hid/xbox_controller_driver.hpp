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

    /// Set vibration/rumble on an Xbox controller
    bool SetVibration(size_t player_index, float low_freq_motor, float high_freq_motor);

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

    std::array<bool, MAX_XBOX_CONTROLLERS> connected_{};
    std::array<XboxGamepadState, MAX_XBOX_CONTROLLERS> injected_state_{};
    std::array<bool, MAX_XBOX_CONTROLLERS> has_injected_{};
};

} // namespace nemu::core::hid
