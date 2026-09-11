#pragma once

#include "core/types.hpp"
#include <array>
#include <cstddef>

namespace nemu::core::hid {

namespace NpadButton {
    constexpr u64 A          = 1ULL << 0;
    constexpr u64 B          = 1ULL << 1;
    constexpr u64 X          = 1ULL << 2;
    constexpr u64 Y          = 1ULL << 3;
    constexpr u64 StickL     = 1ULL << 4;
    constexpr u64 StickR     = 1ULL << 5;
    constexpr u64 L          = 1ULL << 6;
    constexpr u64 R          = 1ULL << 7;
    constexpr u64 ZL         = 1ULL << 8;
    constexpr u64 ZR         = 1ULL << 9;
    constexpr u64 Plus       = 1ULL << 10;
    constexpr u64 Minus      = 1ULL << 11;
    constexpr u64 DLeft      = 1ULL << 12;
    constexpr u64 DUp        = 1ULL << 13;
    constexpr u64 DRight     = 1ULL << 14;
    constexpr u64 DDown      = 1ULL << 15;
    constexpr u64 LeftSL     = 1ULL << 16;
    constexpr u64 LeftSR     = 1ULL << 17;
    constexpr u64 RightSL    = 1ULL << 18;
    constexpr u64 RightSR    = 1ULL << 19;
} // namespace NpadButton

enum class ControllerType : u32 {
    None = 0,
    ProController = 1,
    Handheld = 2,
    JoyConDual = 3,
    JoyConLeft = 4,
    JoyConRight = 5
};

#pragma pack(push, 1)
struct AnalogStickState {
    s32 x{0}; // -32767 to +32767
    s32 y{0}; // -32767 to +32767
};

struct NpadCommonState {
    s64 sampling_number{0};
    u64 buttons{0};
    AnalogStickState stick_l{};
    AnalogStickState stick_r{};
    u32 attributes{0};
    u32 reserved{0};
};

struct NpadRingBuffer {
    s64 sampling_number{0};
    s64 count{0};
    s64 head{0};
    std::array<NpadCommonState, 17> entries{};
};
#pragma pack(pop)

static_assert(sizeof(AnalogStickState) == 8, "AnalogStickState size mismatch");
static_assert(sizeof(NpadCommonState) == 40, "NpadCommonState size mismatch");

struct NpadVibrationValue {
    float amp_low{0.0f};   // Low frequency amplitude [0.0, 1.0]
    float freq_low{160.0f}; // Low frequency in Hz [10.0, 350.0]
    float amp_high{0.0f};  // High frequency amplitude [0.0, 1.0]
    float freq_high{320.0f};// High frequency in Hz [50.0, 1250.0]
};

struct XboxVibrationState {
    float left_motor{0.0f};    // Low-frequency rumble [0.0, 1.0]
    float right_motor{0.0f};   // High-frequency rumble [0.0, 1.0]
    float left_trigger{0.0f};  // Xbox impulse trigger [0.0, 1.0]
    float right_trigger{0.0f}; // Xbox impulse trigger [0.0, 1.0]
};

struct SixAxisSensorState {
    float accel_x{0.0f};
    float accel_y{0.0f};
    float accel_z{1.0f}; // Standard 1G earth gravity
    float gyro_x{0.0f};  // Angular velocity in deg/sec
    float gyro_y{0.0f};
    float gyro_z{0.0f};
    float orientation_x{0.0f};
    float orientation_y{0.0f};
    float orientation_z{0.0f};
    float orientation_w{1.0f};
};

} // namespace nemu::core::hid
