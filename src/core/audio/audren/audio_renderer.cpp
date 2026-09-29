#include "audio_renderer.hpp"
#include "core/audio/adpcm/adpcm.hpp"
#include "platform/logger.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>

namespace nemu::core::audio::audren {

AudioRenderer::AudioRenderer() {
    AudioRendererConfig default_cfg{};
    Initialize(default_cfg, nullptr);
}

AudioRenderer::AudioRenderer(const AudioRendererConfig& config, std::shared_ptr<IAudioBackend> backend) {
    Initialize(config, std::move(backend));
}

bool AudioRenderer::Initialize(const AudioRendererConfig& config, std::shared_ptr<IAudioBackend> backend) {
    std::lock_guard<std::mutex> lock(mutex_);
    config_ = config;
    backend_ = std::move(backend);

    voices_.clear();
    voices_.resize(config_.voice_count);
    for (u32 i = 0; i < config_.voice_count; ++i) {
        voices_[i].id = i;
        voices_[i].is_in_use = false;
        voices_[i].play_state = PlayState::Stopped;
    }

    delay_effects_.clear();
    delay_effects_.resize(config_.submix_count);
    for (auto& eff : delay_effects_) {
        eff.Initialize(config_.sample_rate); // 1-second delay capacity
    }

    const size_t quantum = config_.sample_count > 0 ? config_.sample_count : 240;
    mix_bus_left_.assign(quantum, 0.0f);
    mix_bus_right_.assign(quantum, 0.0f);

    NEMU_LOG_INFO("AudioRenderer", "audren:u initialized with {} voices, {} submixes @ {} Hz (quantum: {} samples)",
                  config_.voice_count, config_.submix_count, config_.sample_rate, quantum);
    return true;
}

void AudioRenderer::Reset() {
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto& v : voices_) {
        v.play_state = PlayState::Stopped;
        v.current_sample_offset = 0.0;
        v.biquad.ResetHistory();
    }
    for (auto& eff : delay_effects_) {
        eff.Initialize(config_.sample_rate);
    }
}

u32 AudioRenderer::AllocateVoice() {
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto& v : voices_) {
        if (!v.is_in_use) {
            v.is_in_use = true;
            v.play_state = PlayState::Stopped;
            v.current_sample_offset = 0.0;
            v.volume = 1.0f;
            v.pitch = 1.0f;
            v.biquad.ResetHistory();
            return v.id;
        }
    }
    return 0; // fallback slot
}

void AudioRenderer::FreeVoice(u32 voice_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (voice_id < voices_.size()) {
        voices_[voice_id].is_in_use = false;
        voices_[voice_id].play_state = PlayState::Stopped;
    }
}

void AudioRenderer::SetVoicePlayState(u32 voice_id, PlayState state) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (voice_id < voices_.size()) {
        voices_[voice_id].play_state = state;
    }
}

void AudioRenderer::SetVoiceSource(
    u32 voice_id,
    u64 guest_sample_address,
    size_t sample_size,
    SampleFormat format,
    u32 sample_rate,
    u32 channels,
    bool loop_enabled,
    u32 loop_start,
    u32 loop_end
) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (voice_id >= voices_.size()) return;

    auto& v = voices_[voice_id];
    v.sample_address = guest_sample_address;
    v.sample_size = sample_size;
    v.format = format;
    v.sample_rate = (sample_rate > 0) ? sample_rate : 48000;
    v.channels = (channels > 0) ? channels : 2;
    v.loop_enabled = loop_enabled;
    v.loop_start = loop_start;
    v.loop_end = loop_end;
    v.current_sample_offset = 0.0;
}

void AudioRenderer::SetVoiceVolume(u32 voice_id, float volume) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (voice_id < voices_.size()) {
        voices_[voice_id].volume = std::max(0.0f, volume);
    }
}

void AudioRenderer::SetVoicePitch(u32 voice_id, float pitch) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (voice_id < voices_.size()) {
        voices_[voice_id].pitch = std::clamp(pitch, 0.05f, 4.0f);
    }
}

void AudioRenderer::SetVoiceBiquad(u32 voice_id, const BiquadFilterParams& params) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (voice_id < voices_.size()) {
        voices_[voice_id].biquad = params;
    }
}

void AudioRenderer::SetVoiceVolumeMatrix(u32 voice_id, const VolumeMatrix& matrix) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (voice_id < voices_.size()) {
        voices_[voice_id].volume_matrix = matrix;
    }
}

void AudioRenderer::SetDelayEffect(u32 submix_id, const DelayEffectParams& params) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (submix_id < delay_effects_.size()) {
        delay_effects_[submix_id] = params;
    }
}

