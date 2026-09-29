#include "audin_service.hpp"
#include "core/kernel/k_handle_table.hpp"
#include "core/kernel/ipc/ipc_service.hpp"
#include "core/memory/virtual_memory.hpp"
#include "platform/logger.hpp"
#include <cstring>
#include <algorithm>

namespace nemu::core::kernel::ipc {

// ---------------------------------------------------------------------------
// AudioInService
// ---------------------------------------------------------------------------

AudioInService::AudioInService(
    std::shared_ptr<audio::audin::AudioInputManager> manager,
    u32 sample_rate,
    u32 channel_count
) : IIpcService("audin:IAudioIn"),
    manager_(std::move(manager)),
    sample_rate_(sample_rate > 0 ? sample_rate : 48000),
    channel_count_(channel_count > 0 ? channel_count : 1),
    buffer_event_(std::make_shared<KEvent>(/*auto_clear=*/true)) {
    if (!manager_) {
        manager_ = std::make_shared<audio::audin::AudioInputManager>();
    }
}

u32 AudioInService::HandleRequest(
    const IpcContext& ctx,
    const IpcRequestReader& request,
    IpcReplyWriter& reply,
    u32 x_id
) {
    (void)request;

    switch (x_id) {
        case GetAudioInState: {
            reply.Begin(0, 8);
            reply.Payload<u32>(0, 0); // Result Success
            reply.Payload<u32>(4, running_.load() ? 0u : 1u); // 0 = Started, 1 = Stopped
            return static_cast<u32>(IpcResult::Success);
        }

        case StartAudioIn: {
            running_.store(true);
            if (manager_) {
                manager_->OpenStream("Default", sample_rate_, channel_count_);
            }
            reply.Begin(0, 4);
            reply.Payload<u32>(0, 0);
            return static_cast<u32>(IpcResult::Success);
        }

        case StopAudioIn: {
            running_.store(false);
            if (manager_) {
                manager_->CloseStream();
            }
            reply.Begin(0, 4);
            reply.Payload<u32>(0, 0);
            return static_cast<u32>(IpcResult::Success);
        }

        case AppendAudioInBuffer:
        case AppendAudioInBufferAuto: {
            u64 sample_ptr = 0;
            u64 data_size = 0;
            u64 tag = 0;

            if (request.GetDataSize() >= sizeof(AudioInBufferDescriptor)) {
                sample_ptr = request.Payload<u64>(8);
                data_size = request.Payload<u64>(24);
                tag = request.Payload<u64>(32);
            } else if (request.GetDataSize() >= 16) {
                sample_ptr = request.Payload<u64>(0);
                data_size = request.Payload<u64>(8);
                tag = (request.GetDataSize() >= 24) ? request.Payload<u64>(16) : sample_ptr;
            }

            if (data_size > 0 && ctx.memory && sample_ptr != 0) {
                const size_t num_samples = data_size / sizeof(s16);
                std::vector<s16> samples(num_samples, 0);
                if (manager_ && running_.load()) {
                    manager_->ReadSamples(samples);
                }
                ctx.memory->WriteBlock(sample_ptr, samples.data(), data_size);
                frames_captured_.fetch_add(num_samples / channel_count_);
            }

            {
                std::lock_guard lock(queue_mutex_);
                released_buffer_tags_.push_back(tag);
            }

            buffer_event_->Signal();

            reply.Begin(0, 4);
            reply.Payload<u32>(0, 0); // Success
            return static_cast<u32>(IpcResult::Success);
        }

        case RegisterBufferEvent: {
            Handle event_handle = 0;
            if (ctx.handle_table && buffer_event_) {
                event_handle = ctx.handle_table->CreateHandle(buffer_event_);
            }
            reply.Begin(0, 8);
            reply.Payload<u32>(0, 0);
            reply.Payload<u32>(4, event_handle);
            return static_cast<u32>(IpcResult::Success);
        }

        case GetReleasedAudioInBuffers:
        case GetReleasedAudioInBuffersAuto: {
            std::vector<u64> tags;
            {
                std::lock_guard lock(queue_mutex_);
                while (!released_buffer_tags_.empty()) {
                    tags.push_back(released_buffer_tags_.front());
                    released_buffer_tags_.pop_front();
                }
            }

            reply.Begin(0, 8 + static_cast<u32>(tags.size() * sizeof(u64)));
            reply.Payload<u32>(0, 0); // Result Success
            reply.Payload<u32>(4, static_cast<u32>(tags.size()));
            for (size_t i = 0; i < tags.size(); ++i) {
                reply.Payload<u64>(8 + static_cast<u32>(i * sizeof(u64)), tags[i]);
            }
            return static_cast<u32>(IpcResult::Success);
        }

        case ContainsAudioInBuffer: {
            u64 tag = 0;
            if (request.GetDataSize() >= 8) {
                tag = request.Payload<u64>(0);
            }
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

        case FlushAudioInBuffers: {
            {
                std::lock_guard lock(queue_mutex_);
                while (!pending_buffer_tags_.empty()) {
                    released_buffer_tags_.push_back(pending_buffer_tags_.back());
                    pending_buffer_tags_.pop_back();
                }
            }
            buffer_event_->Signal();
            reply.Begin(0, 8);
            reply.Payload<u32>(0, 0);
            reply.Payload<u32>(4, 1);
            return static_cast<u32>(IpcResult::Success);
        }

        default:
            NEMU_LOG_WARN("audin", "IAudioIn: Unhandled command 0x{:X}", x_id);
            reply.Begin(0, 4);
            reply.Payload<u32>(0, 0);
            return static_cast<u32>(IpcResult::Success);
    }
}

// ---------------------------------------------------------------------------
// AudinManagerService
// ---------------------------------------------------------------------------

AudinManagerService::AudinManagerService(const std::string& service_name,
                                         std::shared_ptr<audio::audin::AudioInputManager> manager)
    : IIpcService(service_name), manager_(std::move(manager)) {
    if (!manager_) {
        manager_ = std::make_shared<audio::audin::AudioInputManager>();
    }
}

u32 AudinManagerService::HandleRequest(
    const IpcContext& ctx,
    const IpcRequestReader& request,
    IpcReplyWriter& reply,
    u32 x_id
) {
    (void)request;

    switch (x_id) {
        case ListAudioIns:
        case ListAudioInsAuto:
        case ListAudioInsAutoFiltered: {
            auto devs = manager_ ? manager_->EnumerateDevices() : std::vector<audio::audin::AudioInputDevice>{};
            reply.Begin(0, 36);
            reply.Payload<u32>(0, 0); // Result Success
            reply.Payload<u32>(4, static_cast<u32>(devs.size()));
            if (!devs.empty()) {
                (void)reply.WriteString(8, devs[0].name);
            } else {
                (void)reply.WriteString(8, "AudioBuiltInMic");
            }
            return static_cast<u32>(IpcResult::Success);
        }

        case OpenAudioIn:
        case OpenAudioInAuto:
        case OpenAudioInProtocolSpecified: {
            u32 rate = 48000;
            u16 channels = 1;
            if (request.GetDataSize() >= 8) {
                rate = request.Payload<u32>(0);
                if (rate == 0) rate = 48000;
                channels = request.Payload<u16>(4);
                if (channels == 0) channels = 1;
            }

            auto in_svc = std::make_shared<AudioInService>(manager_, rate, channels);
            auto session = std::make_shared<KClientSession>();
            session->SetService(in_svc);

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
            reply.Payload<u32>(20, 1); // State Stopped
            (void)reply.WriteString(24, "AudioBuiltInMic");
            return static_cast<u32>(IpcResult::Success);
        }

        default:
            NEMU_LOG_WARN("audin", "audin:u: Unhandled command 0x{:X}", x_id);
            reply.Begin(0, 4);
            reply.Payload<u32>(0, static_cast<u32>(IpcResult::Unimplemented));
            return static_cast<u32>(IpcResult::Unimplemented);
    }
}

} // namespace nemu::core::kernel::ipc
