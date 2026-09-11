#include "deadzone.hpp"
#include <cmath>
#include <algorithm>

namespace nemu::core::hid {

AnalogStickState DeadzoneFilter::ApplyRadialDeadzone(
    s32 raw_x,
    s32 raw_y,
    float inner_deadzone,
    float outer_deadzone) noexcept {

    constexpr float MAX_COORD = 32767.0f;

    // Normalize raw inputs to [-1.0, +1.0]
    float nx = std::clamp(static_cast<float>(raw_x) / MAX_COORD, -1.0f, 1.0f);
    float ny = std::clamp(static_cast<float>(raw_y) / MAX_COORD, -1.0f, 1.0f);

    const float magnitude = std::sqrt(nx * nx + ny * ny);

    if (magnitude <= inner_deadzone) {
        return AnalogStickState{0, 0};
    }

    if (magnitude >= outer_deadzone) {
        const float scale = 1.0f / magnitude;
        nx *= scale;
        ny *= scale;
    } else {
        const float normalized_mag = (magnitude - inner_deadzone) / (outer_deadzone - inner_deadzone);
        const float scale = normalized_mag / magnitude;
        nx *= scale;
        ny *= scale;
    }

    return AnalogStickState{
        .x = static_cast<s32>(std::round(nx * MAX_COORD)),
        .y = static_cast<s32>(std::round(ny * MAX_COORD))
    };
}

} // namespace nemu::core::hid
