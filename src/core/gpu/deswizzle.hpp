#pragma once

#include "core/types.hpp"
#include <span>

namespace nemu::core::gpu {

class TextureSwizzler {
public:
    static constexpr u32 GOB_WIDTH_BYTES = 64;
    static constexpr u32 GOB_HEIGHT_LINES = 8;
    static constexpr u32 GOB_SIZE_BYTES = GOB_WIDTH_BYTES * GOB_HEIGHT_LINES; // 512 bytes

    /// Calculate swizzled byte offset within a single 64x8 GOB
    [[nodiscard]] static constexpr u32 GetGobOffset(u32 x_bytes, u32 y) noexcept {
        return (x_bytes & 0x0F) |
               ((y & 0x01) << 4) |
               (((x_bytes >> 4) & 0x01) << 5) |
               (((y >> 1) & 0x03) << 6) |
               (((x_bytes >> 5) & 0x01) << 8);
    }

    /// Deswizzle Tegra X1 GM20B block-linear memory to linear pitch memory
    static bool DeswizzleBlockLinear(
        std::span<const u8> src_swizzled,
        std::span<u8> dst_linear,
        u32 width,
        u32 height,
        u32 bytes_per_pixel,
        u32 block_height_gobs = 1);

    /// Swizzle linear pitch memory to Tegra X1 GM20B block-linear memory
    static bool SwizzleBlockLinear(
        std::span<const u8> src_linear,
        std::span<u8> dst_swizzled,
        u32 width,
        u32 height,
        u32 bytes_per_pixel,
        u32 block_height_gobs = 1);
};

} // namespace nemu::core::gpu
