#include "deswizzle.hpp"
#include <cstring>
#include <algorithm>

namespace nemu::core::gpu {

bool TextureSwizzler::DeswizzleBlockLinear(
    std::span<const u8> src_swizzled,
    std::span<u8> dst_linear,
    u32 width,
    u32 height,
    u32 bytes_per_pixel,
    u32 block_height_gobs) {

    if (width == 0 || height == 0 || bytes_per_pixel == 0 || block_height_gobs == 0) {
        return false;
    }

    const size_t linear_size = static_cast<size_t>(width) * height * bytes_per_pixel;
    if (dst_linear.size() < linear_size) {
        return false;
    }

    const u32 width_bytes = width * bytes_per_pixel;
    const u32 blocks_x = (width_bytes + GOB_WIDTH_BYTES - 1) / GOB_WIDTH_BYTES;
    const u32 lines_per_block = GOB_HEIGHT_LINES * block_height_gobs;
    const u32 block_size_bytes = GOB_SIZE_BYTES * block_height_gobs;

    for (u32 y = 0; y < height; ++y) {
        const u32 block_y = y / lines_per_block;
        const u32 y_in_block = y % lines_per_block;
        const u32 gob_in_block_y = y_in_block / GOB_HEIGHT_LINES;
        const u32 y_in_gob = y_in_block % GOB_HEIGHT_LINES;

        const size_t row_linear_offset = static_cast<size_t>(y) * width_bytes;

        for (u32 x = 0; x < width; ++x) {
            const u32 x_bytes = x * bytes_per_pixel;
            const u32 block_x = x_bytes / GOB_WIDTH_BYTES;
            const u32 x_in_gob = x_bytes % GOB_WIDTH_BYTES;

            const size_t block_offset = (static_cast<size_t>(block_y) * blocks_x + block_x) * block_size_bytes;
            const size_t gob_offset = static_cast<size_t>(gob_in_block_y) * GOB_SIZE_BYTES;
            const size_t in_gob_offset = GetGobOffset(x_in_gob, y_in_gob);

            const size_t swizzled_offset = block_offset + gob_offset + in_gob_offset;
            const size_t linear_offset = row_linear_offset + x_bytes;

            if (swizzled_offset + bytes_per_pixel > src_swizzled.size()) {
                return false;
            }

            for (u32 b = 0; b < bytes_per_pixel; ++b) {
                // Individual byte in multi-byte pixel
                const size_t byte_in_gob = GetGobOffset(x_in_gob + b, y_in_gob);
                const size_t byte_swizzled_offset = block_offset + gob_offset + byte_in_gob;
                dst_linear[linear_offset + b] = src_swizzled[byte_swizzled_offset];
            }
        }
    }

    return true;
}

bool TextureSwizzler::SwizzleBlockLinear(
    std::span<const u8> src_linear,
    std::span<u8> dst_swizzled,
    u32 width,
    u32 height,
    u32 bytes_per_pixel,
    u32 block_height_gobs) {

    if (width == 0 || height == 0 || bytes_per_pixel == 0 || block_height_gobs == 0) {
        return false;
    }

    const u32 width_bytes = width * bytes_per_pixel;
    const u32 blocks_x = (width_bytes + GOB_WIDTH_BYTES - 1) / GOB_WIDTH_BYTES;
    const u32 lines_per_block = GOB_HEIGHT_LINES * block_height_gobs;
    const u32 block_size_bytes = GOB_SIZE_BYTES * block_height_gobs;
    const u32 blocks_y = (height + lines_per_block - 1) / lines_per_block;
    const size_t total_swizzled_size = static_cast<size_t>(blocks_x) * blocks_y * block_size_bytes;

    if (dst_swizzled.size() < total_swizzled_size) {
        return false;
    }

    // Zero the destination buffer (for padding areas)
    std::memset(dst_swizzled.data(), 0, total_swizzled_size);

    for (u32 y = 0; y < height; ++y) {
        const u32 block_y = y / lines_per_block;
        const u32 y_in_block = y % lines_per_block;
        const u32 gob_in_block_y = y_in_block / GOB_HEIGHT_LINES;
        const u32 y_in_gob = y_in_block % GOB_HEIGHT_LINES;

        const size_t row_linear_offset = static_cast<size_t>(y) * width_bytes;

        for (u32 x = 0; x < width; ++x) {
            const u32 x_bytes = x * bytes_per_pixel;
            const u32 block_x = x_bytes / GOB_WIDTH_BYTES;
            const u32 x_in_gob = x_bytes % GOB_WIDTH_BYTES;

            const size_t block_offset = (static_cast<size_t>(block_y) * blocks_x + block_x) * block_size_bytes;
            const size_t gob_offset = static_cast<size_t>(gob_in_block_y) * GOB_SIZE_BYTES;
            const size_t linear_offset = row_linear_offset + x_bytes;

            for (u32 b = 0; b < bytes_per_pixel; ++b) {
                const size_t byte_in_gob = GetGobOffset(x_in_gob + b, y_in_gob);
                const size_t byte_swizzled_offset = block_offset + gob_offset + byte_in_gob;
                dst_swizzled[byte_swizzled_offset] = src_linear[linear_offset + b];
            }
        }
    }

    return true;
}

std::string_view TextureSwizzler::GetDeswizzleComputeShaderHlsl() noexcept {
    return R"(// Tegra X1 GM20B Block-Linear Deswizzler Compute Shader (Direct3D 12)
cbuffer DeswizzleParams : register(b0) {
    uint u_width;
    uint u_height;
    uint u_bytes_per_pixel;
    uint u_block_height_gobs;
};

ByteAddressBuffer g_swizzled_input : register(t0);
RWTexture2D<unorm float4> g_linear_output : register(u0);

uint GetGobOffset(uint x_bytes, uint y) {
    return (x_bytes & 0x0F) |
           ((y & 0x01) << 4) |
           (((x_bytes >> 4) & 0x01) << 5) |
           (((y >> 1) & 0x03) << 6) |
           (((x_bytes >> 5) & 0x01) << 8);
}

[numthreads(8, 8, 1)]
void CSMain(uint3 dispatch_id : SV_DispatchThreadID) {
    uint x = dispatch_id.x;
    uint y = dispatch_id.y;
    if (x >= u_width || y >= u_height) return;

    const uint GOB_WIDTH_BYTES = 64;
    const uint GOB_HEIGHT_LINES = 8;
    const uint GOB_SIZE_BYTES = 512;

    uint width_bytes = u_width * u_bytes_per_pixel;
    uint blocks_x = (width_bytes + GOB_WIDTH_BYTES - 1) / GOB_WIDTH_BYTES;
    uint lines_per_block = GOB_HEIGHT_LINES * u_block_height_gobs;
    uint block_size_bytes = GOB_SIZE_BYTES * u_block_height_gobs;

    uint block_y = y / lines_per_block;
    uint y_in_block = y % lines_per_block;
    uint gob_in_block_y = y_in_block / GOB_HEIGHT_LINES;
    uint y_in_gob = y_in_block % GOB_HEIGHT_LINES;

    uint x_bytes = x * u_bytes_per_pixel;
    uint block_x = x_bytes / GOB_WIDTH_BYTES;
    uint x_in_gob = x_bytes % GOB_WIDTH_BYTES;

    uint block_offset = (block_y * blocks_x + block_x) * block_size_bytes;
    uint gob_offset = gob_in_block_y * GOB_SIZE_BYTES;
    uint in_gob_offset = GetGobOffset(x_in_gob, y_in_gob);

    uint swizzled_offset = block_offset + gob_offset + in_gob_offset;

    uint raw_val = g_swizzled_input.Load(swizzled_offset & ~3);
    uint shift = (swizzled_offset & 3) * 8;
    uint pixel = raw_val >> shift;

    float r = float(pixel & 0xFF) / 255.0f;
    float g = float((pixel >> 8) & 0xFF) / 255.0f;
    float b = float((pixel >> 16) & 0xFF) / 255.0f;
    float a = float((pixel >> 24) & 0xFF) / 255.0f;

    g_linear_output[uint2(x, y)] = float4(r, g, b, a);
}
)";
}

} // namespace nemu::core::gpu
