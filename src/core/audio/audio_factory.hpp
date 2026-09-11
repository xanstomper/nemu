#pragma once

#include "audio_interface.hpp"
#include <memory>

namespace nemu::core::audio {

class AudioFactory {
public:
    static std::shared_ptr<IAudioBackend> CreateBackend(u32 sample_rate = DEFAULT_SAMPLE_RATE, u32 channels = DEFAULT_CHANNELS);
};

} // namespace nemu::core::audio
