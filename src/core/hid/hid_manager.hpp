#pragma once

#include "hid_types.hpp"
#include "deadzone.hpp"
#include "controller_mapping.hpp"
#include <array>
#include <mutex>


namespace nemu::core::hid {

class HidManager {
public:
    static constexpr size_t MAX_PLAYERS = 8;

    HidManager();
    ~HidManager() = default;

    /// Update controller input for a player
    void UpdateController(size_t player_index, const XboxGamepadState& state);

    /// Get current Npad state for a player
    [[nodiscard]] NpadCommonState GetCurrentState(size_t player_index) const;

    /// Get ring buffer for a player
    [[nodiscard]] NpadRingBuffer GetRingBuffer(size_t player_index) const;

    /// Set face button layout preference
    void SetButtonLayout(FaceButtonLayout layout) noexcept { layout_ = layout; }
    [[nodiscard]] FaceButtonLayout GetButtonLayout() const noexcept { return layout_; }

    /// Set deadzones
    void SetDeadzones(float inner, float outer) noexcept {
        inner_deadzone_ = inner;
        outer_deadzone_ = outer;
    }

private:
    mutable std::mutex hid_mutex_;
    FaceButtonLayout layout_{FaceButtonLayout::NintendoStandard};
    float inner_deadzone_{DeadzoneFilter::DEFAULT_INNER_DEADZONE};
    float outer_deadzone_{DeadzoneFilter::DEFAULT_OUTER_DEADZONE};

    std::array<NpadRingBuffer, MAX_PLAYERS> controllers_{};
    std::array<s64, MAX_PLAYERS> sampling_numbers_{};
};

} // namespace nemu::core::hid
