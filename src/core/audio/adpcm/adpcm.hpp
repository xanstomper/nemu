#pragma once

#include "core/types.hpp"
#include <array>
#include <span>
#include <algorithm>
#include <cstddef>

namespace nemu::core::audio::adpcm {

constexpr size_t SamplesPerFrame = 14;
constexpr size_t BytesPerFrame = 8;
constexpr size_t NumCoefficients = 16;
constexpr size_t NumCoeffPairs = 8;

/// Nintendo DSP ADPCM runtime history context
struct AdpcmContext {
    s16 yn0{0}; // Previous sample 1
    s16 yn1{0}; // Previous sample 2
};

/// 1-byte frame header (scale factor exponent and coefficient index)
struct AdpcmHeader {
    u8 scale{0};
    u8 coeff_index{0};
};

/// Sign-extend 4-bit nibble (-8 .. +7)
[[nodiscard]] inline constexpr s32 SignExtendNibble(u8 nibble) noexcept {
    return static_cast<s32>(static_cast<s8>(static_cast<u8>(nibble << 4)) >> 4);
}

/// Decode single 4-bit sample using Nintendo DSP ADPCM linear prediction formula
[[nodiscard]] inline s16 DecodeSample(s32 code, u8 scale, s32 coeff0, s32 coeff1, AdpcmContext& ctx) noexcept {
    const s32 xn = code * (1 << scale);
    const s32 prediction = coeff0 * ctx.yn0 + coeff1 * ctx.yn1;
    const s32 sample = ((xn << 11) + 0x400 + prediction) >> 11;
    const s16 saturated = static_cast<s16>(std::clamp<s32>(sample, -32768, 32767));
    ctx.yn1 = ctx.yn0;
    ctx.yn0 = saturated;
    return saturated;
}

/**
 * Decode a single 8-byte ADPCM frame into up to 14 PCM16 samples.
 *
 * @param frame_data Pointer to 8-byte frame (1 byte header + 7 bytes nibble data)
 * @param out_samples Output destination buffer for decoded PCM16 samples
 * @param coefficients Array of 16 signed 16-bit coefficient values (8 pairs)
 * @param ctx Voice history context (yn0, yn1), updated in place
 * @param max_samples Maximum samples to decode from this frame (up to 14)
 * @return Number of samples written
 */
size_t DecodeFrame(const u8* frame_data, s16* out_samples, const s16* coefficients,
                   AdpcmContext& ctx, size_t max_samples = SamplesPerFrame) noexcept;

/**
 * Decode a contiguous ADPCM byte stream into mono PCM16 samples.
 *
 * @param input Raw ADPCM byte stream (multiple 8-byte frames)
 * @param output Destination PCM16 buffer
 * @param coefficients 16 signed 16-bit coefficients
 * @param ctx Voice history context, updated in place
 * @return Total number of samples written to output
 */
size_t DecodeStream(std::span<const u8> input, std::span<s16> output,
                    std::span<const s16, NumCoefficients> coefficients,
                    AdpcmContext& ctx) noexcept;

/**
 * Decode multiple interleaved ADPCM channels into interleaved PCM16 stereo/multichannel output.
 */
size_t DecodeStreamInterleaved(std::span<const u8> input, std::span<s16> output,
                               size_t channels,
                               std::span<const std::array<s16, NumCoefficients>> coefficients,
                               std::span<AdpcmContext> contexts) noexcept;

} // namespace nemu::core::audio::adpcm
