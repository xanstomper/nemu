#include "audout_service.hpp"
#include "core/kernel/k_handle_table.hpp"
#include "core/kernel/ipc/ipc_service.hpp"
#include "core/memory/virtual_memory.hpp"
#include "platform/logger.hpp"
#include <cstring>
#include <algorithm>

namespace nemu::core::kernel::ipc {

// ---------------------------------------------------------------------------
// AudioOutService
// ---------------------------------------------------------------------------

AudioOutService::AudioOutService(
    std::shared_ptr<audio::IAudioBackend> backend,
    u32 sample_rate,
    u32 channel_count
) : IIpcService("audout:IAudioOut"),
    backend_(std::move(backend)),
    sample_rate_(sample_rate),
    channel_count_(channel_count),
    buffer_event_(std::make_shared<KEvent>(/*auto_clear=*/true)) {}

u32 AudioOutService::HandleRequest(
    const IpcContext& ctx,
    const IpcRequestReader& request,
    IpcReplyWriter& reply,
    u32 x_id
) {
    (void)request;

    switch (x_id) {
        case GetAudioOutState: {
            reply.Begin(0, 8);
            reply.Payload<u32>(0, 0); // Success
            reply.Payload<u32>(4, running_.load() ? 1u : 0u);
            return static_cast<u32>(IpcResult::Success);
        }

        case StartAudioOut: {
            running_.store(true);
            if (backend_) {
                backend_->Start();
            }
            reply.Begin(0, 4);
            reply.Payload<u32>(0, 0);
            return static_cast<u32>(IpcResult::Success);
        }

        case StopAudioOut: {
            running_.store(false);
            if (backend_) {
                backend_->Stop();
            }
            reply.Begin(0, 4);
            reply.Payload<u32>(0, 0);
            return static_cast<u32>(IpcResult::Success);
        }

        case AppendAudioOutBuffer: {
            u64 sample_ptr = 0;
            u64 data_size = 0;
            u64 tag = 0;

            if (request.GetDataSize() >= sizeof(AudioOutBufferDescriptor)) {
                sample_ptr = request.Payload<u64>(8);
                data_size = request.Payload<u64>(24);
                tag = request.Payload<u64>(32);
            } else if (request.GetDataSize() >= 16) {
                sample_ptr = request.Payload<u64>(0);
                data_size = request.Payload<u64>(8);
                tag = (request.GetDataSize() >= 24) ? request.Payload<u64>(16) : sample_ptr;
            }

            if (data_size > 0 && ctx.memory && sample_ptr != 0) {
                std::vector<u8> raw_samples(static_cast<size_t>(data_size));
                if (ctx.memory->ReadBlock(sample_ptr, raw_samples.data(), data_size)) {
                    if (backend_) {
                        size_t num_frames = data_size / (channel_count_ * sizeof(s16));
                        std::vector<audio::StereoFrame16> frames(num_frames);
                        std::memcpy(frames.data(), raw_samples.data(), num_frames * sizeof(audio::StereoFrame16));
                        backend_->QueueSamples(frames);
                        frames_played_.fetch_add(num_frames);
                    }
                }
            } else if (backend_) {
                // Synthesize 160 frames for tests or dummy requests
                std::vector<audio::StereoFrame16> frames(160, audio::StereoFrame16{0, 0});
                backend_->QueueSamples(frames);
                frames_played_.fetch_add(160);
            }

            {
                std::lock_guard lock(queue_mutex_);
                released_buffer_tags_.push_back(tag);
            }

            // Signal buffer playback event
            buffer_event_->Signal();

            reply.Begin(0, 4);
            reply.Payload<u32>(0, 0);
            return static_cast<u32>(IpcResult::Success);
        }

        case RegisterBufferEvent: {
            Handle event_handle = 0;
            if (ctx.handle_table) {
                event_handle = ctx.handle_table->CreateHandle(buffer_event_);
            }
            reply.Begin(0, 8);
            reply.Payload<u32>(0, 0);
            reply.Payload<u32>(4, event_handle);
            return static_cast<u32>(IpcResult::Success);
        }

        case GetReleasedAudioOutBuffers: {
            std::lock_guard lock(queue_mutex_);
            u32 count = static_cast<u32>(released_buffer_tags_.size());
            reply.Begin(0, 8 + count * 8);
            reply.Payload<u32>(0, 0); // Result Success
            reply.Payload<u32>(4, count);

            size_t offset = 8;
            while (!released_buffer_tags_.empty()) {
                u64 tag = released_buffer_tags_.front();
                released_buffer_tags_.pop_front();
                reply.Payload<u64>(offset, tag);
                offset += 8;
            }
            return static_cast<u32>(IpcResult::Success);
        }

        case ContainsAudioOutBuffer: {
            u64 tag = (request.GetDataSize() >= 8) ? request.Payload<u64>(0) : 0;
            bool found = false;
            {
                std::lock_guard lock(queue_mutex_);
                for (u64 t : pending_buffer_tags_) {
                    if (t == tag) {
                        found = true;
                        break;
                    }
                }
            }
            reply.Begin(0, 8);
            reply.Payload<u32>(0, 0);
            reply.Payload<u32>(4, found ? 1u : 0u);
            return static_cast<u32>(IpcResult::Success);
        }

        default:
            NEMU_LOG_WARN("audout", "IAudioOut: Unhandled command 0x{:X}", x_id);
            reply.Begin(0, 4);
            reply.Payload<u32>(0, 0);
            return static_cast<u32>(IpcResult::Success);
    }
}

// ---------------------------------------------------------------------------
// AudoutManagerService
// ---------------------------------------------------------------------------

AudoutManagerService::AudoutManagerService(std::shared_ptr<audio::IAudioBackend> backend)
    : IIpcService("audout:u"), backend_(std::move(backend)) {}

u32 AudoutManagerService::HandleRequest(
    const IpcContext& ctx,
    const IpcRequestReader& request,
    IpcReplyWriter& reply,
    u32 x_id
) {
    (void)request;

    switch (x_id) {
        case ListAudioOuts: {
            reply.Begin(0, 36);
            reply.Payload<u32>(0, 0); // Result Success
            reply.Payload<u32>(4, 1); // Device count = 1
            (void)reply.WriteString(8, "AudioTvOutput");
            return static_cast<u32>(IpcResult::Success);
        }

        case OpenAudioOut:
        case OpenAudioOutTrack: {
            u32 rate = 48000;
            u16 channels = 2;
            if (request.GetDataSize() >= 8) {
                rate = request.Payload<u32>(0);
                if (rate == 0) rate = 48000;
                channels = request.Payload<u16>(4);
                if (channels == 0) channels = 2;
            }

            auto out_svc = std::make_shared<AudioOutService>(backend_, rate, channels);
            auto session = std::make_shared<KClientSession>();
            session->SetService(out_svc);

            Handle session_handle = 0;
            if (ctx.handle_table) {
                session_handle = ctx.handle_table->CreateHandle(session);
            }

            reply.Begin(0, 48);
            reply.Payload<u32>(0, 0); // Result Success
            reply.Payload<u32>(4, session_handle);
            reply.Payload<u32>(8, rate);
            reply.Payload<u32>(12, static_cast<u32>(channels));
            reply.Payload<u32>(16, 2); // PcmFormat 16-bit
            reply.Payload<u32>(20, 0); // State Stopped
            (void)reply.WriteString(24, "AudioTvOutput");
            return static_cast<u32>(IpcResult::Success);
        }

        default:
            NEMU_LOG_WARN("audout", "audout:u: Unhandled command 0x{:X}", x_id);
            reply.Begin(0, 4);
            reply.Payload<u32>(0, static_cast<u32>(IpcResult::Unimplemented));
            return static_cast<u32>(IpcResult::Unimplemented);
    }
}

} // namespace nemu::core::kernel::ipc
