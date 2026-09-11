#pragma once

#include "hid_types.hpp"

namespace nemu::core::hid {

class DeadzoneFilter {
public:
    static constexpr float DEFAULT_INNER_DEADZONE = 0.15f;
    static constexpr float DEFAULT_OUTER_DEADZONE = 0.95f;

    /// Filter raw analog stick coordinates using radial deadzone with smooth linear rescale
    [[nodiscard]] static AnalogStickState ApplyRadialDeadzone(
        s32 raw_x,
        s32 raw_y,
        float inner_deadzone = DEFAULT_INNER_DEADZONE,
        float outer_deadzone = DEFAULT_OUTER_DEADZONE) noexcept;
};

} // namespace nemu::core::hid
