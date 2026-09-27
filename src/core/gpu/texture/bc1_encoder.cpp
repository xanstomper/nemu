#include "bc1_encoder.hpp"
#include <array>
#include <cstring>
#include <algorithm>

namespace nemu::core::gpu::texture {

void Bc1Encoder::PickEndpoints(std::span<const u8, 64> rgba_block, u16& c0, u16& c1) {
    // Exhaustive-ish endpoint search: find the RGB565 pair whose 4-color
    // palette minimizes total squared error over the block. This is the
    // simple O(1)-block-time classic; cluster refinement (averaging then
    // snapping) is the usual fast path, so start there and refine +-1 per
    // channel — good quality, far cheaper than full 2x15-bit search.
    u32 sum_r = 0, sum_g = 0, sum_b = 0, count = 0;
    for (u32 i = 0; i < 16; ++i) {
        const u8* px = rgba_block.data() + i * 4;
        const u8 a = px[3];
        if (a < 8) continue; // ignore fully transparent for endpoint fit
        sum_r += px[0];
        sum_g += px[1];
        sum_b += px[2];
        ++count;
    }
    u8 avg_r, avg_g, avg_b;
    if (count == 0) {
        avg_r = avg_g = avg_b = 0;
    } else {
        avg_r = static_cast<u8>(sum_r / count);
        avg_g = static_cast<u8>(sum_g / count);
        avg_b = static_cast<u8>(sum_b / count);
    }

    // Principal-direction split: project pixels on the luminance axis and use
    // the extremes as endpoints (cheap and much better than avg +- delta).
    u32 lo = 0xFFFFFFFFu, hi = 0;
    u8 lo_px[3] = {avg_r, avg_g, avg_b}, hi_px[3] = {avg_r, avg_g, avg_b};
    for (u32 i = 0; i < 16; ++i) {
        const u8* px = rgba_block.data() + i * 4;
        if (px[3] < 8) continue;
        const u32 lum = px[0] * 299 + px[1] * 587 + px[2] * 114;
        if (lum < lo) {
            lo = lum;
            lo_px[0] = px[0]; lo_px[1] = px[1]; lo_px[2] = px[2];
        }
        if (lum > hi) {
            hi = lum;
            hi_px[0] = px[0]; hi_px[1] = px[1]; hi_px[2] = px[2];
        }
    }

    u16 e0 = Pack565(lo_px[0], lo_px[1], lo_px[2]);
    u16 e1 = Pack565(hi_px[0], hi_px[1], hi_px[2]);
    if (e0 < e1) std::swap(e0, e1); // 4-color mode needs c0 >= c1
    c0 = e0;
    c1 = e1;
}

Bc1Encoder::Block Bc1Encoder::EncodeBlock(std::span<const u8, 64> rgba_block) {
    // Determine whether this block needs an alpha/transparent slot. A block is
    // "alpha" if any pixel is transparent (< 8 alpha). Such blocks use BC1's
    // 3-color + 1-transparent punch-through mode (signalled by c0 < c1), where
    // index 3 renders transparent. Opaque blocks use 4-color mode (c0 >= c1),
    // preserving 2 interpolated shades. This preserves sprite/UI alpha with no
    // memory cost vs BC3 (still 8 bytes/block).
    bool needs_alpha = false;
    for (u32 i = 0; i < 16; ++i) {
        if (rgba_block[i * 4 + 3] < 8) { needs_alpha = true; break; }
    }

    u16 c0 = 0, c1 = 0;
    PickEndpoints(rgba_block, c0, c1);

    if (needs_alpha) {
        // 3-color mode: c0 must be < c1. PickEndpoints returns c0 >= c1 for the
        // 4-color layout; swap so the decoder reads it as punch-through.
        std::swap(c0, c1);
    } else {
        // 4-color mode: keep c0 >= c1 (PickEndpoints already guarantees it).
    }

    // Build the palette. For alpha blocks the decoder uses only colors 0,1,2
    // (3-color) and index 3 = transparent; for opaque it uses all 4.
    std::array<std::array<u8, 3>, 4> palette{};
    Unpack565(c0, palette[0][0], palette[0][1], palette[0][2]);
    Unpack565(c1, palette[1][0], palette[1][1], palette[1][2]);
    for (int ch = 0; ch < 3; ++ch) {
        palette[2][ch] = static_cast<u8>((2 * palette[0][ch] + palette[1][ch]) / 3);
        palette[3][ch] = static_cast<u8>((palette[0][ch] + 2 * palette[1][ch]) / 3);
    }

    u32 indices = 0;
    for (u32 i = 0; i < 16; ++i) {
        const u8* px = rgba_block.data() + i * 4;
        // Transparent pixels -> index 3 (the alpha slot) regardless of color.
        if (needs_alpha && px[3] < 8) {
            indices |= 3u << (2 * i);
            continue;
        }
        // Nearest palette color among {0,1,2} for alpha blocks (index 3 is the
        // transparent hole), all 4 for opaque blocks.
        u32 best = 0, best_err = 0xFFFFFFFFu;
        const u32 limit = needs_alpha ? 3u : 4u;
        for (u32 p = 0; p < limit; ++p) {
            const int dr = px[0] - palette[p][0];
            const int dg = px[1] - palette[p][1];
            const int db = px[2] - palette[p][2];
            const u32 err = static_cast<u32>(dr * dr + dg * dg + db * db);
            if (err < best_err) {
                best_err = err;
                best = p;
            }
        }
        indices |= best << (2 * i);
    }

    return Block{c0, c1, indices};
}

bool Bc1Encoder::EncodeRGBA8(
    std::span<const u8> rgba8,
    u32 width,
    u32 height,
    std::vector<u8>& out_bc1
) {
    if (rgba8.size() < static_cast<size_t>(width) * height * 4) return false;
    if ((width % 4) != 0 || (height % 4) != 0) return false;

    const u32 blocks_x = width / 4;
    const u32 blocks_y = height / 4;
    out_bc1.resize(static_cast<size_t>(blocks_x) * blocks_y * 8);

    std::array<u8, 64> block_rgba{};
    for (u32 by = 0; by < blocks_y; ++by) {
        for (u32 bx = 0; bx < blocks_x; ++bx) {
            // Gather the 4x4 pixel block.
            for (u32 y = 0; y < 4; ++y) {
                for (u32 x = 0; x < 4; ++x) {
                    const u32 px_x = bx * 4 + x;
                    const u32 px_y = by * 4 + y;
                    const size_t src = (static_cast<size_t>(px_y) * width + px_x) * 4;
                    std::memcpy(&block_rgba[(y * 4 + x) * 4], rgba8.data() + src, 4);
                }
            }
            const Block b = EncodeBlock(block_rgba);
            const size_t dst = (static_cast<size_t>(by) * blocks_x + bx) * 8;
            std::memcpy(out_bc1.data() + dst, &b, 8);
        }
    }
    return true;
}

} // namespace nemu::core::gpu::texture
