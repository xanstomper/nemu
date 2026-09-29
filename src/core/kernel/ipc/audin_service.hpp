#pragma once

#include "ipc_service.hpp"
#include "core/audio/audin/audio_input.hpp"
#include "core/kernel/k_event.hpp"
#include <memory>
#include <vector>
#include <deque>
#include <atomic>
#include <mutex>

namespace nemu::core::kernel::ipc {

#pragma pack(push, 1)
struct AudioInBufferDescriptor {
    u64 next_ptr{0};
    u64 sample_data_ptr{0};
    u64 buffer_capacity{0};
    u64 data_size{0};
    u64 tag{0};
};
#pragma pack(pop)

class AudioInService final : public IIpcService {
public:
    explicit AudioInService(std::shared_ptr<audio::audin::AudioInputManager> manager, u32 sample_rate, u32 channel_count);
    ~AudioInService() override = default;

    enum : u32 {
        GetAudioInState = 0x0,
        StartAudioIn = 0x1,
        StopAudioIn = 0x2,
        AppendAudioInBuffer = 0x3,
        RegisterBufferEvent = 0x4,
        GetReleasedAudioInBuffers = 0x5,
        ContainsAudioInBuffer = 0x6,
        AppendAudioInBufferAuto = 0x7,
        GetReleasedAudioInBuffersAuto = 0x8,
        FlushAudioInBuffers = 0x9,
    };

    u32 HandleRequest(const IpcContext& ctx, const IpcRequestReader& request,
                      IpcReplyWriter& reply, u32 x_id) override;

    [[nodiscard]] bool IsRunning() const noexcept { return running_.load(); }
    [[nodiscard]] u64 GetTotalFramesCaptured() const noexcept { return frames_captured_.load(); }

private:
    std::shared_ptr<audio::audin::AudioInputManager> manager_;
    u32 sample_rate_{48000};
    u32 channel_count_{1};
    std::atomic<bool> running_{false};
    std::atomic<u64> frames_captured_{0};
    std::shared_ptr<KEvent> buffer_event_;

    mutable std::mutex queue_mutex_;
    std::deque<u64> released_buffer_tags_;
    std::vector<u64> pending_buffer_tags_;
};

class AudinManagerService final : public IIpcService {
public:
    explicit AudinManagerService(const std::string& service_name = "audin:u",
                                 std::shared_ptr<audio::audin::AudioInputManager> manager = nullptr);
    ~AudinManagerService() override = default;

    enum : u32 {
        ListAudioIns = 0x0,
        OpenAudioIn = 0x1,
        ListAudioInsAuto = 0x2,
        OpenAudioInAuto = 0x3,
        ListAudioInsAutoFiltered = 0x4,
        OpenAudioInProtocolSpecified = 0x5,
    };

    u32 HandleRequest(const IpcContext& ctx, const IpcRequestReader& request,
                      IpcReplyWriter& reply, u32 x_id) override;

private:
    std::shared_ptr<audio::audin::AudioInputManager> manager_;
};

} // namespace nemu::core::kernel::ipc
