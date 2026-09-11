#include "xaudio2_backend.hpp"

#ifdef _WIN32
#include "platform/logger.hpp"
#include <cstring>

namespace nemu::core::audio {

XAudio2Backend::XAudio2Backend() = default;

XAudio2Backend::~XAudio2Backend() {
    Shutdown();
}

bool XAudio2Backend::Initialize(u32 sample_rate, u32 channels) {
    sample_rate_ = sample_rate;
    channels_ = channels;

    HRESULT hr = XAudio2Create(&xaudio2_, 0, XAUDIO2_DEFAULT_PROCESSOR);
    if (FAILED(hr)) {
        NEMU_LOG_WARN("Audio", "XAudio2Create failed: 0x{:08X}", static_cast<u32>(hr));
        return false;
    }

    hr = xaudio2_->CreateMasteringVoice(&mastering_voice_);
    if (FAILED(hr)) {
        NEMU_LOG_WARN("Audio", "CreateMasteringVoice failed: 0x{:08X}", static_cast<u32>(hr));
        xaudio2_.Reset();
        return false;
    }

    WAVEFORMATEX wfx{};
    wfx.wFormatTag = WAVE_FORMAT_PCM;
    wfx.nChannels = static_cast<WORD>(channels_);
    wfx.nSamplesPerSec = sample_rate_;
    wfx.wBitsPerSample = 16;
    wfx.nBlockAlign = static_cast<WORD>((wfx.nChannels * wfx.wBitsPerSample) / 8);
    wfx.nAvgBytesPerSec = wfx.nSamplesPerSec * wfx.nBlockAlign;
    wfx.cbSize = 0;

    hr = xaudio2_->CreateSourceVoice(&source_voice_, &wfx, 0, XAUDIO2_DEFAULT_FREQ_RATIO, this);
    if (FAILED(hr)) {
        NEMU_LOG_WARN("Audio", "CreateSourceVoice failed: 0x{:08X}", static_cast<u32>(hr));
        mastering_voice_->DestroyVoice();
        mastering_voice_ = nullptr;
        xaudio2_.Reset();
        return false;
    }

    initialized_ = true;
    NEMU_LOG_INFO("Audio", "Initialized XAudio2 backend ({} Hz, {} channels)", sample_rate_, channels_);
    return true;
}

void XAudio2Backend::Shutdown() {
    if (initialized_) {
        Stop();
        if (source_voice_) {
            source_voice_->DestroyVoice();
            source_voice_ = nullptr;
        }
        if (mastering_voice_) {
            mastering_voice_->DestroyVoice();
            mastering_voice_ = nullptr;
        }
        xaudio2_.Reset();
        initialized_ = false;
        NEMU_LOG_INFO("Audio", "Shutdown XAudio2 backend");
    }
}

bool XAudio2Backend::Start() {
    if (!initialized_ || !source_voice_) return false;
    HRESULT hr = source_voice_->Start(0);
    if (SUCCEEDED(hr)) {
        running_ = true;
        return true;
    }
    return false;
}

void XAudio2Backend::Stop() {
    if (running_ && source_voice_) {
        source_voice_->Stop(0);
        running_ = false;
    }
}

size_t XAudio2Backend::QueueSamples(std::span<const StereoFrame16> samples) {
    if (!initialized_ || !running_ || !source_voice_ || samples.empty()) return 0;

    const size_t byte_count = samples.size() * sizeof(StereoFrame16);
    std::vector<u8> buf(byte_count);
    std::memcpy(buf.data(), samples.data(), byte_count);

    XAUDIO2_BUFFER xbuf{};
    xbuf.AudioBytes = static_cast<UINT32>(byte_count);
    xbuf.pAudioData = buf.data();
    xbuf.Flags = 0;
    xbuf.pContext = nullptr;

    {
        std::lock_guard lock(audio_mutex_);
        active_buffers_.push_back(std::move(buf));
        xbuf.pAudioData = active_buffers_.back().data();
        xbuf.pContext = active_buffers_.back().data();
    }

    HRESULT hr = source_voice_->SubmitSourceBuffer(&xbuf);
    if (SUCCEEDED(hr)) {
        queued_frames_ += samples.size();
        return samples.size();
    }

    return 0;
}

size_t XAudio2Backend::GetQueuedFramesCount() const noexcept {
    if (!source_voice_) return 0;
    XAUDIO2_VOICE_STATE state;
    source_voice_->GetState(&state);
    return state.BuffersQueued;
}

float XAudio2Backend::GetLatencyMs() const noexcept {
    if (sample_rate_ == 0) return 0.0f;
    return (static_cast<float>(GetQueuedFramesCount()) / static_cast<float>(sample_rate_)) * 1000.0f;
}

void XAudio2Backend::OnVoiceProcessingPassStart([[maybe_unused]] UINT32 bytes_required) {}
void XAudio2Backend::OnVoiceProcessingPassEnd() {}
void XAudio2Backend::OnStreamEnd() {}
void XAudio2Backend::OnBufferStart([[maybe_unused]] void* buffer_context) {}

void XAudio2Backend::OnBufferEnd(void* buffer_context) {
    std::lock_guard lock(audio_mutex_);
    for (auto it = active_buffers_.begin(); it != active_buffers_.end(); ++it) {
        if (it->data() == buffer_context) {
            active_buffers_.erase(it);
            break;
        }
    }
}

void XAudio2Backend::OnLoopEnd([[maybe_unused]] void* buffer_context) {}
void XAudio2Backend::OnVoiceError([[maybe_unused]] void* buffer_context, [[maybe_unused]] HRESULT error) {}

} // namespace nemu::core::audio
#endif // _WIN32
