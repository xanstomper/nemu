#pragma once

#include "core/types.hpp"
#include <cstdint>
#include <array>
#include <span>

namespace nemu::core::gpu::texture {

enum class TextureFormat : u32 {
    Unknown = 0,
    R8_UNORM = 1,
    R8_UINT = 2,
    R8_SNORM = 3,
    R8_SINT = 4,
    R8G8_UNORM = 5,
    R16_FLOAT = 6,
    R16_UNORM = 7,
    R32_FLOAT = 8,
    R32_UINT = 9,
    RGBA8_UNORM = 10,
    RGBA8_SRGB = 11,
    RGBA8_SNORM = 12,
    BGRA8_UNORM = 13,
    BGRA8_SRGB = 14,
    RGB10A2_UNORM = 15,
    RGBA16_FLOAT = 16,
    RGBA16_UNORM = 17,
    RGBA32_FLOAT = 18,
    RGBA32_UINT = 19,
    D16_UNORM = 20,
    D24_UNORM_S8_UINT = 21,
    D32_FLOAT = 22,
    BC1_UNORM = 23,
    BC1_SRGB = 24,
    BC2_UNORM = 25,
    BC2_SRGB = 26,
    BC3_UNORM = 27,
    BC3_SRGB = 28,
    BC4_UNORM = 29,
    BC4_SNORM = 30,
    BC5_UNORM = 31,
    BC5_SNORM = 32,
    BC7_UNORM = 33,
    BC7_SRGB = 34,
    ASTC_4x4 = 35,
    ASTC_5x4 = 36,
    ASTC_5x5 = 37,
    ASTC_6x5 = 38,
    ASTC_6x6 = 39,
    ASTC_8x5 = 40,
    ASTC_8x6 = 41,
    ASTC_8x8 = 42,
    ASTC_10x5 = 43,
    ASTC_10x6 = 44,
    ASTC_10x8 = 45,
    ASTC_10x10 = 46,
    ASTC_12x10 = 47,
    ASTC_12x12 = 48,
};

enum class TextureComponent : u8 {
    Zero = 0,
    One = 1,
    R = 2,
    G = 3,
    B = 4,
    A = 5,
};

enum class SamplerWrapMode : u8 {
    Repeat = 0,
    MirroredRepeat = 1,
    ClampToEdge = 2,
    ClampToBorder = 3,
    MirrorClampToEdge = 4,
};

enum class SamplerFilter : u8 {
    Nearest = 0,
    Linear = 1,
};

enum class MipFilter : u8 {
    None = 0,
    Nearest = 1,
    Linear = 2,
};

struct TextureDescriptor {
    u64 gpu_address{0};
    u32 width{1};
    u32 height{1};
    u32 depth{1};
    u32 mip_levels{1};
    TextureFormat format{TextureFormat::RGBA8_UNORM};
    bool is_block_linear{true};
    u32 block_height_gobs{1};
    u32 bytes_per_pixel{4};
    TextureComponent swizzle_r{TextureComponent::R};
    TextureComponent swizzle_g{TextureComponent::G};
    TextureComponent swizzle_b{TextureComponent::B};
    TextureComponent swizzle_a{TextureComponent::A};

    [[nodiscard]] bool IsCompressed() const noexcept {
        return (format >= TextureFormat::BC1_UNORM && format <= TextureFormat::ASTC_12x12);
    }

    [[nodiscard]] bool IsAstc() const noexcept {
        return (format >= TextureFormat::ASTC_4x4 && format <= TextureFormat::ASTC_12x12);
    }

    [[nodiscard]] size_t CalculateLinearSize() const noexcept {
        return static_cast<size_t>(width) * height * bytes_per_pixel;
    }
};

struct SamplerDescriptor {
    SamplerWrapMode wrap_u{SamplerWrapMode::ClampToEdge};
    SamplerWrapMode wrap_v{SamplerWrapMode::ClampToEdge};
    SamplerWrapMode wrap_w{SamplerWrapMode::ClampToEdge};
    SamplerFilter min_filter{SamplerFilter::Linear};
    SamplerFilter mag_filter{SamplerFilter::Linear};
    MipFilter mip_filter{MipFilter::Linear};
    float min_lod{0.0f};
    float max_lod{14.0f};
    float lod_bias{0.0f};
    std::array<float, 4> border_color{0.0f, 0.0f, 0.0f, 0.0f};
};

/// Parser for Maxwell 32-byte (8 u32 words) Texture Image Control (TIC)
class TicParser {
public:
    static TextureDescriptor Parse(std::span<const u32, 8> tic_words) noexcept;
};

/// Parser for Maxwell 32-byte (8 u32 words) Texture Sampler Control (TSC)
class TscParser {
public:
    static SamplerDescriptor Parse(std::span<const u32, 8> tsc_words) noexcept;
};

} // namespace nemu::core::gpu::texture
