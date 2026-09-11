#include "audio_factory.hpp"
#include "null_audio_backend.hpp"
#include "platform/logger.hpp"

#ifdef _WIN32
#include "xaudio2/xaudio2_backend.hpp"
#endif

namespace nemu::core::audio {

std::shared_ptr<IAudioBackend> AudioFactory::CreateBackend(u32 sample_rate, u32 channels) {
#ifdef _WIN32
    auto xaudio = std::make_shared<XAudio2Backend>();
    if (xaudio->Initialize(sample_rate, channels)) {
        NEMU_LOG_INFO("Audio", "Using XAudio2 Hardware Audio Backend");
        return xaudio;
    }
    NEMU_LOG_WARN("Audio", "XAudio2 initialization failed, falling back to Null Audio backend");
#endif

    auto null_audio = std::make_shared<NullAudioBackend>();
    null_audio->Initialize(sample_rate, channels);
    return null_audio;
}

} // namespace nemu::core::audio
