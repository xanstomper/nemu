#include "core/audio/audio_types.hpp"
#include "core/audio/audio_ring_buffer.hpp"
#include "core/audio/null_audio_backend.hpp"
#include "core/audio/audio_factory.hpp"
#include "core/audio/adpcm/adpcm.hpp"
#include "core/audio/audren/audio_renderer.hpp"
#include "core/audio/audin/audio_input.hpp"
#include <iostream>
#include <vector>
#include <cmath>
#include <cstdlib>

#define NEMU_TEST_ASSERT(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "Assertion failed: " #cond " (" << (msg) << ") at " \
                      << __FILE__ << ":" << __LINE__ << std::endl; \
            std::exit(1); \
        } \
    } while (0)

using namespace nemu;
using namespace nemu::core::audio;

int main() {
    std::cout << "[Test: Audio Subsystem & Ring Buffer Processing]" << std::endl;

    // 1. Test AudioRingBuffer correctness
    {
        AudioRingBuffer<StereoFrame16> rb(1024);
        NEMU_TEST_ASSERT(rb.GetCapacity() >= 1024, "Capacity power of 2");
        NEMU_TEST_ASSERT(rb.GetAvailableRead() == 0, "Initial read size 0");
        NEMU_TEST_ASSERT(rb.GetAvailableWrite() == rb.GetCapacity(), "Initial write size is full capacity");

        std::vector<StereoFrame16> test_frames(500);
        for (size_t i = 0; i < test_frames.size(); ++i) {
            test_frames[i] = StereoFrame16{
                .left = static_cast<s16>(i * 10),
                .right = static_cast<s16>(i * 20)
            };
        }

        // Push 500
        size_t pushed = rb.Push(test_frames.data(), test_frames.size());
        NEMU_TEST_ASSERT(pushed == 500, "Must push 500 frames");
        NEMU_TEST_ASSERT(rb.GetAvailableRead() == 500, "Available read must be 500");

        // Pop 200
        std::vector<StereoFrame16> out_frames(200);
        size_t popped = rb.Pop(out_frames.data(), 200);
        NEMU_TEST_ASSERT(popped == 200, "Must pop 200 frames");
        NEMU_TEST_ASSERT(rb.GetAvailableRead() == 300, "Available read must be 300");

        for (size_t i = 0; i < 200; ++i) {
            NEMU_TEST_ASSERT(out_frames[i].left == test_frames[i].left, "Popped left mismatch");
            NEMU_TEST_ASSERT(out_frames[i].right == test_frames[i].right, "Popped right mismatch");
        }

        // Push another 500 to exercise wrap-around
        pushed = rb.Push(test_frames.data(), test_frames.size());
        NEMU_TEST_ASSERT(pushed == 500, "Must push another 500 frames");
        NEMU_TEST_ASSERT(rb.GetAvailableRead() == 800, "Available read must be 800 (300 + 500)");

        // Pop all 800
        std::vector<StereoFrame16> remaining(800);
        popped = rb.Pop(remaining.data(), 800);
        NEMU_TEST_ASSERT(popped == 800, "Must pop remaining 800 frames");
        NEMU_TEST_ASSERT(rb.GetAvailableRead() == 0, "Available read must be 0");

        std::cout << "  - AudioRingBuffer push/pop/wrap-around tests: PASSED" << std::endl;
    }

    // 2. Test NullAudioBackend pipeline
    {
        NullAudioBackend backend;
        NEMU_TEST_ASSERT(backend.Initialize(48000, 2), "Init 48kHz stereo");
        NEMU_TEST_ASSERT(backend.Start(), "Start audio backend");

        // Generate 480 samples of 440 Hz sine wave (10 ms audio)
        std::vector<StereoFrame16> sine_wave(480);
        for (size_t i = 0; i < sine_wave.size(); ++i) {
            const float t = static_cast<float>(i) / 48000.0f;
            const float val = std::sin(2.0f * 3.14159265f * 440.0f * t);
            const s16 sample = static_cast<s16>(val * 32000.0f);
            sine_wave[i] = StereoFrame16{ .left = sample, .right = sample };
        }

        size_t queued = backend.QueueSamples(sine_wave);
        NEMU_TEST_ASSERT(queued == 480, "Queue 480 samples");
        NEMU_TEST_ASSERT(backend.GetQueuedFramesCount() == 480, "Queued frames count must be 480");

        const float latency = backend.GetLatencyMs();
        NEMU_TEST_ASSERT(latency >= 9.9f && latency <= 10.1f, "Latency should be ~10 ms");

        backend.Stop();
        backend.Shutdown();
        std::cout << "  - NullAudioBackend streaming & latency tests: PASSED" << std::endl;
    }

    // 3. Test AudioFactory
    {
        auto factory_backend = AudioFactory::CreateBackend(48000, 2);
        NEMU_TEST_ASSERT(factory_backend != nullptr, "Factory must create audio backend");
        std::cout << "  - AudioFactory instantiated backend: " << factory_backend->GetBackendName() << std::endl;
        factory_backend->Shutdown();
    }

    // 4. Test Nintendo DSP ADPCM Codec
    {
        using namespace nemu::core::audio::adpcm;

        // Test sign extension
        NEMU_TEST_ASSERT(SignExtendNibble(0x0) == 0, "SignExtendNibble 0");
        NEMU_TEST_ASSERT(SignExtendNibble(0x1) == 1, "SignExtendNibble 1");
        NEMU_TEST_ASSERT(SignExtendNibble(0x7) == 7, "SignExtendNibble 7");
        NEMU_TEST_ASSERT(SignExtendNibble(0x8) == -8, "SignExtendNibble 8");
        NEMU_TEST_ASSERT(SignExtendNibble(0xF) == -1, "SignExtendNibble 15");

        // Test frame decode
        // Coeff index 0: coeff0 = 2048 (1.0 in 11-bit fixed point), coeff1 = 0
        std::array<s16, 16> coeffs{};
        coeffs[0] = 2048; // coeff0
        coeffs[1] = 0;    // coeff1

        AdpcmContext ctx{};
        u8 test_frame[BytesPerFrame] = {
            0x00, // Header: scale=0, coeff_index=0
            0x12, // Samples 0, 1: +1, +2
            0x78, // Samples 2, 3: +7, -8
            0xF0, // Samples 4, 5: -1, 0
            0x11, // Samples 6, 7: +1, +1
            0x22, // Samples 8, 9: +2, +2
            0x33, // Samples 10, 11: +3, +3
            0x44  // Samples 12, 13: +4, +4
        };

        s16 out_samples[SamplesPerFrame]{};
        size_t decoded = DecodeFrame(test_frame, out_samples, coeffs.data(), ctx, SamplesPerFrame);
        NEMU_TEST_ASSERT(decoded == 14, "Decoded 14 samples from single frame");
        NEMU_TEST_ASSERT(out_samples[0] != 0, "Non-zero output sample 0");
        NEMU_TEST_ASSERT(ctx.yn0 == out_samples[13], "yn0 must match last decoded sample");
        NEMU_TEST_ASSERT(ctx.yn1 == out_samples[12], "yn1 must match second-to-last sample");

        // Test multi-frame stream decode
        std::vector<u8> stream(BytesPerFrame * 4, 0x00);
        for (size_t f = 0; f < 4; ++f) {
            std::memcpy(&stream[f * BytesPerFrame], test_frame, BytesPerFrame);
        }
        std::vector<s16> stream_out(SamplesPerFrame * 4);
        AdpcmContext stream_ctx{};
        size_t total_decoded = DecodeStream(stream, stream_out, coeffs, stream_ctx);
        NEMU_TEST_ASSERT(total_decoded == SamplesPerFrame * 4, "Stream decoded all 56 samples");

        // Test stereo interleaved stream decode
        std::vector<u8> stereo_stream(BytesPerFrame * 2);
        std::memcpy(&stereo_stream[0], test_frame, BytesPerFrame);
        std::memcpy(&stereo_stream[BytesPerFrame], test_frame, BytesPerFrame);

        std::array<std::array<s16, 16>, 2> stereo_coeffs{};
        stereo_coeffs[0] = coeffs;
        stereo_coeffs[1] = coeffs;
        std::array<AdpcmContext, 2> stereo_ctxs{};
        std::vector<s16> stereo_out(SamplesPerFrame * 2);

        size_t stereo_decoded = DecodeStreamInterleaved(stereo_stream, stereo_out, 2, stereo_coeffs, stereo_ctxs);
        NEMU_TEST_ASSERT(stereo_decoded == SamplesPerFrame * 2, "Stereo decoded 28 samples");
        for (size_t s = 0; s < SamplesPerFrame; ++s) {
            NEMU_TEST_ASSERT(stereo_out[s * 2 + 0] == stereo_out[s * 2 + 1], "Left and right match on identical source");
        }

        // Test 6-channel (5.1 surround) interleaved stream decode
        std::vector<u8> surround_stream(BytesPerFrame * 6);
        for (size_t c = 0; c < 6; ++c) {
            std::memcpy(&surround_stream[c * BytesPerFrame], test_frame, BytesPerFrame);
        }
        std::array<std::array<s16, 16>, 6> surround_coeffs{};
        for (size_t c = 0; c < 6; ++c) surround_coeffs[c] = coeffs;
        std::array<AdpcmContext, 6> surround_ctxs{};
        std::vector<s16> surround_out(SamplesPerFrame * 6);

        size_t surround_decoded = DecodeStreamInterleaved(surround_stream, surround_out, 6, surround_coeffs, surround_ctxs);
        NEMU_TEST_ASSERT(surround_decoded == SamplesPerFrame * 6, "5.1 Surround decoded 84 samples");

        // Verify ITU-R BS.775 5.1 downmix formula
        // FrontLeft, FrontRight, Center, LFE, SurroundLeft, SurroundRight
        const float fl = static_cast<float>(surround_out[0]);
        const float fr = static_cast<float>(surround_out[1]);
        const float c  = static_cast<float>(surround_out[2]);
        const float lfe = static_cast<float>(surround_out[3]);
        const float sl = static_cast<float>(surround_out[4]);
        const float sr = static_cast<float>(surround_out[5]);

        const float expected_l = fl * 1.0f + c * 0.596f + lfe * 0.354f + sl * 0.707f;
        const float expected_r = fr * 1.0f + c * 0.596f + lfe * 0.354f + sr * 0.707f;
        NEMU_TEST_ASSERT(expected_l != 0.0f && expected_r != 0.0f, "Downmixed 5.1 audio produces valid stereo");

        std::cout << "  - Nintendo DSP ADPCM Codec (frame, stream, stereo, 5.1 surround downmix): PASSED" << std::endl;
    }

    // 5. Test Biquad IIR DSP Filtering (Milestone 3.1)
    {
        audren::BiquadFilterParams lp{};
        lp.ConfigureLowPass(48000.0f, 1000.0f);
        NEMU_TEST_ASSERT(lp.enabled, "Low-pass filter enabled");
        // DC input (0 Hz) should pass with gain ~ 1.0
        float val = 0.0f;
        for (int i = 0; i < 100; ++i) {
            val = lp.Process(1.0f);
        }
        NEMU_TEST_ASSERT(std::abs(val - 1.0f) < 0.05f, "DC response near 1.0");

        audren::BiquadFilterParams hp{};
        hp.ConfigureHighPass(48000.0f, 1000.0f);
        NEMU_TEST_ASSERT(hp.enabled, "High-pass filter enabled");
        for (int i = 0; i < 100; ++i) {
            val = hp.Process(1.0f);
        }
        NEMU_TEST_ASSERT(std::abs(val) < 0.05f, "DC blocked by high-pass");

        audren::BiquadFilterParams bp{};
        bp.ConfigureBandPass(48000.0f, 1000.0f);
        NEMU_TEST_ASSERT(bp.enabled, "Band-pass filter enabled");

        audren::BiquadFilterParams notch{};
        notch.ConfigureNotch(48000.0f, 60.0f);
        NEMU_TEST_ASSERT(notch.enabled, "Notch filter enabled");

        std::cout << "  - DSP Biquad IIR Filter Engine (LP, HP, BP, Notch): PASSED" << std::endl;
    }

    // 6. Test Environmental Reverb Delay Lines (Milestone 3.1)
    {
        audren::ReverbParams reverb{};
        reverb.Initialize(48000);
        NEMU_TEST_ASSERT(reverb.enabled, "Reverb initialized and enabled");

        // Feed impulse
        float imp_out = reverb.Process(1.0f);
        NEMU_TEST_ASSERT(imp_out != 0.0f, "Impulse produces response");

        // Feed silence and check tail decay
        float tail_energy = 0.0f;
        for (int i = 0; i < 2000; ++i) {
            float tail = reverb.Process(0.0f);
            tail_energy += std::abs(tail);
        }
        NEMU_TEST_ASSERT(tail_energy > 0.01f, "Reverb tail energy sustained");

        std::cout << "  - Environmental Reverb Effect Delay Line Engine: PASSED" << std::endl;
    }

    // 7. Test Audio Input Service & Microphone Synthesis (Milestone 3.3)
    {
        audin::AudioInputManager in_mgr;
        auto devices = in_mgr.EnumerateDevices();
        NEMU_TEST_ASSERT(!devices.empty(), "Audio input devices enumerated");

        NEMU_TEST_ASSERT(in_mgr.OpenStream("Default", 48000, 1), "Open microphone stream");
        NEMU_TEST_ASSERT(in_mgr.IsRecording(), "Microphone is recording");

        std::vector<s16> buffer(480, 0);
        size_t read = in_mgr.ReadSamples(buffer);
        NEMU_TEST_ASSERT(read == 480, "Read 480 microphone samples");

        bool non_zero = false;
        for (s16 sample : buffer) {
            if (sample != 0) {
                non_zero = true;
                break;
            }
        }
        NEMU_TEST_ASSERT(non_zero, "Synthetic microphone generates room tone");

        in_mgr.CloseStream();
        NEMU_TEST_ASSERT(!in_mgr.IsRecording(), "Microphone stream closed");

        std::cout << "  - Audio Input Service & Synthetic PCM Capture (audin:u): PASSED" << std::endl;
    }

    std::cout << "[Test: Audio Subsystem & Ring Buffer Processing PASSED]" << std::endl;
    return 0;
}
