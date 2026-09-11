#pragma once

#include "core/audio/audio_interface.hpp"

#ifdef _WIN32
#include <xaudio2.h>
#include <wrl/client.h>
#include <vector>
#include <mutex>

namespace nemu::core::audio {

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wnon-virtual-dtor"
class XAudio2Backend final : public IAudioBackend, public IXAudio2VoiceCallback {
public:
    XAudio2Backend();
    ~XAudio2Backend() override;

    bool Initialize(u32 sample_rate = DEFAULT_SAMPLE_RATE, u32 channels = DEFAULT_CHANNELS) override;
    void Shutdown() override;

    bool Start() override;
    void Stop() override;

    size_t QueueSamples(std::span<const StereoFrame16> samples) override;
    [[nodiscard]] size_t GetQueuedFramesCount() const noexcept override;
    [[nodiscard]] float GetLatencyMs() const noexcept override;
    [[nodiscard]] std::string_view GetBackendName() const noexcept override { return "XAudio2 (Xbox Series S/X & Win32)"; }

    // IXAudio2VoiceCallback interface
    STDMETHODIMP_(void) OnVoiceProcessingPassStart(UINT32 bytes_required) override;
    STDMETHODIMP_(void) OnVoiceProcessingPassEnd() override;
    STDMETHODIMP_(void) OnStreamEnd() override;
    STDMETHODIMP_(void) OnBufferStart(void* buffer_context) override;
    STDMETHODIMP_(void) OnBufferEnd(void* buffer_context) override;
    STDMETHODIMP_(void) OnLoopEnd(void* buffer_context) override;
    STDMETHODIMP_(void) OnVoiceError(void* buffer_context, HRESULT error) override;

private:
    bool initialized_{false};
    bool running_{false};
    u32 sample_rate_{DEFAULT_SAMPLE_RATE};
    u32 channels_{DEFAULT_CHANNELS};

    Microsoft::WRL::ComPtr<IXAudio2> xaudio2_;
    IXAudio2MasteringVoice* mastering_voice_{nullptr};
    IXAudio2SourceVoice* source_voice_{nullptr};

    std::mutex audio_mutex_;
    std::vector<std::vector<u8>> active_buffers_;
    size_t queued_frames_{0};
};
#pragma GCC diagnostic pop

} // namespace nemu::core::audio
#endif // _WIN32
