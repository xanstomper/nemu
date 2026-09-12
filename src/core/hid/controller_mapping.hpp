#pragma once

#include "hid_types.hpp"

namespace nemu::core::hid {

enum class FaceButtonLayout : u32 {
    NintendoStandard, // Xbox A -> Switch B, Xbox B -> Switch A, Xbox X -> Switch Y, Xbox Y -> Switch X
    XboxMirrored      // Xbox A -> Switch A, Xbox B -> Switch B, Xbox X -> Switch X, Xbox Y -> Switch Y
};

struct XboxGamepadState {
    bool a{false};
    bool b{false};
    bool x{false};
    bool y{false};
    bool lb{false};
    bool rb{false};
    float trigger_l{0.0f}; // 0.0f to 1.0f
    float trigger_r{0.0f}; // 0.0f to 1.0f
    bool start{false};
    bool back{false};
    bool lsb{false};
    bool rsb{false};
    bool dpad_up{false};
    bool dpad_down{false};
    bool dpad_left{false};
    bool dpad_right{false};
    s32 thumb_lx{0}; // -32768 to 32767
    s32 thumb_ly{0};
    s32 thumb_rx{0};
    s32 thumb_ry{0};
};

class ControllerMapper {
public:
    static constexpr float TRIGGER_THRESHOLD = 0.3f;

    /// Map raw Xbox gamepad input to Nintendo Switch NpadCommonState
    [[nodiscard]] static NpadCommonState MapXboxToNpad(
        const XboxGamepadState& xbox,
        s64 sampling_number,
        FaceButtonLayout layout = FaceButtonLayout::NintendoStandard,
        float inner_deadzone = 0.15f,
        float outer_deadzone = 0.95f) noexcept;

    /// Map Switch HD Rumble vibration packet to Xbox dual motor rumble levels
    static void MapVibrationToMotors(
        const NpadVibrationValue& vib,
        float master_strength,
        float& out_low_motor,
        float& out_high_motor) noexcept;
};

} // namespace nemu::core::hid
