#pragma once

#include "core/types.hpp"
#include <array>
#include <vector>
#include <cstddef>
#include <span>

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

/// Environmental acoustic delay/reverb parameters
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
