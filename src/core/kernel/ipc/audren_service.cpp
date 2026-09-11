#include "audren_service.hpp"
#include "core/kernel/k_handle_table.hpp"
#include "core/kernel/ipc/ipc_service.hpp"
#include "platform/logger.hpp"
#include <cstring>
#include <algorithm>

namespace nemu::core::kernel::ipc {

// ---------------------------------------------------------------------------
// AudioDeviceService
// ---------------------------------------------------------------------------

AudioDeviceService::AudioDeviceService(std::shared_ptr<audio::IAudioBackend> backend)
    : IIpcService("audren:IAudioDevice"), backend_(std::move(backend)) {}

u32 AudioDeviceService::HandleRequest(
    const IpcContext& ctx,
    const IpcRequestReader& request,
    IpcReplyWriter& reply,
    u32 x_id
) {
    (void)ctx;
    (void)request;

    switch (x_id) {
        case ListAudioDeviceName: {
            reply.Begin(0, 36);
            reply.Payload<u32>(0, 0); // Result Success
            reply.Payload<u32>(4, 1); // Device count = 1
            (void)reply.WriteString(8, "AudioTvOutput");
            return static_cast<u32>(IpcResult::Success);
        }

        case SetAudioDeviceOutputVolume: {
            u32 vol_bits = request.Payload<u32>(0);
            float v = 1.0f;
            std::memcpy(&v, &vol_bits, sizeof(float));
            volume_ = std::clamp(v, 0.0f, 1.0f);

            reply.Begin(0, 4);
            reply.Payload<u32>(0, 0);
            return static_cast<u32>(IpcResult::Success);
        }

        case GetAudioDeviceOutputVolume: {
            u32 vol_bits = 0;
            std::memcpy(&vol_bits, &volume_, sizeof(float));

            reply.Begin(0, 8);
            reply.Payload<u32>(0, 0);
            reply.Payload<u32>(4, vol_bits);
            return static_cast<u32>(IpcResult::Success);
        }

        case GetActiveAudioDeviceName: {
            reply.Begin(0, 36);
            reply.Payload<u32>(0, 0);
            (void)reply.WriteString(4, "AudioTvOutput");
            return static_cast<u32>(IpcResult::Success);
        }

        default:
            reply.Begin(0, 4);
            reply.Payload<u32>(0, 0);
            return static_cast<u32>(IpcResult::Success);
    }
}

// ---------------------------------------------------------------------------
// AudioRendererService
// ---------------------------------------------------------------------------

AudioRendererService::AudioRendererService(
    std::shared_ptr<audio::IAudioBackend> backend,
    u32 sample_rate,
    u32 sample_count
) : IIpcService("audren:IAudioRenderer"),
    backend_(std::move(backend)),
    sample_rate_(sample_rate),
    sample_count_(sample_count),
    system_event_(std::make_shared<KEvent>(/*auto_clear=*/true)) {}

u32 AudioRendererService::HandleRequest(
    const IpcContext& ctx,
    const IpcRequestReader& request,
    IpcReplyWriter& reply,
    u32 x_id
) {
    (void)request;

    switch (x_id) {
        case GetSampleRate: {
            reply.Begin(0, 8);
            reply.Payload<u32>(0, 0);
            reply.Payload<u32>(4, sample_rate_);
            return static_cast<u32>(IpcResult::Success);
        }

        case GetSampleCount: {
            reply.Begin(0, 8);
            reply.Payload<u32>(0, 0);
            reply.Payload<u32>(4, sample_count_);
            return static_cast<u32>(IpcResult::Success);
        }

        case GetMixBufferCount: {
            reply.Begin(0, 8);
            reply.Payload<u32>(0, 0);
            reply.Payload<u32>(4, mix_buffer_count_);
            return static_cast<u32>(IpcResult::Success);
        }

        case GetState: {
            reply.Begin(0, 8);
            reply.Payload<u32>(0, 0);
            reply.Payload<u32>(4, running_.load() ? 1u : 0u);
            return static_cast<u32>(IpcResult::Success);
        }

        case Start: {
            running_.store(true);
            if (backend_) {
                backend_->Start();
            }
            reply.Begin(0, 4);
            reply.Payload<u32>(0, 0);
            return static_cast<u32>(IpcResult::Success);
        }

        case Stop: {
            running_.store(false);
            if (backend_) {
                backend_->Stop();
            }
            reply.Begin(0, 4);
            reply.Payload<u32>(0, 0);
            return static_cast<u32>(IpcResult::Success);
        }

        case QuerySystemEvent: {
            Handle event_handle = 0;
            if (ctx.handle_table) {
                event_handle = ctx.handle_table->CreateHandle(system_event_);
            }
            reply.Begin(0, 8);
            reply.Payload<u32>(0, 0);
            reply.Payload<u32>(4, event_handle);
            return static_cast<u32>(IpcResult::Success);
        }

        case RequestUpdate: {
            if (running_.load() && backend_) {
                // Synthesize stereo frame buffer and queue to audio backend
                std::vector<audio::StereoFrame16> frames(sample_count_, audio::StereoFrame16{0, 0});
                backend_->QueueSamples(frames);
                frames_rendered_.fetch_add(sample_count_);
            }

            system_event_->Signal();

            reply.Begin(0, 16);
            reply.Payload<u32>(0, 0); // Result Success
            reply.Payload<u64>(4, frames_rendered_.load());
            return static_cast<u32>(IpcResult::Success);
        }

        default:
            NEMU_LOG_WARN("audren", "IAudioRenderer: Unhandled command 0x{:X}", x_id);
            reply.Begin(0, 4);
            reply.Payload<u32>(0, static_cast<u32>(IpcResult::Unimplemented));
            return static_cast<u32>(IpcResult::Unimplemented);
    }
}

// ---------------------------------------------------------------------------
// AudrenManagerService
// ---------------------------------------------------------------------------

AudrenManagerService::AudrenManagerService(std::shared_ptr<audio::IAudioBackend> backend)
    : IIpcService("audren:u"), backend_(std::move(backend)) {}

u32 AudrenManagerService::HandleRequest(
    const IpcContext& ctx,
    const IpcRequestReader& request,
    IpcReplyWriter& reply,
    u32 x_id
) {
    (void)request;

    switch (x_id) {
        case OpenAudioRenderer:
        case OpenAudioRendererAuto: {
            u32 rate = request.Payload<u32>(0);
            if (rate == 0) rate = 48000;
            u32 count = request.Payload<u32>(4);
            if (count == 0) count = 160;

            auto renderer_svc = std::make_shared<AudioRendererService>(backend_, rate, count);
            auto session = std::make_shared<KClientSession>();
            session->SetService(renderer_svc);

            Handle session_handle = 0;
            if (ctx.handle_table) {
                session_handle = ctx.handle_table->CreateHandle(session);
            }

            reply.Begin(0, 8);
            reply.Payload<u32>(0, 0);
            reply.Payload<u32>(4, session_handle);
            return static_cast<u32>(IpcResult::Success);
        }

        case GetAudioDeviceService:
        case GetAudioDeviceServiceWithRevisionInfo: {
            auto device_svc = std::make_shared<AudioDeviceService>(backend_);
            auto session = std::make_shared<KClientSession>();
            session->SetService(device_svc);

            Handle session_handle = 0;
            if (ctx.handle_table) {
                session_handle = ctx.handle_table->CreateHandle(session);
            }

            reply.Begin(0, 8);
            reply.Payload<u32>(0, 0);
            reply.Payload<u32>(4, session_handle);
            return static_cast<u32>(IpcResult::Success);
        }

        default:
            NEMU_LOG_WARN("audren", "audren:u: Unhandled command 0x{:X}", x_id);
            reply.Begin(0, 4);
            reply.Payload<u32>(0, static_cast<u32>(IpcResult::Unimplemented));
            return static_cast<u32>(IpcResult::Unimplemented);
    }
}

} // namespace nemu::core::kernel::ipc
