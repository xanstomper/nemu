#include "null_audio_backend.hpp"
#include "platform/logger.hpp"

namespace nemu::core::audio {

NullAudioBackend::NullAudioBackend()
    : ring_buffer_(32768) {
}

NullAudioBackend::~NullAudioBackend() {
    Shutdown();
}

bool NullAudioBackend::Initialize(u32 sample_rate, u32 channels) {
    sample_rate_ = sample_rate;
    channels_ = channels;
    ring_buffer_.Clear();
    initialized_ = true;
    NEMU_LOG_INFO("Audio", "Initialized Null Audio backend ({} Hz, {} channels)", sample_rate_, channels_);
    return true;
}

void NullAudioBackend::Shutdown() {
    if (initialized_) {
        Stop();
        initialized_ = false;
        NEMU_LOG_INFO("Audio", "Shutdown Null Audio backend");
    }
}

bool NullAudioBackend::Start() {
    if (!initialized_) return false;
    running_ = true;
    return true;
}

void NullAudioBackend::Stop() {
    running_ = false;
}

size_t NullAudioBackend::QueueSamples(std::span<const StereoFrame16> samples) {
    if (!initialized_ || !running_) return 0;
    if (volume_percent_ < 100) {
        // Master-volume scaling applied inline (Tier-B UI wiring).
        volume_scratch_.resize(samples.size());
        const float gain = static_cast<float>(volume_percent_) / 100.0f;
        for (size_t i = 0; i < samples.size(); ++i) {
            volume_scratch_[i].left =
                static_cast<s16>(std::clamp(static_cast<int>(static_cast<float>(samples[i].left) * gain), -32768, 32767));
            volume_scratch_[i].right =
                static_cast<s16>(std::clamp(static_cast<int>(static_cast<float>(samples[i].right) * gain), -32768, 32767));
        }
        samples = volume_scratch_;
    }
    const size_t pushed = ring_buffer_.Push(samples.data(), samples.size());
    total_frames_played_ += pushed;
    return pushed;
}

size_t NullAudioBackend::GetQueuedFramesCount() const noexcept {
    return ring_buffer_.GetAvailableRead();
}

float NullAudioBackend::GetLatencyMs() const noexcept {
    if (sample_rate_ == 0) return 0.0f;
    return (static_cast<float>(GetQueuedFramesCount()) / static_cast<float>(sample_rate_)) * 1000.0f;
}

} // namespace nemu::core::audio
