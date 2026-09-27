#pragma once

#include "core/types.hpp"
#include <span>
#include <vector>
#include <cstdint>

namespace nemu::core::gpu::texture {

// ---------------------------------------------------------------------------
// BC1 (DXT1) encoder — Tier-B1 texture recompression.
//
// Games ship textures as ASTC (variable footprint up to 12x12). The software
// ASTC decoder expands to RGBA8 (up to 34x memory blowup, unusable framerate
// on Xbox). The fix mirrors yuzu's "accelerated" ASTC path: decompress each
// ASTC block once, then immediately recompress to BC1 (8 bytes per 4x4 block
// = 0.5 bytes/pixel) and upload THAT. D3D12 natively samples BC1, so the GPU
// does the decode at draw time for free.
//
// BC1 block layout (8 bytes):
//   u16 color0, u16 color1  (RGB565 endpoints; c0 > c1 for 4-color mode)
//   u32 indices (2 bits/pixel, row-major from LSB)
// ---------------------------------------------------------------------------
class Bc1Encoder {
public:
    struct Block {
        u16 color0;
        u16 color1;
        u32 indices;
    };
    static_assert(sizeof(Block) == 8);

    /// Encode an RGBA8 surface (w*h*u32, 0xAABBGGRR byte order) into BC1.
    /// Returns 4-bit-per-pixel packed blocks, row-major, width must be
    /// padded to 4. Output size = (w/4)*(h/4)*8 bytes.
    static bool EncodeRGBA8(
        std::span<const u8> rgba8,
        u32 width,
        u32 height,
        std::vector<u8>& out_bc1
    );

    /// Encode a single 4x4 RGBA8 block (64 bytes).
    static Block EncodeBlock(std::span<const u8, 64> rgba_block);

    /// RGB565 color quantization for one endpoint pair via exhaustive min-error
    /// search over the 4-color palette (matches D3D's decoder behavior).
    static void PickEndpoints(std::span<const u8, 64> rgba_block, u16& c0, u16& c1);

    /// Pack an RGBA8 pixel into RGB565.
    [[nodiscard]] static constexpr u16 Pack565(u8 r, u8 g, u8 b) noexcept {
        return static_cast<u16>(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
    }

    /// Unpack RGB565 back to 8-bit RGB (bit-replication for accuracy).
    [[nodiscard]] static constexpr void Unpack565(u16 c, u8& r, u8& g, u8& b) noexcept {
        r = static_cast<u8>(((c >> 11) & 0x1F) << 3 | ((c >> 11) & 0x1F) >> 2);
        g = static_cast<u8>(((c >> 5) & 0x3F) << 2 | ((c >> 5) & 0x3F) >> 4);
        b = static_cast<u8>((c & 0x1F) << 3 | (c & 0x1F) >> 2);
    }
};

} // namespace nemu::core::gpu::texture
