#pragma once

#include "ipc_service.hpp"
#include "core/audio/audio_interface.hpp"
#include "core/kernel/k_event.hpp"
#include <memory>
#include <vector>
#include <atomic>

namespace nemu::core::kernel::ipc {

class AudioDeviceService final : public IIpcService {
public:
    explicit AudioDeviceService(std::shared_ptr<audio::IAudioBackend> backend);
    ~AudioDeviceService() override = default;

    enum : u32 {
        ListAudioDeviceName = 0x0,
        SetAudioDeviceOutputVolume = 0x1,
        GetAudioDeviceOutputVolume = 0x2,
        GetActiveAudioDeviceName = 0x3,
    };

    u32 HandleRequest(const IpcContext& ctx, const IpcRequestReader& request,
                      IpcReplyWriter& reply, u32 x_id) override;

private:
    std::shared_ptr<audio::IAudioBackend> backend_;
    float volume_{1.0f};
};

class AudioRendererService final : public IIpcService {
public:
    explicit AudioRendererService(std::shared_ptr<audio::IAudioBackend> backend, u32 sample_rate, u32 sample_count);
    ~AudioRendererService() override = default;

    enum : u32 {
        GetSampleRate = 0x0,
        GetSampleCount = 0x1,
        GetMixBufferCount = 0x2,
        GetState = 0x3,
        RequestUpdate = 0x4,
        Start = 0x5,
        Stop = 0x6,
        QuerySystemEvent = 0x7,
    };

    u32 HandleRequest(const IpcContext& ctx, const IpcRequestReader& request,
                      IpcReplyWriter& reply, u32 x_id) override;

    [[nodiscard]] bool IsRunning() const noexcept { return running_.load(); }
    [[nodiscard]] u64 GetTotalFramesRendered() const noexcept { return frames_rendered_.load(); }

    struct AudioVoice {
        bool active{false};
        bool playing{false};
        u32 sample_rate{48000};
        u32 channels{2};
        float volume{1.0f};
        float mix_volume[2]{1.0f, 1.0f};
        vaddr_t wave_buffer_addr{0};
        size_t wave_buffer_size{0};
        size_t play_offset{0};
    };

    static constexpr size_t kMaxVoices = 32;
    void SetVoice(size_t index, const AudioVoice& voice);
    [[nodiscard]] const AudioVoice& GetVoice(size_t index) const noexcept;

private:
    std::shared_ptr<audio::IAudioBackend> backend_;
    u32 sample_rate_{48000};
    u32 sample_count_{160};
    u32 mix_buffer_count_{24};
    std::atomic<bool> running_{false};
    std::atomic<u64> frames_rendered_{0};
    std::shared_ptr<KEvent> system_event_;
    std::array<AudioVoice, kMaxVoices> voices_{};
    mutable std::mutex voice_mutex_;
};

class AudrenManagerService final : public IIpcService {
public:
    explicit AudrenManagerService(std::shared_ptr<audio::IAudioBackend> backend);
    ~AudrenManagerService() override = default;

    enum : u32 {
        OpenAudioRenderer = 0x0,
        GetAudioDeviceService = 0x1,
        OpenAudioRendererAuto = 0x2,
        GetAudioDeviceServiceWithRevisionInfo = 0x4,
    };

    u32 HandleRequest(const IpcContext& ctx, const IpcRequestReader& request,
                      IpcReplyWriter& reply, u32 x_id) override;

    [[nodiscard]] std::shared_ptr<audio::IAudioBackend> GetBackend() const noexcept { return backend_; }

private:
    std::shared_ptr<audio::IAudioBackend> backend_;
};

} // namespace nemu::core::kernel::ipc
