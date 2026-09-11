#pragma once

#include "audio_types.hpp"
#include <span>
#include <string_view>

namespace nemu::core::audio {

class IAudioBackend {
public:
    virtual ~IAudioBackend() = default;

    virtual bool Initialize(u32 sample_rate = DEFAULT_SAMPLE_RATE, u32 channels = DEFAULT_CHANNELS) = 0;
    virtual void Shutdown() = 0;

    virtual bool Start() = 0;
    virtual void Stop() = 0;

    virtual size_t QueueSamples(std::span<const StereoFrame16> samples) = 0;
    [[nodiscard]] virtual size_t GetQueuedFramesCount() const noexcept = 0;
    [[nodiscard]] virtual float GetLatencyMs() const noexcept = 0;
    [[nodiscard]] virtual std::string_view GetBackendName() const noexcept = 0;
};

} // namespace nemu::core::audio
