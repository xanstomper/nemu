#include "controller_mapping.hpp"
#include "deadzone.hpp"

namespace nemu::core::hid {

NpadCommonState ControllerMapper::MapXboxToNpad(
    const XboxGamepadState& xbox,
    s64 sampling_number,
    FaceButtonLayout layout,
    float inner_deadzone,
    float outer_deadzone) noexcept {

    u64 buttons = 0;

    // Face buttons mapping
    if (layout == FaceButtonLayout::NintendoStandard) {
        if (xbox.b) buttons |= NpadButton::A;
        if (xbox.a) buttons |= NpadButton::B;
        if (xbox.y) buttons |= NpadButton::X;
        if (xbox.x) buttons |= NpadButton::Y;
    } else {
        if (xbox.a) buttons |= NpadButton::A;
        if (xbox.b) buttons |= NpadButton::B;
        if (xbox.x) buttons |= NpadButton::X;
        if (xbox.y) buttons |= NpadButton::Y;
    }

    // Bumpers
    if (xbox.lb) buttons |= NpadButton::L;
    if (xbox.rb) buttons |= NpadButton::R;

    // Triggers
    if (xbox.trigger_l >= TRIGGER_THRESHOLD) buttons |= NpadButton::ZL;
    if (xbox.trigger_r >= TRIGGER_THRESHOLD) buttons |= NpadButton::ZR;

    // System / Menu buttons
    if (xbox.start) buttons |= NpadButton::Plus;
    if (xbox.back)  buttons |= NpadButton::Minus;

    // Stick clicks
    if (xbox.lsb) buttons |= NpadButton::StickL;
    if (xbox.rsb) buttons |= NpadButton::StickR;

    // D-Pad
    if (xbox.dpad_up)    buttons |= NpadButton::DUp;
    if (xbox.dpad_down)  buttons |= NpadButton::DDown;
    if (xbox.dpad_left)  buttons |= NpadButton::DLeft;
    if (xbox.dpad_right) buttons |= NpadButton::DRight;

    // Filter analog sticks
    AnalogStickState stick_l = DeadzoneFilter::ApplyRadialDeadzone(
        xbox.thumb_lx, xbox.thumb_ly, inner_deadzone, outer_deadzone);

    AnalogStickState stick_r = DeadzoneFilter::ApplyRadialDeadzone(
        xbox.thumb_rx, xbox.thumb_ry, inner_deadzone, outer_deadzone);

    return NpadCommonState{
        .sampling_number = sampling_number,
        .buttons = buttons,
        .stick_l = stick_l,
        .stick_r = stick_r,
        .attributes = 1, // Connected / Active
        .reserved = 0
    };
}

} // namespace nemu::core::hid
