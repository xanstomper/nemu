#pragma once

#include "core/types.hpp"
#include <cstddef>

namespace nemu::core::audio {

constexpr u32 DEFAULT_SAMPLE_RATE = 48000;
constexpr u32 DEFAULT_CHANNELS = 2; // Stereo

enum class AudioFormat : u32 {
    Pcm16,
    Float32
};

enum class ChannelConfiguration : u32 {
    Mono = 1,
    Stereo = 2,
    Surround51 = 6
};

#pragma pack(push, 1)
struct alignas(4) StereoFrame16 {
    s16 left{0};
    s16 right{0};
};

struct alignas(8) StereoFrameFloat {
    float left{0.0f};
    float right{0.0f};
};
#pragma pack(pop)

static_assert(sizeof(StereoFrame16) == 4, "StereoFrame16 size mismatch");
static_assert(sizeof(StereoFrameFloat) == 8, "StereoFrameFloat size mismatch");

} // namespace nemu::core::audio
