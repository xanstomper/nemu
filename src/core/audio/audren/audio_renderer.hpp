#pragma once

#include "audren_types.hpp"
#include "core/audio/audio_interface.hpp"
#include "core/memory/virtual_memory.hpp"
#include <vector>
#include <memory>
#include <mutex>
#include <span>

namespace nemu::core::audio::audren {

/// Complete Nintendo Switch Audio Renderer (audren:u) DSP engine.
/// Executes modular 48 kHz mixing command lists, voice resampling, biquad
/// filtering, delay/reverb effects, and multi-channel surround downmixing.
class AudioRenderer {
public:
    AudioRenderer();
    explicit AudioRenderer(const AudioRendererConfig& config, std::shared_ptr<IAudioBackend> backend = nullptr);
    ~AudioRenderer() = default;

    /// Initialize the audio renderer with the requested voice and mix configuration
    bool Initialize(const AudioRendererConfig& config, std::shared_ptr<IAudioBackend> backend = nullptr);

    /// Reset all voices, submixes, and effect delay lines
    void Reset();

    /// Allocate a new voice channel
    [[nodiscard]] u32 AllocateVoice();

    /// Free a voice channel
    void FreeVoice(u32 voice_id);

    /// Set voice playback state (Started, Stopped, Paused)
    void SetVoicePlayState(u32 voice_id, PlayState state);

    /// Set voice audio source parameters
    void SetVoiceSource(
        u32 voice_id,
        u64 guest_sample_address,
        size_t sample_size,
        SampleFormat format,
        u32 sample_rate,
        u32 channels,
        bool loop_enabled = false,
        u32 loop_start = 0,
        u32 loop_end = 0
    );

    /// Configure voice volume and pitch
    void SetVoiceVolume(u32 voice_id, float volume);
    void SetVoicePitch(u32 voice_id, float pitch);

    /// Configure voice biquad IIR filter
    void SetVoiceBiquad(u32 voice_id, const BiquadFilterParams& params);

    /// Configure voice multi-channel volume routing matrix
    void SetVoiceVolumeMatrix(u32 voice_id, const VolumeMatrix& matrix);

    /// Configure environmental delay on a submix
    void SetDelayEffect(u32 submix_id, const DelayEffectParams& params);

    /// Configure environmental multi-tap reverb on main mix bus
    void SetReverbEffect(const ReverbParams& params);

    /// Execute an audren:u command list buffer sent by the game
    void ProcessCommandList(std::span<const u8> command_list, memory::VirtualMemory* vmm);

    /// Render a frame of 48 kHz mixed audio samples (typically 240 samples per tick)
    /// @param out_pcm Output interleaved stereo 16-bit PCM buffer
    /// @param vmm Virtual memory manager to read sample buffers
    size_t RenderFrame(std::span<s16> out_pcm, memory::VirtualMemory* vmm);

    [[nodiscard]] const AudioRendererConfig& GetConfig() const noexcept { return config_; }
    [[nodiscard]] size_t GetActiveVoiceCount() const noexcept;

private:
    AudioRendererConfig config_{};
    std::shared_ptr<IAudioBackend> backend_{nullptr};
    std::vector<AudioVoice> voices_;
    std::vector<DelayEffectParams> delay_effects_;
    ReverbParams reverb_effect_{};
    std::vector<float> mix_bus_left_;
    std::vector<float> mix_bus_right_;
    mutable std::mutex mutex_;
};

} // namespace nemu::core::audio::audren
