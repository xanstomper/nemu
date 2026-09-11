#pragma once

#include "audio_interface.hpp"
#include "audio_ring_buffer.hpp"

namespace nemu::core::audio {

class NullAudioBackend final : public IAudioBackend {
public:
    NullAudioBackend();
    ~NullAudioBackend() override;

    bool Initialize(u32 sample_rate = DEFAULT_SAMPLE_RATE, u32 channels = DEFAULT_CHANNELS) override;
    void Shutdown() override;

    bool Start() override;
    void Stop() override;

    size_t QueueSamples(std::span<const StereoFrame16> samples) override;
    [[nodiscard]] size_t GetQueuedFramesCount() const noexcept override;
    [[nodiscard]] float GetLatencyMs() const noexcept override;
    [[nodiscard]] std::string_view GetBackendName() const noexcept override { return "Null / Headless Audio"; }

    [[nodiscard]] u64 GetTotalFramesPlayed() const noexcept { return total_frames_played_; }

private:
    bool initialized_{false};
    bool running_{false};
    u32 sample_rate_{DEFAULT_SAMPLE_RATE};
    u32 channels_{DEFAULT_CHANNELS};
    u64 total_frames_played_{0};
    AudioRingBuffer<StereoFrame16> ring_buffer_;
};

} // namespace nemu::core::audio
