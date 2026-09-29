#pragma once

#include "core/types.hpp"
#include <array>
#include <vector>
#include <cstddef>
#include <span>
#include <cmath>
#include <algorithm>

namespace nemu::core::audio::audren {

enum class SampleFormat : u8 {
    Invalid = 0,
    Pcm8 = 1,
    Pcm16 = 2,
    Pcm24 = 3,
    Pcm32 = 4,
    PcmFloat = 5,
    Adpcm = 6,
};

enum class PlayState : u8 {
    Started = 0,
    Stopped = 1,
    Paused = 2,
};

enum class EffectType : u8 {
    Invalid = 0,
    BufferMix = 1,
    Auxiliary = 2,
    Delay = 3,
    Reverb = 4,
    IirFilter = 5,
    BiquadFilter = 6,
    Limiter = 7,
};

enum class ChannelConfig : u32 {
    Mono = 1,
    Stereo = 2,
    Surround51 = 6,
    Surround71 = 8,
};

/// 6-channel (5.1) mixing matrix parameters
struct VolumeMatrix {
    static constexpr size_t MAX_CHANNELS = 6;
    std::array<std::array<float, MAX_CHANNELS>, MAX_CHANNELS> matrix{};

    VolumeMatrix() {
        for (size_t i = 0; i < MAX_CHANNELS; ++i) {
            for (size_t j = 0; j < MAX_CHANNELS; ++j) {
                matrix[i][j] = (i == j) ? 1.0f : 0.0f;
            }
        }
    }
};

/// Parameters for a Biquad IIR audio filter
struct BiquadFilterParams {
    bool enabled{false};
    float b0{1.0f};
    float b1{0.0f};
    float b2{0.0f};
    float a1{0.0f};
    float a2{0.0f};
    float x1{0.0f};
    float x2{0.0f};
    float y1{0.0f};
    float y2{0.0f};

    void ResetHistory() noexcept {
        x1 = x2 = y1 = y2 = 0.0f;
    }

    void ConfigureLowPass(float sample_rate, float cutoff, float q = 0.7071f) noexcept {
        enabled = true;
        const float w0 = 2.0f * 3.14159265358979323846f * cutoff / sample_rate;
        const float alpha = std::sin(w0) / (2.0f * q);
        const float cos_w0 = std::cos(w0);
        const float a0 = 1.0f + alpha;
        b0 = ((1.0f - cos_w0) / 2.0f) / a0;
        b1 = (1.0f - cos_w0) / a0;
        b2 = ((1.0f - cos_w0) / 2.0f) / a0;
        a1 = (-2.0f * cos_w0) / a0;
        a2 = (1.0f - alpha) / a0;
        ResetHistory();
    }

    void ConfigureHighPass(float sample_rate, float cutoff, float q = 0.7071f) noexcept {
        enabled = true;
        const float w0 = 2.0f * 3.14159265358979323846f * cutoff / sample_rate;
        const float alpha = std::sin(w0) / (2.0f * q);
        const float cos_w0 = std::cos(w0);
        const float a0 = 1.0f + alpha;
        b0 = ((1.0f + cos_w0) / 2.0f) / a0;
        b1 = (-(1.0f + cos_w0)) / a0;
        b2 = ((1.0f + cos_w0) / 2.0f) / a0;
        a1 = (-2.0f * cos_w0) / a0;
        a2 = (1.0f - alpha) / a0;
        ResetHistory();
    }

    void ConfigureBandPass(float sample_rate, float center, float q = 1.0f) noexcept {
        enabled = true;
        const float w0 = 2.0f * 3.14159265358979323846f * center / sample_rate;
        const float alpha = std::sin(w0) / (2.0f * q);
        const float cos_w0 = std::cos(w0);
        const float a0 = 1.0f + alpha;
        b0 = alpha / a0;
        b1 = 0.0f;
        b2 = -alpha / a0;
        a1 = (-2.0f * cos_w0) / a0;
        a2 = (1.0f - alpha) / a0;
        ResetHistory();
    }

    void ConfigureNotch(float sample_rate, float notch_freq, float q = 10.0f) noexcept {
        enabled = true;
        const float w0 = 2.0f * 3.14159265358979323846f * notch_freq / sample_rate;
        const float alpha = std::sin(w0) / (2.0f * q);
        const float cos_w0 = std::cos(w0);
        const float a0 = 1.0f + alpha;
        b0 = 1.0f / a0;
        b1 = (-2.0f * cos_w0) / a0;
        b2 = 1.0f / a0;
        a1 = (-2.0f * cos_w0) / a0;
        a2 = (1.0f - alpha) / a0;
        ResetHistory();
    }

