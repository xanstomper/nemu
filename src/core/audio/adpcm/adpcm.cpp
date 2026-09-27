#include "adpcm.hpp"
#include <cstring>

namespace nemu::core::audio::adpcm {

size_t DecodeFrame(const u8* frame_data, s16* out_samples, const s16* coefficients,
                   AdpcmContext& ctx, size_t max_samples) noexcept {
    if (!frame_data || !out_samples || !coefficients || max_samples == 0) {
        return 0;
    }

    const u8 header_byte = frame_data[0];
    const u8 scale = header_byte & 0x0F;
    const u8 coeff_index = static_cast<u8>((header_byte >> 4) & 0x07);

    const s32 coeff0 = coefficients[coeff_index * 2 + 0];
    const s32 coeff1 = coefficients[coeff_index * 2 + 1];

    const size_t samples_to_decode = std::min(max_samples, SamplesPerFrame);
    size_t samples_decoded = 0;

    for (size_t byte_idx = 0; byte_idx < 7 && samples_decoded < samples_to_decode; ++byte_idx) {
        const u8 byte_val = frame_data[1 + byte_idx];

        // High nibble first
        const s32 code_hi = SignExtendNibble(static_cast<u8>((byte_val >> 4) & 0x0F));
        out_samples[samples_decoded++] = DecodeSample(code_hi, scale, coeff0, coeff1, ctx);

        if (samples_decoded >= samples_to_decode) {
            break;
        }

        // Low nibble second
        const s32 code_lo = SignExtendNibble(static_cast<u8>(byte_val & 0x0F));
        out_samples[samples_decoded++] = DecodeSample(code_lo, scale, coeff0, coeff1, ctx);
    }

    return samples_decoded;
}

size_t DecodeStream(std::span<const u8> input, std::span<s16> output,
                    std::span<const s16, NumCoefficients> coefficients,
                    AdpcmContext& ctx) noexcept {
    if (input.empty() || output.empty() || coefficients.size() < NumCoefficients) {
        return 0;
    }

    size_t in_offset = 0;
    size_t out_offset = 0;

    while (in_offset + BytesPerFrame <= input.size() && out_offset < output.size()) {
        const size_t remaining_out = output.size() - out_offset;
        const size_t decoded = DecodeFrame(
            &input[in_offset],
            &output[out_offset],
            coefficients.data(),
            ctx,
            remaining_out
        );

        in_offset += BytesPerFrame;
        out_offset += decoded;

        if (decoded < SamplesPerFrame) {
            break;
        }
    }

    return out_offset;
}

size_t DecodeStreamInterleaved(std::span<const u8> input, std::span<s16> output,
                               size_t channels,
                               std::span<const std::array<s16, NumCoefficients>> coefficients,
                               std::span<AdpcmContext> contexts) noexcept {
    if (channels == 0 || input.empty() || output.empty() ||
        coefficients.size() < channels || contexts.size() < channels) {
        return 0;
    }

    const size_t frame_block_size = BytesPerFrame * channels;
    size_t in_offset = 0;
    size_t out_frames = 0;
    const size_t max_out_frames = output.size() / channels;

    s16 channel_temp[SamplesPerFrame];

    while (in_offset + frame_block_size <= input.size() && out_frames < max_out_frames) {
        const size_t frames_to_decode = std::min(SamplesPerFrame, max_out_frames - out_frames);

        // Decode each channel's frame
        for (size_t ch = 0; ch < channels; ++ch) {
            const u8* ch_data = &input[in_offset + ch * BytesPerFrame];
            DecodeFrame(ch_data, channel_temp, coefficients[ch].data(), contexts[ch], frames_to_decode);

            // Interleave into output
            for (size_t s = 0; s < frames_to_decode; ++s) {
                output[(out_frames + s) * channels + ch] = channel_temp[s];
            }
        }

        in_offset += frame_block_size;
        out_frames += frames_to_decode;
    }

    return out_frames * channels;
}

} // namespace nemu::core::audio::adpcm