void AudioRenderer::ProcessCommandList(std::span<const u8> command_list, memory::VirtualMemory* vmm) {
    (void)vmm;
    if (command_list.empty()) return;

    // Simple opcode dispatcher for audren:u command stream:
    // Header format: [Opcode:1][Size:1][Parameters:Var]
    size_t offset = 0;
    const size_t len = command_list.size();

    while (offset + 2 <= len) {
        const u8 op = command_list[offset];
        const u8 cmd_size = command_list[offset + 1];
        if (cmd_size < 2 || offset + cmd_size > len) break;

        const u8* payload = &command_list[offset + 2];
        const size_t payload_size = cmd_size - 2;

        switch (op) {
            case 0x01: { // SetVoicePlayState: [VoiceID:2][PlayState:1]
                if (payload_size >= 3) {
                    u16 vid = static_cast<u16>(payload[0] | (payload[1] << 8));
                    PlayState state = static_cast<PlayState>(payload[2]);
                    SetVoicePlayState(vid, state);
                }
                break;
            }
            case 0x02: { // SetVoiceVolume: [VoiceID:2][VolumeFloat:4]
                if (payload_size >= 6) {
                    u16 vid = static_cast<u16>(payload[0] | (payload[1] << 8));
                    float vol = 1.0f;
                    std::memcpy(&vol, &payload[2], sizeof(float));
                    SetVoiceVolume(vid, vol);
                }
                break;
            }
            case 0x03: { // SetVoicePitch: [VoiceID:2][PitchFloat:4]
                if (payload_size >= 6) {
                    u16 vid = static_cast<u16>(payload[0] | (payload[1] << 8));
                    float pitch = 1.0f;
                    std::memcpy(&pitch, &payload[2], sizeof(float));
                    SetVoicePitch(vid, pitch);
                }
                break;
            }
            default:
                break;
        }

        offset += cmd_size;
    }
}

size_t AudioRenderer::RenderFrame(std::span<s16> out_pcm, memory::VirtualMemory* vmm) {
    std::lock_guard<std::mutex> lock(mutex_);
    const size_t frames = out_pcm.size() / 2;
    if (frames == 0) return 0;

    if (mix_bus_left_.size() < frames) {
        mix_bus_left_.resize(frames);
        mix_bus_right_.resize(frames);
    }

    std::fill_n(mix_bus_left_.begin(), frames, 0.0f);
    std::fill_n(mix_bus_right_.begin(), frames, 0.0f);

    // Mix all active voices
    for (auto& v : voices_) {
        if (!v.is_in_use || v.play_state != PlayState::Started || v.sample_address == 0) {
            continue;
        }

        const double pitch_ratio = (static_cast<double>(v.sample_rate) / static_cast<double>(config_.sample_rate)) * v.pitch;
        const size_t total_samples = v.sample_size / sizeof(s16);
        if (total_samples < 2) continue;

        // Fetch sample block from guest memory
        std::vector<s16> pcm_data(total_samples);
        if (vmm) {
            if (!vmm->ReadBlock(v.sample_address, pcm_data.data(), v.sample_size)) {
                continue;
            }
        }

        const float l_gain = v.volume * v.volume_matrix.matrix[0][0];
        const float r_gain = v.volume * v.volume_matrix.matrix[0][1];

        for (size_t t = 0; t < frames; ++t) {
            size_t idx = static_cast<size_t>(v.current_sample_offset);

            if (v.loop_enabled && v.loop_end > v.loop_start && idx >= v.loop_end) {
                v.current_sample_offset = v.loop_start;
                idx = v.loop_start;
            } else if (idx + 1 >= total_samples) {
                v.play_state = PlayState::Stopped;
                break;
            }

            const float s0 = static_cast<float>(pcm_data[idx]) / 32768.0f;
            const float s1 = static_cast<float>(pcm_data[idx + 1]) / 32768.0f;
            const float frac = static_cast<float>(v.current_sample_offset - static_cast<double>(idx));
            float sample = s0 * (1.0f - frac) + s1 * frac;

            // Apply voice biquad IIR filter
            sample = v.biquad.Process(sample);

            mix_bus_left_[t] += sample * l_gain;
            mix_bus_right_[t] += sample * r_gain;

            v.current_sample_offset += pitch_ratio;
        }
    }

    // Apply environmental delay/reverb on main mix bus
    if (!delay_effects_.empty()) {
        auto& eff = delay_effects_[0];
        if (eff.enabled) {
            for (size_t t = 0; t < frames; ++t) {
                mix_bus_left_[t] = eff.Process(mix_bus_left_[t]);
                mix_bus_right_[t] = eff.Process(mix_bus_right_[t]);
            }
        }
    }

    // Convert to interleaved 16-bit PCM with clipping protection
    for (size_t t = 0; t < frames; ++t) {
        float l = std::clamp(mix_bus_left_[t], -1.0f, 1.0f);
        float r = std::clamp(mix_bus_right_[t], -1.0f, 1.0f);
        out_pcm[t * 2 + 0] = static_cast<s16>(l * 32767.0f);
        out_pcm[t * 2 + 1] = static_cast<s16>(r * 32767.0f);
    }

    // Push to hardware audio backend if available
    if (backend_) {
        const auto* frames_ptr = reinterpret_cast<const StereoFrame16*>(out_pcm.data());
        backend_->QueueSamples(std::span<const StereoFrame16>(frames_ptr, frames));
    }

    return frames;
}

size_t AudioRenderer::GetActiveVoiceCount() const noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    size_t count = 0;
    for (const auto& v : voices_) {
        if (v.is_in_use && v.play_state == PlayState::Started) {
            count++;
        }
    }
    return count;
}

} // namespace nemu::core::audio::audren
