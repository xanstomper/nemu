#pragma once

#include "core/types.hpp"
#include <span>
#include <vector>
#include <cstddef>
#include <string_view>
#include <optional>

namespace nemu::core::gpu::texture {

enum class AstcBlockDimension : u32 {
    Block4x4,
    Block5x4,
    Block5x5,
    Block6x5,
    Block6x6,
    Block8x5,
    Block8x6,
    Block8x8,
    Block10x5,
    Block10x6,
    Block10x8,
    Block10x10,
    Block12x10,
    Block12x12,
};

struct AstcBlockInfo {
    u32 block_width;
    u32 block_height;
    std::string_view name;
};

[[nodiscard]] constexpr AstcBlockInfo GetAstcBlockInfo(AstcBlockDimension dim) noexcept {
    switch (dim) {
        case AstcBlockDimension::Block4x4:   return {4, 4, "4x4"};
        case AstcBlockDimension::Block5x4:   return {5, 4, "5x4"};
        case AstcBlockDimension::Block5x5:   return {5, 5, "5x5"};
        case AstcBlockDimension::Block6x5:   return {6, 5, "6x5"};
        case AstcBlockDimension::Block6x6:   return {6, 6, "6x6"};
        case AstcBlockDimension::Block8x5:   return {8, 5, "8x5"};
        case AstcBlockDimension::Block8x6:   return {8, 6, "8x6"};
        case AstcBlockDimension::Block8x8:   return {8, 8, "8x8"};
        case AstcBlockDimension::Block10x5:  return {10, 5, "10x5"};
        case AstcBlockDimension::Block10x6:  return {10, 6, "10x6"};
        case AstcBlockDimension::Block10x8:  return {10, 8, "10x8"};
        case AstcBlockDimension::Block10x10: return {10, 10, "10x10"};
        case AstcBlockDimension::Block12x10: return {12, 10, "12x10"};
        case AstcBlockDimension::Block12x12: return {12, 12, "12x12"};
        default:                             return {4, 4, "4x4"};
    }
}

[[nodiscard]] constexpr std::optional<AstcBlockDimension> ParseBlockDimension(u32 bw, u32 bh) noexcept {
    if (bw == 4 && bh == 4) return AstcBlockDimension::Block4x4;
    if (bw == 5 && bh == 4) return AstcBlockDimension::Block5x4;
    if (bw == 5 && bh == 5) return AstcBlockDimension::Block5x5;
    if (bw == 6 && bh == 5) return AstcBlockDimension::Block6x5;
    if (bw == 6 && bh == 6) return AstcBlockDimension::Block6x6;
    if (bw == 8 && bh == 5) return AstcBlockDimension::Block8x5;
    if (bw == 8 && bh == 6) return AstcBlockDimension::Block8x6;
    if (bw == 8 && bh == 8) return AstcBlockDimension::Block8x8;
    if (bw == 10 && bh == 5) return AstcBlockDimension::Block10x5;
    if (bw == 10 && bh == 6) return AstcBlockDimension::Block10x6;
    if (bw == 10 && bh == 8) return AstcBlockDimension::Block10x8;
    if (bw == 10 && bh == 10) return AstcBlockDimension::Block10x10;
    if (bw == 12 && bh == 10) return AstcBlockDimension::Block12x10;
    if (bw == 12 && bh == 12) return AstcBlockDimension::Block12x12;
    return std::nullopt;
}

class AstcDecoder {
public:
    static constexpr size_t BLOCK_SIZE_BYTES = 16; // 128 bits per ASTC block

    /// Decodes a single 128-bit ASTC block into an output RGBA8 buffer of size (block_width * block_height)
    /// @param block_data 16 bytes of ASTC encoded block data
    /// @param block_width Block footprint width (e.g. 4, 5, 6, 8, 10, 12)
    /// @param block_height Block footprint height (e.g. 4, 5, 6, 8, 10, 12)
    /// @param out_rgba8 Buffer of u32 (0xAABBGGRR) of size at least block_width * block_height
    /// @param is_srgb True if sRGB gamma decompression should be applied
    static bool DecodeBlock(
        std::span<const u8, BLOCK_SIZE_BYTES> block_data,
        u32 block_width,
        u32 block_height,
        std::span<u32> out_rgba8,
        bool is_srgb = false
    );

    /// Decodes a full 2D surface of ASTC compressed blocks into a contiguous RGBA8 buffer
    /// @param astc_data Packed ASTC block stream
    /// @param width Width of the texture in pixels
    /// @param height Height of the texture in pixels
    /// @param block_width Width of each ASTC block (e.g. 4..12)
    /// @param block_height Height of each ASTC block (e.g. 4..12)
    /// @param out_rgba8 Destination vector to be filled with width * height RGBA8 pixels
    /// @param is_srgb True if sRGB gamma decompression should be applied
    static bool DecompressSurface(
        std::span<const u8> astc_data,
        u32 width,
        u32 height,
        u32 block_width,
        u32 block_height,
        std::vector<u32>& out_rgba8,
        bool is_srgb = false
    );

    /// Convert linear color component [0, 255] to sRGB [0, 255]
    [[nodiscard]] static u8 LinearToSrgb(u8 linear) noexcept;

    /// Convert sRGB color component [0, 255] to linear [0, 255]
    [[nodiscard]] static u8 SrgbToLinear(u8 srgb) noexcept;
};

} // namespace nemu::core::gpu::texture
