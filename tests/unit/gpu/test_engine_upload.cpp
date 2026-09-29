// Differential test: engine_upload (inline-to-memory) — ported from yuzu/citron
// engine_upload semantics. Verifies the LaunchDma/InlineData path writes
// byte-exact data into guest memory (the path games use for const buffers).
#include "core/gpu/maxwell_3d.hpp"
#include "core/gpu/gmmu.hpp"
#include "core/types.hpp"
#include <iostream>
#include <cstdlib>
#include <array>
#include <cstring>

#define UP_ASSERT(cond, msg)                                                    \
    do {                                                                        \
        if (!(cond)) {                                                          \
            std::cerr << "Assertion failed: " #cond " (" << (msg) << ") at "    \
                      << __FILE__ << ":" << __LINE__ << std::endl;              \
            std::exit(1);                                                       \
        }                                                                       \
    } while (0)

using namespace nemu;
using namespace nemu::core;
using namespace nemu::core::gpu;

int main() {
    std::cout << "[Test: engine_upload inline-to-memory (LaunchDma/InlineData)]" << std::endl;

    auto gmmu = std::make_shared<GpuMemoryManager>();
    constexpr u64 kDst = 0x0040000000ULL;
    constexpr u64 kMapSize = 0x10000ULL;
    static std::array<u8, kMapSize> backing{};   // host backing for the GPU region
    UP_ASSERT(gmmu->Map(kDst, kMapSize, backing.data()), "gmmu map");

    Maxwell3D m3d(nullptr);
    m3d.SetGpuMemory(gmmu);

    // --- Case 1: single-word InlineData write (mode-2-free path) ---
    // Regs: line_length=4, line_count=1, dst=0x0040010000
    m3d.ProcessMethod(MaxwellMethod::Upload + 0, 4);        // 0x60 line_length_in
    m3d.ProcessMethod(MaxwellMethod::Upload + 1, 1);        // 0x61 line_count
    m3d.ProcessMethod(MaxwellMethod::Upload + 2, static_cast<u32>((kDst + 0x008000) >> 32));        // 0x62 dst addr high
    m3d.ProcessMethod(MaxwellMethod::Upload + 3, static_cast<u32>((kDst + 0x008000) & 0xFFFFFFFF)); // 0x63 dst addr low (mid-region)
    m3d.ProcessMethod(MaxwellMethod::LaunchDma, 1);         // pitch layout
    m3d.ProcessMethod(MaxwellMethod::InlineData, 0xDEADBEEF);

    u32 got = 0;
    UP_ASSERT(gmmu->Read(kDst + 0x008000, &got, 4) == 4, "read uploaded word");
    UP_ASSERT(got == 0xDEADBEEF, "single-word upload byte-exact");
    std::cout << "  single-word InlineData upload: OK" << std::endl;

    // --- Case 2: mode-2 pushbuffer stream (16-word payload) ---
    constexpr u32 kWords = 16;
    std::array<u32, kWords> payload{};
    for (u32 i = 0; i < kWords; ++i) payload[i] = 0x01000000u * (i + 1);

    m3d.ProcessMethod(MaxwellMethod::Upload + 0, kWords * 4); // 64 bytes
    m3d.ProcessMethod(MaxwellMethod::Upload + 1, 1);
    m3d.ProcessMethod(MaxwellMethod::Upload + 2, static_cast<u32>((kDst + 0x009000) >> 32));
    m3d.ProcessMethod(MaxwellMethod::Upload + 3, static_cast<u32>((kDst + 0x009000) & 0xFFFFFFFF));
    // Build the mode-2 inline pushbuffer segment: header + payload words.
    std::array<u32, kWords + 1> segment{};
    segment[0] = (2u << 29) | (kWords << 16) | 0x006D; // InlineData, non-inc
    for (u32 i = 0; i < kWords; ++i) segment[i + 1] = payload[i];
    m3d.SubmitPushbuffer(segment);

    std::array<u32, kWords> back{};
    UP_ASSERT(gmmu->Read(kDst + 0x009000, back.data(), sizeof(back)) == sizeof(back),
              "read uploaded block");
    UP_ASSERT(back == payload, "mode-2 stream upload byte-exact");
    std::cout << "  mode-2 pushbuffer stream (" << kWords << " words): OK" << std::endl;

    // --- Case 3: upload landed in the middle of the mapped region (no clobber) ---
    u32 untouched = 0;
    UP_ASSERT(gmmu->Read(kDst + 0x00B000, &untouched, 4) == 4 && untouched == 0,
              "regions outside the upload stay untouched");
    std::cout << "  no adjacent-region clobber: OK" << std::endl;

    std::cout << "[Test: engine_upload PASSED]" << std::endl;
    return 0;
}
