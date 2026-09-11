#include "core/audio/audio_types.hpp"
#include "core/audio/audio_ring_buffer.hpp"
#include "core/audio/null_audio_backend.hpp"
#include "core/audio/audio_factory.hpp"
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

    std::cout << "[Test: Audio Subsystem & Ring Buffer Processing PASSED]" << std::endl;
    return 0;
}
