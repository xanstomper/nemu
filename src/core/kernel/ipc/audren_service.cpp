#include "audren_service.hpp"
#include "core/kernel/k_handle_table.hpp"
#include "core/kernel/ipc/ipc_service.hpp"
#include "core/memory/virtual_memory.hpp"
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

void AudioRendererService::SetVoice(size_t index, const AudioVoice& voice) {
    std::lock_guard<std::mutex> lock(voice_mutex_);
    if (index < kMaxVoices) {
        voices_[index] = voice;
    }
}

const AudioRendererService::AudioVoice& AudioRendererService::GetVoice(size_t index) const noexcept {
    static const AudioVoice s_empty{};
    if (index < kMaxVoices) {
        return voices_[index];
    }
    return s_empty;
}

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
                std::vector<audio::StereoFrame16> frames(sample_count_, audio::StereoFrame16{0, 0});

                std::lock_guard<std::mutex> lock(voice_mutex_);
                for (size_t v = 0; v < kMaxVoices; ++v) {
                    auto& voice = voices_[v];
                    if (!voice.active || !voice.playing || voice.wave_buffer_addr == 0 || voice.wave_buffer_size == 0) {
                        continue;
                    }

                    const size_t bytes_per_frame = voice.channels * sizeof(s16);
                    for (u32 i = 0; i < sample_count_; ++i) {
                        if (voice.play_offset + bytes_per_frame > voice.wave_buffer_size) {
                            voice.playing = false;
                            break;
                        }

                        s16 pcm_l = 0;
                        s16 pcm_r = 0;
                        if (ctx.memory) {
                            (void)ctx.memory->ReadBlock(voice.wave_buffer_addr + voice.play_offset, &pcm_l, sizeof(s16));
                            if (voice.channels > 1) {
                                (void)ctx.memory->ReadBlock(voice.wave_buffer_addr + voice.play_offset + sizeof(s16), &pcm_r, sizeof(s16));
                            } else {
                                pcm_r = pcm_l;
                            }
                        }
                        voice.play_offset += bytes_per_frame;

                        const float vol_l = voice.volume * voice.mix_volume[0];
                        const float vol_r = voice.volume * voice.mix_volume[1];
                        const s32 mixed_l = frames[i].left + static_cast<s32>(pcm_l * vol_l);
                        const s32 mixed_r = frames[i].right + static_cast<s32>(pcm_r * vol_r);
                        frames[i].left = static_cast<s16>(std::clamp(mixed_l, -32768, 32767));
                        frames[i].right = static_cast<s16>(std::clamp(mixed_r, -32768, 32767));
                    }
                }

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
