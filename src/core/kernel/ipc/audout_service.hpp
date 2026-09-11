#pragma once

#include "ipc_service.hpp"
#include "core/audio/audio_interface.hpp"
#include "core/kernel/k_event.hpp"
#include <memory>
#include <vector>
#include <deque>
#include <atomic>
#include <mutex>

namespace nemu::core::kernel::ipc {

#pragma pack(push, 1)
struct AudioOutBufferDescriptor {
    u64 next_ptr{0};
    u64 sample_data_ptr{0};
    u64 buffer_capacity{0};
    u64 data_size{0};
    u64 tag{0};
};
#pragma pack(pop)

class AudioOutService final : public IIpcService {
public:
    explicit AudioOutService(std::shared_ptr<audio::IAudioBackend> backend, u32 sample_rate, u32 channel_count);
    ~AudioOutService() override = default;

    enum : u32 {
        GetAudioOutState = 0x0,
        StartAudioOut = 0x1,
        StopAudioOut = 0x2,
        AppendAudioOutBuffer = 0x3,
        RegisterBufferEvent = 0x4,
        GetReleasedAudioOutBuffers = 0x5,
        ContainsAudioOutBuffer = 0x6,
    };

    u32 HandleRequest(const IpcContext& ctx, const IpcRequestReader& request,
                      IpcReplyWriter& reply, u32 x_id) override;

    [[nodiscard]] bool IsRunning() const noexcept { return running_.load(); }
    [[nodiscard]] u64 GetTotalFramesPlayed() const noexcept { return frames_played_.load(); }

private:
    std::shared_ptr<audio::IAudioBackend> backend_;
    u32 sample_rate_{48000};
    u32 channel_count_{2};
    std::atomic<bool> running_{false};
    std::atomic<u64> frames_played_{0};
    std::shared_ptr<KEvent> buffer_event_;

    mutable std::mutex queue_mutex_;
    std::deque<u64> released_buffer_tags_;
    std::vector<u64> pending_buffer_tags_;
};

class AudoutManagerService final : public IIpcService {
public:
    explicit AudoutManagerService(std::shared_ptr<audio::IAudioBackend> backend);
    ~AudoutManagerService() override = default;

    enum : u32 {
        ListAudioOuts = 0x0,
        OpenAudioOut = 0x1,
        OpenAudioOutTrack = 0x2,
    };

    u32 HandleRequest(const IpcContext& ctx, const IpcRequestReader& request,
                      IpcReplyWriter& reply, u32 x_id) override;

    [[nodiscard]] std::shared_ptr<audio::IAudioBackend> GetBackend() const noexcept { return backend_; }

private:
    std::shared_ptr<audio::IAudioBackend> backend_;
};

} // namespace nemu::core::kernel::ipc
