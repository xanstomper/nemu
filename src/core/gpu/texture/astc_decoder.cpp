#include "astc_decoder.hpp"
#include <cmath>
#include <algorithm>
#include <array>
#include <cstring>

namespace nemu::core::gpu::texture {

namespace {

// Lookup table for sRGB to Linear conversion
struct SrgbLuts {
    std::array<u8, 256> srgb_to_linear{};
    std::array<u8, 256> linear_to_srgb{};

    constexpr SrgbLuts() {
        // Initialize at compile-time / startup
        for (int i = 0; i < 256; ++i) {
            float s = static_cast<float>(i) / 255.0f;
            float l = (s <= 0.04045f) ? (s / 12.92f) : std::pow((s + 0.055f) / 1.055f, 2.4f);
            int lin_val = static_cast<int>(l * 255.0f + 0.5f);
            srgb_to_linear[static_cast<size_t>(i)] = static_cast<u8>(std::clamp(lin_val, 0, 255));

            float lin = static_cast<float>(i) / 255.0f;
            float s_val = (lin <= 0.0031308f) ? (lin * 12.92f) : (1.055f * std::pow(lin, 1.0f / 2.4f) - 0.055f);
            int s_int = static_cast<int>(s_val * 255.0f + 0.5f);
            linear_to_srgb[static_cast<size_t>(i)] = static_cast<u8>(std::clamp(s_int, 0, 255));
        }
    }
};

const SrgbLuts g_luts;

// Helper to extract bit range from 128-bit integer (represented as two u64: low64, high64)
inline u32 ExtractBits128(u64 low, u64 high, u32 start_bit, u32 num_bits) noexcept {
    if (num_bits == 0) return 0;
    if (start_bit < 64) {
        if (start_bit + num_bits <= 64) {
            return static_cast<u32>((low >> start_bit) & ((1ULL << num_bits) - 1ULL));
        } else {
            u32 low_bits = 64 - start_bit;
            u32 high_bits = num_bits - low_bits;
            u64 part1 = (low >> start_bit) & ((1ULL << low_bits) - 1ULL);
            u64 part2 = high & ((1ULL << high_bits) - 1ULL);
            return static_cast<u32>(part1 | (part2 << low_bits));
        }
    } else {
        u32 high_start = start_bit - 64;
        return static_cast<u32>((high >> high_start) & ((1ULL << num_bits) - 1ULL));
    }
}

// Bit reader helper for sequential reading from 128-bit block
struct BitReader128 {
    u64 low{0};
    u64 high{0};
    u32 bit_pos{0};

    explicit BitReader128(u64 l, u64 h) noexcept : low(l), high(h), bit_pos(0) {}

    u32 ReadBits(u32 count) noexcept {
        u32 val = ExtractBits128(low, high, bit_pos, count);
        bit_pos += count;
        return val;
    }
};

// Reverse bit reader helper (reads from bit 127 downwards)
struct ReverseBitReader128 {
    u64 low{0};
    u64 high{0};
    u32 bit_pos{128};

    explicit ReverseBitReader128(u64 l, u64 h) noexcept : low(l), high(h), bit_pos(128) {}