    [[nodiscard]] float Process(float in) noexcept {
        if (!enabled) return in;
        float out = b0 * in + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2;
        x2 = x1;
        x1 = in;
        y2 = y1;
        y1 = out;
        return out;
    }
};

/// Environmental acoustic delay parameters
struct DelayEffectParams {
    bool enabled{false};
    u32 delay_samples{2400}; // e.g. 50 ms at 48 kHz
    float feedback{0.35f};
    float dry_gain{1.0f};
    float wet_gain{0.25f};
    std::vector<float> delay_line;
    size_t write_pos{0};

    void Initialize(u32 max_delay) {
        delay_line.assign(max_delay > 0 ? max_delay : 48000, 0.0f);
        write_pos = 0;
    }

    [[nodiscard]] float Process(float in) noexcept {
        if (!enabled || delay_line.empty()) return in;
        size_t read_pos = (write_pos + delay_line.size() - delay_samples) % delay_line.size();
        float delayed = delay_line[read_pos];
        delay_line[write_pos] = in + delayed * feedback;
        write_pos = (write_pos + 1) % delay_line.size();
        return in * dry_gain + delayed * wet_gain;
    }
};

/// Multi-tap environmental reverberation effect engine
struct ReverbParams {
    bool enabled{false};
    float decay_time{1.5f};      // seconds
    float early_gain{0.4f};
    float late_gain{0.3f};
    float wet_gain{0.35f};
    float dry_gain{0.85f};
    std::array<std::vector<float>, 4> comb_delays;
    std::array<size_t, 4> comb_write_pos{0, 0, 0, 0};
    std::array<std::vector<float>, 2> allpass_delays;
    std::array<size_t, 2> allpass_write_pos{0, 0};

    void Initialize(u32 sample_rate) {
        enabled = true;
        const float sr = static_cast<float>(sample_rate);
        const size_t comb_lens[4] = {
            static_cast<size_t>(sr * 0.0297f), // ~30ms
            static_cast<size_t>(sr * 0.0371f), // ~37ms
            static_cast<size_t>(sr * 0.0411f), // ~41ms
            static_cast<size_t>(sr * 0.0437f)  // ~44ms
        };
        for (size_t i = 0; i < 4; ++i) {
            comb_delays[i].assign(std::max<size_t>(16, comb_lens[i]), 0.0f);
            comb_write_pos[i] = 0;
        }
        const size_t allpass_lens[2] = {
            static_cast<size_t>(sr * 0.005f),  // ~5ms
            static_cast<size_t>(sr * 0.0017f)  // ~1.7ms
        };
        for (size_t i = 0; i < 2; ++i) {
            allpass_delays[i].assign(std::max<size_t>(16, allpass_lens[i]), 0.0f);
            allpass_write_pos[i] = 0;
        }
    }

    [[nodiscard]] float Process(float in) noexcept {
        if (!enabled) return in;
        float comb_sum = 0.0f;
        for (size_t i = 0; i < 4; ++i) {
            auto& buf = comb_delays[i];
            if (buf.empty()) continue;
            size_t wp = comb_write_pos[i];
            float out = buf[wp];
            buf[wp] = in + out * (0.7f * std::clamp(decay_time, 0.1f, 5.0f) / 5.0f);
            comb_write_pos[i] = (wp + 1) % buf.size();
            comb_sum += out;
        }
        float ap = comb_sum * 0.25f;
        for (size_t i = 0; i < 2; ++i) {
            auto& buf = allpass_delays[i];
            if (buf.empty()) continue;
            size_t wp = allpass_write_pos[i];
            float delayed = buf[wp];
            float v = ap - delayed * 0.5f;
            buf[wp] = v;
            allpass_write_pos[i] = (wp + 1) % buf.size();
            ap = delayed + v * 0.5f;
        }
        return in * dry_gain + ap * wet_gain;
    }
};

/// Voice channel state inside the Audio Renderer
struct AudioVoice {
    u32 id{0};
    PlayState play_state{PlayState::Stopped};
    SampleFormat format{SampleFormat::Pcm16};
    u32 sample_rate{48000};
    u32 channels{2};
    float volume{1.0f};
    float pitch{1.0f}; // 1.0 = native pitch
    u64 sample_address{0};
    size_t sample_size{0};
    bool loop_enabled{false};
    u32 loop_start{0};
    u32 loop_end{0};

    // Submix destination and matrix
    u32 destination_mix_id{0};
    VolumeMatrix volume_matrix{};
    BiquadFilterParams biquad{};

    // Playback cursor
    double current_sample_offset{0.0};
    bool is_in_use{false};
};

/// Configuration returned by audren:u OpenAudioRenderer
struct AudioRendererConfig {
    u32 sample_rate{48000};
    u32 sample_count{240}; // 5 ms per audio quantum at 48 kHz
    u32 voice_count{64};
    u32 submix_count{8};
    u32 sink_count{2};
    u32 effect_count{8};
    ChannelConfig channel_config{ChannelConfig::Stereo};
};

} // namespace nemu::core::audio::audren
