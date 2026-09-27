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

                    if (voice.format == audio::AudioFormat::Adpcm) {
                        const size_t frame_bytes = voice.channels * audio::adpcm::BytesPerFrame;
                        for (u32 i = 0; i < sample_count_; ++i) {
                            const size_t frame_index = (voice.play_offset / audio::adpcm::SamplesPerFrame);
                            const size_t sample_in_frame = (voice.play_offset % audio::adpcm::SamplesPerFrame);
                            const size_t frame_byte_offset = frame_index * frame_bytes;

                            if (frame_byte_offset + frame_bytes > voice.wave_buffer_size) {
                                voice.playing = false;
                                break;
                            }

                            s16 pcm_l = 0;
                            s16 pcm_r = 0;
                            float vol_l = voice.volume;
                            float vol_r = voice.volume;

                            if (ctx.memory) {
                                const size_t byte_pos = 1 + (sample_in_frame / 2);

                                if (voice.channels >= 6) {
                                    // 6-channel 5.1 downmixing (SMPTE: FL, FR, Center, LFE, SurroundL, SurroundR)
                                    s16 ch_samples[6]{};
                                    for (size_t c = 0; c < 6; ++c) {
                                        u8 frame[audio::adpcm::BytesPerFrame]{};
                                        (void)ctx.memory->ReadBlock(voice.wave_buffer_addr + frame_byte_offset + c * audio::adpcm::BytesPerFrame, frame, sizeof(frame));
                                        const u8 scale = frame[0] & 0x0F;
                                        const size_t coeff_idx = static_cast<size_t>((frame[0] >> 4) & 0x07);
                                        const s32 c0 = voice.adpcm_coefficients[c * 16 + coeff_idx * 2 + 0];
                                        const s32 c1 = voice.adpcm_coefficients[c * 16 + coeff_idx * 2 + 1];

                                        const u8 byte_val = frame[byte_pos];
                                        const u8 nibble = (sample_in_frame % 2 == 0) ? static_cast<u8>((byte_val >> 4) & 0x0F) : static_cast<u8>(byte_val & 0x0F);
                                        ch_samples[c] = audio::adpcm::DecodeSample(audio::adpcm::SignExtendNibble(nibble), scale, c0, c1, voice.adpcm_context[c]);
                                    }

                                    constexpr float kCoeffFL_FR = 1.0f;
                                    constexpr float kCoeffCenter = 0.596f;
                                    constexpr float kCoeffLFE = 0.354f;
                                    constexpr float kCoeffSurround = 0.707f;

                                    const float left_mix = (ch_samples[0] * kCoeffFL_FR * voice.mix_volume[0]) +
                                                           (ch_samples[2] * kCoeffCenter * voice.mix_volume[2]) +
                                                           (ch_samples[3] * kCoeffLFE * voice.mix_volume[3]) +
                                                           (ch_samples[4] * kCoeffSurround * voice.mix_volume[4]);

                                    const float right_mix = (ch_samples[1] * kCoeffFL_FR * voice.mix_volume[1]) +
                                                            (ch_samples[2] * kCoeffCenter * voice.mix_volume[2]) +
                                                            (ch_samples[3] * kCoeffLFE * voice.mix_volume[3]) +
                                                            (ch_samples[5] * kCoeffSurround * voice.mix_volume[5]);

                                    pcm_l = static_cast<s16>(std::clamp(static_cast<s32>(left_mix), -32768, 32767));
                                    pcm_r = static_cast<s16>(std::clamp(static_cast<s32>(right_mix), -32768, 32767));
                                } else {
                                    u8 frame_l[audio::adpcm::BytesPerFrame]{};
                                    (void)ctx.memory->ReadBlock(voice.wave_buffer_addr + frame_byte_offset, frame_l, sizeof(frame_l));
                                    const u8 scale_l = frame_l[0] & 0x0F;
                                    const size_t coeff_idx_l = static_cast<size_t>((frame_l[0] >> 4) & 0x07);
                                    const s32 c0_l = voice.adpcm_coefficients[coeff_idx_l * 2 + 0];
                                    const s32 c1_l = voice.adpcm_coefficients[coeff_idx_l * 2 + 1];

                                    const u8 byte_val_l = frame_l[byte_pos];
                                    const u8 nibble_l = (sample_in_frame % 2 == 0) ? static_cast<u8>((byte_val_l >> 4) & 0x0F) : static_cast<u8>(byte_val_l & 0x0F);
                                    pcm_l = audio::adpcm::DecodeSample(audio::adpcm::SignExtendNibble(nibble_l), scale_l, c0_l, c1_l, voice.adpcm_context[0]);

                                    if (voice.channels > 1) {
                                        u8 frame_r[audio::adpcm::BytesPerFrame]{};
                                        (void)ctx.memory->ReadBlock(voice.wave_buffer_addr + frame_byte_offset + audio::adpcm::BytesPerFrame, frame_r, sizeof(frame_r));
                                        const u8 scale_r = frame_r[0] & 0x0F;
                                        const size_t coeff_idx_r = static_cast<size_t>((frame_r[0] >> 4) & 0x07);
                                        const s32 c0_r = voice.adpcm_coefficients[coeff_idx_r * 2 + 0];
                                        const s32 c1_r = voice.adpcm_coefficients[coeff_idx_r * 2 + 1];

                                        const u8 byte_val_r = frame_r[byte_pos];
                                        const u8 nibble_r = (sample_in_frame % 2 == 0) ? static_cast<u8>((byte_val_r >> 4) & 0x0F) : static_cast<u8>(byte_val_r & 0x0F);
                                        pcm_r = audio::adpcm::DecodeSample(audio::adpcm::SignExtendNibble(nibble_r), scale_r, c0_r, c1_r, voice.adpcm_context[1]);
                                    } else {
                                        pcm_r = pcm_l;
                                    }
                                    vol_l *= voice.mix_volume[0];
                                    vol_r *= voice.mix_volume[1];
                                }
                            }
                            voice.play_offset++;

                            const s32 mixed_l = frames[i].left + static_cast<s32>(pcm_l * vol_l);
                            const s32 mixed_r = frames[i].right + static_cast<s32>(pcm_r * vol_r);
                            frames[i].left = static_cast<s16>(std::clamp(mixed_l, -32768, 32767));
                            frames[i].right = static_cast<s16>(std::clamp(mixed_r, -32768, 32767));
                        }
                    } else {
                        const size_t bytes_per_frame = voice.channels * sizeof(s16);
                        for (u32 i = 0; i < sample_count_; ++i) {
                            if (voice.play_offset + bytes_per_frame > voice.wave_buffer_size) {
                                voice.playing = false;
                                break;
                            }

                            s16 pcm_l = 0;
                            s16 pcm_r = 0;
                            float vol_l = voice.volume;
                            float vol_r = voice.volume;

                            if (ctx.memory) {
                                if (voice.channels >= 6) {
                                    // 6-channel 5.1 downmixing (SMPTE: FL, FR, Center, LFE, SurroundL, SurroundR)
                                    s16 ch[6]{};
                                    (void)ctx.memory->ReadBlock(voice.wave_buffer_addr + voice.play_offset, ch, sizeof(ch));
                                    const float fl  = static_cast<float>(ch[0]);
                                    const float fr  = static_cast<float>(ch[1]);
                                    const float c   = static_cast<float>(ch[2]);
                                    const float lfe = static_cast<float>(ch[3]);
                                    const float sl  = static_cast<float>(ch[4]);
                                    const float sr  = static_cast<float>(ch[5]);

                                    constexpr float kCoeffFL_FR = 1.0f;
                                    constexpr float kCoeffCenter = 0.596f;
                                    constexpr float kCoeffLFE = 0.354f;
                                    constexpr float kCoeffSurround = 0.707f;

                                    const float left_mix = (fl * kCoeffFL_FR * voice.mix_volume[0]) +
                                                           (c  * kCoeffCenter * voice.mix_volume[2]) +
                                                           (lfe * kCoeffLFE * voice.mix_volume[3]) +
                                                           (sl * kCoeffSurround * voice.mix_volume[4]);

                                    const float right_mix = (fr * kCoeffFL_FR * voice.mix_volume[1]) +
                                                            (c  * kCoeffCenter * voice.mix_volume[2]) +
                                                            (lfe * kCoeffLFE * voice.mix_volume[3]) +
                                                            (sr * kCoeffSurround * voice.mix_volume[5]);

                                    pcm_l = static_cast<s16>(std::clamp(static_cast<s32>(left_mix), -32768, 32767));
                                    pcm_r = static_cast<s16>(std::clamp(static_cast<s32>(right_mix), -32768, 32767));
                                } else {
                                    (void)ctx.memory->ReadBlock(voice.wave_buffer_addr + voice.play_offset, &pcm_l, sizeof(s16));
                                    if (voice.channels > 1) {
                                        (void)ctx.memory->ReadBlock(voice.wave_buffer_addr + voice.play_offset + sizeof(s16), &pcm_r, sizeof(s16));
                                    } else {
                                        pcm_r = pcm_l;
                                    }
                                    vol_l *= voice.mix_volume[0];
                                    vol_r *= voice.mix_volume[1];
                                }
                            }
                            voice.play_offset += bytes_per_frame;

                            const s32 mixed_l = frames[i].left + static_cast<s32>(pcm_l * vol_l);
                            const s32 mixed_r = frames[i].right + static_cast<s32>(pcm_r * vol_r);
                            frames[i].left = static_cast<s16>(std::clamp(mixed_l, -32768, 32767));
                            frames[i].right = static_cast<s16>(std::clamp(mixed_r, -32768, 32767));
                        }
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