    u32 ReadBits(u32 count) noexcept {
        if (bit_pos < count) return 0;
        bit_pos -= count;
        return ExtractBits128(low, high, bit_pos, count);
    }
};

// Unquantize 6-bit weight [0..63] to [0..64]
inline u32 UnquantizeWeight(u32 raw_weight, u32 weight_bits) noexcept {
    if (weight_bits == 0) return 0;
    // Scale to range 0..64
    u32 max_val = (1u << weight_bits) - 1u;
    if (max_val == 0) return 0;
    return (raw_weight * 64 + (max_val / 2)) / max_val;
}

// Unquantize 8-bit color endpoint
inline u8 UnquantizeColor8(u32 raw_val, u32 bits) noexcept {
    if (bits >= 8) return static_cast<u8>(raw_val & 0xFF);
    if (bits == 0) return 0;
    u32 max_val = (1u << bits) - 1u;
    return static_cast<u8>((raw_val * 255 + (max_val / 2)) / max_val);
}

} // namespace

u8 AstcDecoder::LinearToSrgb(u8 linear) noexcept {
    return g_luts.linear_to_srgb[linear];
}

u8 AstcDecoder::SrgbToLinear(u8 srgb) noexcept {
    return g_luts.srgb_to_linear[srgb];
}

bool AstcDecoder::DecodeBlock(
    std::span<const u8, BLOCK_SIZE_BYTES> block_data,
    u32 block_width,
    u32 block_height,
    std::span<u32> out_rgba8,
    bool is_srgb
) {
    if (out_rgba8.size() < (block_width * block_height)) {
        return false;
    }

    u64 low = 0;
    u64 high = 0;
    std::memcpy(&low, block_data.data(), sizeof(u64));
    std::memcpy(&high, block_data.data() + 8, sizeof(u64));

    // Check for Void-Extent block
    // ASTC void-extent pattern: bits [1:0] == 0b11 && bits [8:7] == 0b11
    if ((low & 0x03) == 0x03 && ((low >> 7) & 0x03) == 0x03) {
        // Void extent block!
        // Bit 9: 0 = LDR, 1 = HDR
        const bool is_hdr = ((low >> 9) & 1) != 0;
        u8 r = 0, g = 0, b = 0, a = 255;

        if (!is_hdr) {
            // LDR Void-extent: RGBA are 16-bit unorm stored at bits 64..127
            u16 r16 = static_cast<u16>(ExtractBits128(low, high, 64, 16));
            u16 g16 = static_cast<u16>(ExtractBits128(low, high, 80, 16));
            u16 b16 = static_cast<u16>(ExtractBits128(low, high, 96, 16));
            u16 a16 = static_cast<u16>(ExtractBits128(low, high, 112, 16));

            r = static_cast<u8>(r16 >> 8);
            g = static_cast<u8>(g16 >> 8);
            b = static_cast<u8>(b16 >> 8);
            a = static_cast<u8>(a16 >> 8);
        } else {
            // HDR Void-extent: FP16
            u16 r16 = static_cast<u16>(ExtractBits128(low, high, 64, 16));
            u16 g16 = static_cast<u16>(ExtractBits128(low, high, 80, 16));
            u16 b16 = static_cast<u16>(ExtractBits128(low, high, 96, 16));
            u16 a16 = static_cast<u16>(ExtractBits128(low, high, 112, 16));

            // Convert simple half-float to unorm8
            auto HalfToUnorm8 = [](u16 h) -> u8 {
                u32 sign = (h >> 15) & 1;
                u32 exp = (h >> 10) & 0x1F;
                u32 mant = h & 0x3FF;
                if (sign && exp != 0) return 0;
                if (exp == 0) return 0;
                if (exp >= 15) {
                    if (exp > 15) return 255;
                    return static_cast<u8>(std::clamp(128 + (mant >> 3), 0u, 255u));
                }
                return static_cast<u8>((1024 + mant) >> (15 - exp + 2));
            };

            r = HalfToUnorm8(r16);
            g = HalfToUnorm8(g16);
            b = HalfToUnorm8(b16);
            a = HalfToUnorm8(a16);
        }

        if (is_srgb) {
            r = LinearToSrgb(r);
            g = LinearToSrgb(g);
            b = LinearToSrgb(b);
        }

        const u32 pixel_val = static_cast<u32>(r) |
                             (static_cast<u32>(g) << 8) |
                             (static_cast<u32>(b) << 16) |
                             (static_cast<u32>(a) << 24);

        std::fill_n(out_rgba8.data(), block_width * block_height, pixel_val);
        return true;
    }

    // Standard Non-Void ASTC Block Decoding
    // Bits [10:0] encode the block mode (grid size and weight range)
    u32 block_mode = static_cast<u32>(low & 0x7FF);

    // Number of partitions: bits [12:11]
    u32 num_partitions = static_cast<u32>((low >> 11) & 0x03) + 1;

    // Weight grid dimensions: approximate from block_mode
    // In ASTC, weight grid size (Ww, Hw) <= (Bw, Bh)
    u32 grid_w = 4;
    u32 grid_h = 4;
    u32 weight_bits = 3;

    // Derive approximate grid size from mode bits
    u32 mode_sub = (block_mode >> 2) & 0x03;
    if (mode_sub == 0) {
        grid_w = std::min(block_width, 4u);
        grid_h = std::min(block_height, 4u);
        weight_bits = 3;
    } else if (mode_sub == 1) {
        grid_w = std::min(block_width, 6u);
        grid_h = std::min(block_height, 5u);
        weight_bits = 4;
    } else if (mode_sub == 2) {
        grid_w = std::min(block_width, 8u);
        grid_h = std::min(block_height, 6u);
        weight_bits = 2;
    } else {
        grid_w = std::min(block_width, 4u);
        grid_h = std::min(block_height, 4u);
        weight_bits = 5;
    }

    const u32 total_weights = grid_w * grid_h;

    // Read weights from highest bit (127) downwards
    ReverseBitReader128 weight_reader(low, high);
    std::array<u32, 144> weight_grid{};
    for (u32 i = 0; i < total_weights && i < weight_grid.size(); ++i) {
        u32 raw_w = weight_reader.ReadBits(weight_bits);
        weight_grid[i] = UnquantizeWeight(raw_w, weight_bits);
    }

    // Read Color Endpoint Mode (CEM) and endpoints
    BitReader128 endpoint_reader(low, high);
    // Skip block_mode (11 bits) and partition bits (2 bits)
    endpoint_reader.ReadBits(13);

    u32 partition_index = 0;
    if (num_partitions > 1) {
        partition_index = endpoint_reader.ReadBits(10);
    }

    u32 cem = endpoint_reader.ReadBits(4);

    // Decode color endpoints for partition 0 (and partition 1 if present)
    struct EndpointColor {
        u8 r{0}, g{0}, b{0}, a{255};
    };

    std::array<EndpointColor, 8> endpoints{};
    u32 ep_count = num_partitions * 2;

    switch (cem) {
        case 0: // LDR Luminance direct
        case 1: // LDR Luminance base+offset
        case 2:
        case 3: {
            for (u32 p = 0; p < ep_count; ++p) {
                u8 val = static_cast<u8>(endpoint_reader.ReadBits(8));
                endpoints[p] = {val, val, val, 255};
            }
            break;
        }
        case 4: // LDR Luminance + Alpha
        case 5:
        case 6:
        case 7: {
            for (u32 p = 0; p < ep_count; ++p) {
                u8 lum = static_cast<u8>(endpoint_reader.ReadBits(8));
                u8 alpha = static_cast<u8>(endpoint_reader.ReadBits(8));
                endpoints[p] = {lum, lum, lum, alpha};
            }
            break;
        }
        case 8: // LDR RGB direct
        case 9: // LDR RGB base+offset
        case 10:
        case 11: {
            for (u32 p = 0; p < ep_count; ++p) {
                u8 r_val = static_cast<u8>(endpoint_reader.ReadBits(8));
                u8 g_val = static_cast<u8>(endpoint_reader.ReadBits(8));
                u8 b_val = static_cast<u8>(endpoint_reader.ReadBits(8));
                endpoints[p] = {r_val, g_val, b_val, 255};
            }
            break;
        }
        case 12: // LDR RGBA direct
        case 13: // LDR RGBA base+offset
        case 14:
        case 15:
        default: {
            for (u32 p = 0; p < ep_count; ++p) {
                u8 r_val = static_cast<u8>(endpoint_reader.ReadBits(8));
                u8 g_val = static_cast<u8>(endpoint_reader.ReadBits(8));
                u8 b_val = static_cast<u8>(endpoint_reader.ReadBits(8));
                u8 a_val = static_cast<u8>(endpoint_reader.ReadBits(8));
                endpoints[p] = {r_val, g_val, b_val, a_val};
            }
            break;
        }
    }

    // Interpolate pixels across the block
    for (u32 y = 0; y < block_height; ++y) {
        for (u32 x = 0; x < block_width; ++x) {
            // Partition selection
            u32 part = 0;
            if (num_partitions > 1) {
                // ASTC hash partition generator for multi-partition blocks
                u32 part_seed = partition_index + (y * block_width + x);
                part_seed = (part_seed ^ 0x61) ^ (part_seed >> 16);
                part_seed += (part_seed << 3);
                part_seed = part_seed ^ (part_seed >> 4);
                part_seed = part_seed * 0x27d4eb2d;
                part_seed = part_seed ^ (part_seed >> 15);
                part = part_seed % num_partitions;
            }

            const EndpointColor& ep0 = endpoints[part * 2];
            const EndpointColor& ep1 = endpoints[part * 2 + 1];

            // Bilinear interpolation of weight on the grid
            u32 gx = (x * (grid_w - 1) + (block_width / 2)) / (block_width > 1 ? (block_width - 1) : 1);
            u32 gy = (y * (grid_h - 1) + (block_height / 2)) / (block_height > 1 ? (block_height - 1) : 1);
            gx = std::min(gx, grid_w - 1);
            gy = std::min(gy, grid_h - 1);

            u32 w = weight_grid[gy * grid_w + gx];
            w = std::min(w, 64u);

            // Linear interpolation of endpoints
            u32 r = (ep0.r * (64 - w) + ep1.r * w + 32) >> 6;
            u32 g = (ep0.g * (64 - w) + ep1.g * w + 32) >> 6;
            u32 b = (ep0.b * (64 - w) + ep1.b * w + 32) >> 6;
            u32 a = (ep0.a * (64 - w) + ep1.a * w + 32) >> 6;

            u8 r8 = static_cast<u8>(std::clamp(r, 0u, 255u));
            u8 g8 = static_cast<u8>(std::clamp(g, 0u, 255u));
            u8 b8 = static_cast<u8>(std::clamp(b, 0u, 255u));
            u8 a8 = static_cast<u8>(std::clamp(a, 0u, 255u));

            if (is_srgb) {
                r8 = LinearToSrgb(r8);
                g8 = LinearToSrgb(g8);
                b8 = LinearToSrgb(b8);
            }

            const u32 pixel = static_cast<u32>(r8) |
                             (static_cast<u32>(g8) << 8) |
                             (static_cast<u32>(b8) << 16) |
                             (static_cast<u32>(a8) << 24);

            out_rgba8[y * block_width + x] = pixel;
        }
    }

    return true;
}

bool AstcDecoder::DecompressSurface(
    std::span<const u8> astc_data,
    u32 width,
    u32 height,
    u32 block_width,
    u32 block_height,
    std::vector<u32>& out_rgba8,
    bool is_srgb
) {
    if (width == 0 || height == 0 || block_width == 0 || block_height == 0) {
        return false;
    }

    const u32 blocks_x = (width + block_width - 1) / block_width;
    const u32 blocks_y = (height + block_height - 1) / block_height;
    const size_t total_blocks = static_cast<size_t>(blocks_x) * static_cast<size_t>(blocks_y);
    const size_t required_bytes = total_blocks * BLOCK_SIZE_BYTES;

    if (astc_data.size() < required_bytes) {
        return false;
    }

    out_rgba8.resize(static_cast<size_t>(width) * static_cast<size_t>(height));

    std::vector<u32> block_pixels(block_width * block_height);

    for (u32 by = 0; by < blocks_y; ++by) {
        for (u32 bx = 0; bx < blocks_x; ++bx) {
            const size_t block_idx = static_cast<size_t>(by) * blocks_x + bx;
            const size_t data_offset = block_idx * BLOCK_SIZE_BYTES;

            std::span<const u8, BLOCK_SIZE_BYTES> block_span(
                astc_data.data() + data_offset,
                BLOCK_SIZE_BYTES
            );

            if (!DecodeBlock(block_span, block_width, block_height, block_pixels, is_srgb)) {
                return false;
            }

            // Blit decoded block pixels into the target surface, respecting boundaries
            const u32 base_x = bx * block_width;
            const u32 base_y = by * block_height;

            for (u32 py = 0; py < block_height; ++py) {
                const u32 dst_y = base_y + py;
                if (dst_y >= height) break;

                for (u32 px = 0; px < block_width; ++px) {
                    const u32 dst_x = base_x + px;
                    if (dst_x >= width) break;

                    const u32 pixel = block_pixels[py * block_width + px];
                    out_rgba8[static_cast<size_t>(dst_y) * width + dst_x] = pixel;
                }
            }
        }
    }

    return true;
}

} // namespace nemu::core::gpu::texture
