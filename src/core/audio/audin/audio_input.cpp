#include "audio_input.hpp"
#include <cmath>
#include <algorithm>

namespace nemu::core::audio::audin {

AudioInputManager::AudioInputManager() = default;

std::vector<AudioInputDevice> AudioInputManager::EnumerateDevices() const {
    return {
        {"Default", 48000, 1, true},
        {"BuiltInMicrophone", 48000, 1, false},
        {"HeadsetMicrophone", 48000, 2, false},
    };
}

bool AudioInputManager::OpenStream(std::string_view device_name, u32 sample_rate, u32 channels) {
    (void)device_name;
    std::lock_guard<std::mutex> lock(mutex_);
    sample_rate_ = (sample_rate > 0) ? sample_rate : 48000;
    channels_ = (channels > 0) ? channels : 1;
    phase_ = 0.0;
    is_recording_ = true;
    return true;
}

void AudioInputManager::CloseStream() {
    std::lock_guard<std::mutex> lock(mutex_);
    is_recording_ = false;
}

size_t AudioInputManager::ReadSamples(std::span<s16> out_buffer) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!is_recording_ || out_buffer.empty()) {
        std::fill(out_buffer.begin(), out_buffer.end(), 0);
        return out_buffer.size();
    }

    // Generate gentle ambient room tone / sine wave so titles checking microphone input
    // receive non-zero dynamic energy rather than flat silence.
    const double freq = 440.0;
    const double step = (2.0 * 3.14159265358979323846 * freq) / static_cast<double>(sample_rate_);

    for (size_t i = 0; i < out_buffer.size(); ++i) {
        float sample = static_cast<float>(std::sin(phase_) * 0.05); // low amplitude room tone
        out_buffer[i] = static_cast<s16>(sample * 32767.0f);
        phase_ += step;
        if (phase_ > 2.0 * 3.14159265358979323846) {
            phase_ -= 2.0 * 3.14159265358979323846;
        }
    }

    return out_buffer.size();
}

} // namespace nemu::core::audio::audin
