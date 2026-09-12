#include "texture_types.hpp"
#include <cstring>

namespace nemu::core::gpu::texture {

TextureDescriptor TicParser::Parse(std::span<const u32, 8> tic_words) noexcept {
    TextureDescriptor desc{};

    const u32 w0 = tic_words[0];
    const u32 w1 = tic_words[1];
    const u32 w2 = tic_words[2];
    const u32 w3 = tic_words[3];
    const u32 w4 = tic_words[4];

    // Format & component routing
    const u32 raw_format = w0 & 0x7F;
    desc.swizzle_r = static_cast<TextureComponent>((w0 >> 7) & 0x7);
    desc.swizzle_g = static_cast<TextureComponent>((w0 >> 10) & 0x7);
    desc.swizzle_b = static_cast<TextureComponent>((w0 >> 13) & 0x7);
    desc.swizzle_a = static_cast<TextureComponent>((w0 >> 16) & 0x7);

    // 48-bit GPU Virtual Address
    const u64 addr_low = w1;
    const u64 addr_high = w2 & 0xFFFF;
    desc.gpu_address = (addr_high << 32) | addr_low;

    // Layout
    const u32 layout_type = (w2 >> 12) & 0x1;
    desc.is_block_linear = (layout_type == 0);
    const u32 block_height_shift = (w2 >> 13) & 0x7;
    desc.block_height_gobs = 1u << block_height_shift;

    // Dimensions
    desc.width = (w3 & 0xFFFF) + 1;
    desc.height = ((w3 >> 16) & 0xFFFF) + 1;
    desc.depth = (w4 & 0x3FFF) + 1;
    desc.mip_levels = ((w4 >> 14) & 0xF) + 1;

    // Format mapping & bytes-per-pixel
    switch (raw_format) {
        case 0x01: desc.format = TextureFormat::R8_UNORM; desc.bytes_per_pixel = 1; break;
        case 0x02: desc.format = TextureFormat::R8_UINT; desc.bytes_per_pixel = 1; break;
        case 0x03: desc.format = TextureFormat::R8_SNORM; desc.bytes_per_pixel = 1; break;
        case 0x04: desc.format = TextureFormat::R8_SINT; desc.bytes_per_pixel = 1; break;
        case 0x07: desc.format = TextureFormat::R8G8_UNORM; desc.bytes_per_pixel = 2; break;
        case 0x0B: desc.format = TextureFormat::RGBA8_UNORM; desc.bytes_per_pixel = 4; break;
        case 0x0C: desc.format = TextureFormat::RGBA8_SRGB; desc.bytes_per_pixel = 4; break;
        case 0x0E: desc.format = TextureFormat::BGRA8_UNORM; desc.bytes_per_pixel = 4; break;
        case 0x0F: desc.format = TextureFormat::BGRA8_SRGB; desc.bytes_per_pixel = 4; break;
        case 0x13: desc.format = TextureFormat::R16_UNORM; desc.bytes_per_pixel = 2; break;
        case 0x14: desc.format = TextureFormat::R16_FLOAT; desc.bytes_per_pixel = 2; break;
        case 0x19: desc.format = TextureFormat::RGBA16_UNORM; desc.bytes_per_pixel = 8; break;
        case 0x1A: desc.format = TextureFormat::RGBA16_FLOAT; desc.bytes_per_pixel = 8; break;
        case 0x1B: desc.format = TextureFormat::R32_FLOAT; desc.bytes_per_pixel = 4; break;
        case 0x1C: desc.format = TextureFormat::R32_UINT; desc.bytes_per_pixel = 4; break;
        case 0x1F: desc.format = TextureFormat::RGBA32_FLOAT; desc.bytes_per_pixel = 16; break;
        case 0x20: desc.format = TextureFormat::BC1_UNORM; desc.bytes_per_pixel = 4; break;
        case 0x21: desc.format = TextureFormat::BC2_UNORM; desc.bytes_per_pixel = 4; break;
        case 0x22: desc.format = TextureFormat::BC3_UNORM; desc.bytes_per_pixel = 4; break;
        case 0x23: desc.format = TextureFormat::BC4_UNORM; desc.bytes_per_pixel = 4; break;
        case 0x24: desc.format = TextureFormat::BC5_UNORM; desc.bytes_per_pixel = 4; break;
        case 0x25: desc.format = TextureFormat::BC7_UNORM; desc.bytes_per_pixel = 4; break;
        case 0x26: desc.format = TextureFormat::BC1_SRGB; desc.bytes_per_pixel = 4; break;
        case 0x27: desc.format = TextureFormat::BC3_SRGB; desc.bytes_per_pixel = 4; break;
        case 0x28: desc.format = TextureFormat::BC7_SRGB; desc.bytes_per_pixel = 4; break;
        case 0x30: desc.format = TextureFormat::ASTC_4x4; desc.bytes_per_pixel = 4; break;
        case 0x31: desc.format = TextureFormat::ASTC_5x4; desc.bytes_per_pixel = 4; break;
        case 0x32: desc.format = TextureFormat::ASTC_5x5; desc.bytes_per_pixel = 4; break;
        case 0x33: desc.format = TextureFormat::ASTC_6x5; desc.bytes_per_pixel = 4; break;
        case 0x34: desc.format = TextureFormat::ASTC_6x6; desc.bytes_per_pixel = 4; break;
        case 0x35: desc.format = TextureFormat::ASTC_8x5; desc.bytes_per_pixel = 4; break;
        case 0x36: desc.format = TextureFormat::ASTC_8x6; desc.bytes_per_pixel = 4; break;
        case 0x37: desc.format = TextureFormat::ASTC_8x8; desc.bytes_per_pixel = 4; break;
        case 0x38: desc.format = TextureFormat::ASTC_10x5; desc.bytes_per_pixel = 4; break;
        case 0x39: desc.format = TextureFormat::ASTC_10x6; desc.bytes_per_pixel = 4; break;
        case 0x3A: desc.format = TextureFormat::ASTC_10x8; desc.bytes_per_pixel = 4; break;
        case 0x3B: desc.format = TextureFormat::ASTC_10x10; desc.bytes_per_pixel = 4; break;
        case 0x3C: desc.format = TextureFormat::ASTC_12x10; desc.bytes_per_pixel = 4; break;
        case 0x3D: desc.format = TextureFormat::ASTC_12x12; desc.bytes_per_pixel = 4; break;
        default:
            desc.format = TextureFormat::RGBA8_UNORM;
            desc.bytes_per_pixel = 4;
            break;
    }

    return desc;
}

SamplerDescriptor TscParser::Parse(std::span<const u32, 8> tsc_words) noexcept {
    SamplerDescriptor desc{};

    const u32 w0 = tsc_words[0];
    const u32 w1 = tsc_words[1];

    desc.wrap_u = static_cast<SamplerWrapMode>(w0 & 0x7);
    desc.wrap_v = static_cast<SamplerWrapMode>((w0 >> 3) & 0x7);
    desc.wrap_w = static_cast<SamplerWrapMode>((w0 >> 6) & 0x7);

    const u32 min_raw = (w0 >> 12) & 0x3;
    const u32 mag_raw = (w0 >> 15) & 0x3;
    desc.min_filter = (min_raw == 1) ? SamplerFilter::Linear : SamplerFilter::Nearest;
    desc.mag_filter = (mag_raw == 1) ? SamplerFilter::Linear : SamplerFilter::Nearest;
    desc.mip_filter = static_cast<MipFilter>((w0 >> 18) & 0x3);

    desc.min_lod = static_cast<float>(w1 & 0xFFF) / 256.0f;
    desc.max_lod = static_cast<float>((w1 >> 12) & 0xFFF) / 256.0f;
    desc.lod_bias = static_cast<float>(static_cast<s8>((w1 >> 24) & 0xFF)) / 256.0f;

    for (size_t i = 0; i < 4; ++i) {
        float f = 0.0f;
        std::memcpy(&f, &tsc_words[2 + i], sizeof(float));
        desc.border_color[i] = f;
    }

    return desc;
}

} // namespace nemu::core::gpu::texture
