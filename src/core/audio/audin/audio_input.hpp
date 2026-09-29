#pragma once

#include "core/types.hpp"
#include <span>
#include <string>
#include <vector>
#include <memory>
#include <mutex>

namespace nemu::core::audio::audin {

struct AudioInputDevice {
    std::string name;
    u32 sample_rate{48000};
    u32 channels{1};
    bool is_default{false};
};

class AudioInputManager {
public:
    AudioInputManager();
    ~AudioInputManager() = default;

    /// List available audio input devices
    [[nodiscard]] std::vector<AudioInputDevice> EnumerateDevices() const;

    /// Open an audio input recording stream
    bool OpenStream(std::string_view device_name, u32 sample_rate = 48000, u32 channels = 1);

    /// Close the active recording stream
    void CloseStream();

    /// Check if stream is currently recording
    [[nodiscard]] bool IsRecording() const noexcept { return is_recording_; }

    /// Read recorded PCM16 microphone samples into a guest buffer
    size_t ReadSamples(std::span<s16> out_buffer);

private:
    bool is_recording_{false};
    u32 sample_rate_{48000};
    u32 channels_{1};
    double phase_{0.0};
    mutable std::mutex mutex_;
};

} // namespace nemu::core::audio::audin
