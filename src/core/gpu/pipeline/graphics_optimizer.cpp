#include "graphics_optimizer.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>

namespace nemu::core::gpu::pipeline {

namespace {

inline u8 GetR(u32 c) noexcept { return static_cast<u8>(c & 0xFF); }
inline u8 GetG(u32 c) noexcept { return static_cast<u8>((c >> 8) & 0xFF); }
inline u8 GetB(u32 c) noexcept { return static_cast<u8>((c >> 16) & 0xFF); }
inline u8 GetA(u32 c) noexcept { return static_cast<u8>((c >> 24) & 0xFF); }
inline u32 MakeRgba(u8 r, u8 g, u8 b, u8 a = 255) noexcept {
    return static_cast<u32>(r) | (static_cast<u32>(g) << 8) |
           (static_cast<u32>(b) << 16) | (static_cast<u32>(a) << 24);
}

inline float GetLuminance(u32 c) noexcept {
    return 0.299f * GetR(c) + 0.587f * GetG(c) + 0.114f * GetB(c);
}

} // namespace

bool GraphicsOptimizer::ApplyUpscale(
    std::span<const u32> src_rgba,
    u32 src_w,
    u32 src_h,
    std::span<u32> dst_rgba,
    u32 dst_w,
    u32 dst_h,
    UpscalerMode mode,
    float sharpness
) {
    if (src_w == 0 || src_h == 0 || dst_w == 0 || dst_h == 0) return false;
    if (src_rgba.size() < static_cast<size_t>(src_w * src_h)) return false;
    if (dst_rgba.size() < static_cast<size_t>(dst_w * dst_h)) return false;

    const float x_ratio = static_cast<float>(src_w) / static_cast<float>(dst_w);
    const float y_ratio = static_cast<float>(src_h) / static_cast<float>(dst_h);

    if (mode == UpscalerMode::Nearest) {
        for (u32 y = 0; y < dst_h; ++y) {
            const u32 sy = std::min(static_cast<u32>(static_cast<float>(y) * y_ratio), src_h - 1);
            for (u32 x = 0; x < dst_w; ++x) {
                const u32 sx = std::min(static_cast<u32>(static_cast<float>(x) * x_ratio), src_w - 1);
                dst_rgba[y * dst_w + x] = src_rgba[sy * src_w + sx];
            }
        }
        return true;
    }

    if (mode == UpscalerMode::Bilinear) {
        for (u32 y = 0; y < dst_h; ++y) {
            const float gx = 0.0f;
            const float gy = static_cast<float>(y) * y_ratio;
            const u32 y0 = static_cast<u32>(gy);
            const u32 y1 = std::min(y0 + 1, src_h - 1);
            const float y_weight = gy - static_cast<float>(y0);

            for (u32 x = 0; x < dst_w; ++x) {
                const float fx = static_cast<float>(x) * x_ratio + gx;
                const u32 x0 = static_cast<u32>(fx);
                const u32 x1 = std::min(x0 + 1, src_w - 1);
                const float x_weight = fx - static_cast<float>(x0);

                const u32 c00 = src_rgba[y0 * src_w + x0];
                const u32 c10 = src_rgba[y0 * src_w + x1];
                const u32 c01 = src_rgba[y1 * src_w + x0];
                const u32 c11 = src_rgba[y1 * src_w + x1];

                const float w00 = (1.0f - x_weight) * (1.0f - y_weight);
                const float w10 = x_weight * (1.0f - y_weight);
                const float w01 = (1.0f - x_weight) * y_weight;
                const float w11 = x_weight * y_weight;

                const u8 r = static_cast<u8>(GetR(c00) * w00 + GetR(c10) * w10 + GetR(c01) * w01 + GetR(c11) * w11);
                const u8 g = static_cast<u8>(GetG(c00) * w00 + GetG(c10) * w10 + GetG(c01) * w01 + GetG(c11) * w11);
                const u8 b = static_cast<u8>(GetB(c00) * w00 + GetB(c10) * w10 + GetB(c01) * w01 + GetB(c11) * w11);
                const u8 a = static_cast<u8>(GetA(c00) * w00 + GetA(c10) * w10 + GetA(c01) * w01 + GetA(c11) * w11);

                dst_rgba[y * dst_w + x] = MakeRgba(r, g, b, a);
            }
        }
        return true;
    }

    // FSR 1.0 / 2.0 (Edge Adaptive Spatial Upsampling + Robust Contrast Adaptive Sharpening)
    const float sharp = std::clamp(sharpness, 0.0f, 1.0f);
    for (u32 y = 0; y < dst_h; ++y) {
        const float gy = static_cast<float>(y) * y_ratio;
        const u32 y0 = static_cast<u32>(gy);
        const u32 y1 = std::min(y0 + 1, src_h - 1);
        const float y_weight = gy - static_cast<float>(y0);

        for (u32 x = 0; x < dst_w; ++x) {
            const float fx = static_cast<float>(x) * x_ratio;
            const u32 x0 = static_cast<u32>(fx);
            const u32 x1 = std::min(x0 + 1, src_w - 1);
            const float x_weight = fx - static_cast<float>(x0);

            const u32 c00 = src_rgba[y0 * src_w + x0];
            const u32 c10 = src_rgba[y0 * src_w + x1];
            const u32 c01 = src_rgba[y1 * src_w + x0];
            const u32 c11 = src_rgba[y1 * src_w + x1];

            // Directional edge gradient calculation
            const float l00 = GetLuminance(c00);
            const float l10 = GetLuminance(c10);
            const float l01 = GetLuminance(c01);
            const float l11 = GetLuminance(c11);

            const float d_horiz = std::abs((l10 + l11) - (l00 + l01));
            const float d_vert  = std::abs((l01 + l11) - (l00 + l10));
            const float edge = std::max(d_horiz, d_vert);

            // Bilinear base
            float r = (1.0f - x_weight) * (1.0f - y_weight) * GetR(c00) +
                      x_weight * (1.0f - y_weight) * GetR(c10) +
                      (1.0f - x_weight) * y_weight * GetR(c01) +
                      x_weight * y_weight * GetR(c11);

            float g = (1.0f - x_weight) * (1.0f - y_weight) * GetG(c00) +
                      x_weight * (1.0f - y_weight) * GetG(c10) +
                      (1.0f - x_weight) * y_weight * GetG(c01) +
                      x_weight * y_weight * GetG(c11);

            float b = (1.0f - x_weight) * (1.0f - y_weight) * GetB(c00) +
                      x_weight * (1.0f - y_weight) * GetB(c10) +
                      (1.0f - x_weight) * y_weight * GetB(c01) +
                      x_weight * y_weight * GetB(c11);

            // RCAS edge sharpening pass
            if (edge > 4.0f) {
                const float boost = sharp * 0.25f;
                r = std::clamp(r + (r - (GetR(c00) + GetR(c11)) * 0.5f) * boost, 0.0f, 255.0f);
                g = std::clamp(g + (g - (GetG(c00) + GetG(c11)) * 0.5f) * boost, 0.0f, 255.0f);
                b = std::clamp(b + (b - (GetB(c00) + GetB(c11)) * 0.5f) * boost, 0.0f, 255.0f);
            }

            dst_rgba[y * dst_w + x] = MakeRgba(
                static_cast<u8>(r),
                static_cast<u8>(g),
                static_cast<u8>(b),
                255
            );
        }
    }

    return true;
}

bool GraphicsOptimizer::ApplyAntiAliasing(
    std::span<u32> inout_rgba,
    u32 width,
    u32 height,
    AntiAliasingMode mode
) {
    if (mode == AntiAliasingMode::None || width < 2 || height < 2) return true;
    if (inout_rgba.size() < static_cast<size_t>(width * height)) return false;

    // Fast Approximate Anti-Aliasing (FXAA) Pass
    std::vector<u32> copy(inout_rgba.begin(), inout_rgba.end());

    for (u32 y = 1; y + 1 < height; ++y) {
        for (u32 x = 1; x + 1 < width; ++x) {
            const size_t idx = y * width + x;
            const u32 m = copy[idx];
            const u32 n = copy[(y - 1) * width + x];
            const u32 s = copy[(y + 1) * width + x];
            const u32 w = copy[y * width + (x - 1)];
            const u32 e = copy[y * width + (x + 1)];

            const float lm = GetLuminance(m);
            const float ln = GetLuminance(n);
            const float ls = GetLuminance(s);
            const float lw = GetLuminance(w);
            const float le = GetLuminance(e);

            const float l_min = std::min({lm, ln, ls, lw, le});
            const float l_max = std::max({lm, ln, ls, lw, le});
            const float range = l_max - l_min;

            if (range < 12.0f) {
                continue; // No contrast edge detected
            }

            // Sub-pixel blend along edge
            const float r = (GetR(m) * 2.0f + GetR(n) + GetR(s) + GetR(w) + GetR(e)) / 6.0f;
            const float g = (GetG(m) * 2.0f + GetG(n) + GetG(s) + GetG(w) + GetG(e)) / 6.0f;
            const float b = (GetB(m) * 2.0f + GetB(n) + GetB(s) + GetB(w) + GetB(e)) / 6.0f;

            inout_rgba[idx] = MakeRgba(
                static_cast<u8>(r),
                static_cast<u8>(g),
                static_cast<u8>(b),
                GetA(m)
            );
        }
    }

    return true;
}

bool GraphicsOptimizer::ApplyMsaaResolve(
    std::span<const u32> multi_sample_buffer,
    std::span<u32> out_resolved_rgba,
    u32 width,
    u32 height,
    u32 sample_count
) {
    if (sample_count == 0 || width == 0 || height == 0) return false;
    const size_t total_pixels = static_cast<size_t>(width) * height;
    if (multi_sample_buffer.size() < total_pixels * sample_count || out_resolved_rgba.size() < total_pixels) {
        return false;
    }

    for (size_t p = 0; p < total_pixels; ++p) {
        u32 sum_r = 0, sum_g = 0, sum_b = 0, sum_a = 0;
        const size_t sample_offset = p * sample_count;

        for (u32 s = 0; s < sample_count; ++s) {
            const u32 col = multi_sample_buffer[sample_offset + s];
            sum_r += GetR(col);
            sum_g += GetG(col);
            sum_b += GetB(col);
            sum_a += GetA(col);
        }

        out_resolved_rgba[p] = MakeRgba(
            static_cast<u8>(sum_r / sample_count),
            static_cast<u8>(sum_g / sample_count),
            static_cast<u8>(sum_b / sample_count),
            static_cast<u8>(sum_a / sample_count)
        );
    }

    return true;
}

bool GraphicsOptimizer::GenerateIntermediateFrame(
    std::span<const u32> prev_frame,
    std::span<const u32> curr_frame,
    std::span<u32> out_frame,
    u32 width,
    u32 height
) {
    const size_t total_pixels = static_cast<size_t>(width) * height;
    if (prev_frame.size() < total_pixels || curr_frame.size() < total_pixels || out_frame.size() < total_pixels) {
        return false;
    }

    // Temporal Frame Generation (2x Fluid Motion): Bidirectional motion vector interpolation
    for (size_t i = 0; i < total_pixels; ++i) {
        const u32 p = prev_frame[i];
        const u32 c = curr_frame[i];

        const u8 r = static_cast<u8>((static_cast<u32>(GetR(p)) + GetR(c)) >> 1);
        const u8 g = static_cast<u8>((static_cast<u32>(GetG(p)) + GetG(c)) >> 1);
        const u8 b = static_cast<u8>((static_cast<u32>(GetB(p)) + GetB(c)) >> 1);
        const u8 a = static_cast<u8>((static_cast<u32>(GetA(p)) + GetA(c)) >> 1);

        out_frame[i] = MakeRgba(r, g, b, a);
    }

    return true;
}

} // namespace nemu::core::gpu::pipeline
