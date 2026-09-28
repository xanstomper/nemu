#include "core/gpu/maxwell_dma.hpp"
#include "core/memory/virtual_memory.hpp"
#include "core/types.hpp"
#include <iostream>
#include <cstdlib>
#include <vector>
#include <cstring>

using namespace nemu;
using namespace nemu::core;
using namespace nemu::core::gpu;

#define DMA_ASSERT(...)                                                        \
    do {                                                                       \
        if (!(__VA_ARGS__)) {                                                  \
            std::cerr << "Assertion failed: " #__VA_ARGS__ << " at "           \
                      << __FILE__ << ":" << __LINE__ << std::endl;             \
            std::exit(1);                                                      \
        }                                                                      \
    } while (0)

int main() {
    std::cout << "[Test: Maxwell DMA copy engine (0xB0B7)]" << std::endl;

    memory::VirtualMemory mem;
    constexpr vaddr_t kSrc = 0x0090000000ULL;
    constexpr vaddr_t kDst = 0x0091000000ULL;
    constexpr u32 kBytes = 4096;
    DMA_ASSERT(mem.Map(kSrc, kBytes * 2, memory::MemoryPermission::All));
    DMA_ASSERT(mem.Map(kDst, kBytes * 2, memory::MemoryPermission::All));

    // Fill source with a recognizable pattern.
    std::vector<u8> src(kBytes);
    for (u32 i = 0; i < kBytes; ++i) src[i] = static_cast<u8>(i & 0xFF);
    DMA_ASSERT(mem.WriteBlock(kSrc, src.data(), src.size()));

    MaxwellDma dma(&mem);

    // --- 1D pitch->pitch copy: program the registers, then Launch. ---
    dma.CallMethod(MaxwellDma::REG_LINE_LENGTH_IN, kBytes);
    dma.CallMethod(MaxwellDma::REG_LINE_COUNT, 1);
    dma.CallMethod(MaxwellDma::REG_OFFSET_IN, static_cast<u32>(kSrc & 0xFFFFFFFFULL));
    dma.CallMethod(MaxwellDma::REG_OFFSET_IN + 1, static_cast<u32>(kSrc >> 32));
    dma.CallMethod(MaxwellDma::REG_OFFSET_OUT, static_cast<u32>(kDst & 0xFFFFFFFFULL));
    dma.CallMethod(MaxwellDma::REG_OFFSET_OUT + 1, static_cast<u32>(kDst >> 32));
    dma.CallMethod(MaxwellDma::REG_PITCH_IN, kBytes);
    dma.CallMethod(MaxwellDma::REG_PITCH_OUT, kBytes);
    // Launch: src pitch (bit2), dst pitch (bit4).
    dma.CallMethod(MaxwellDma::REG_LAUNCH, (1u << 2) | (1u << 4));

    DMA_ASSERT(dma.GetCopyCount() == 1, "one DMA copy executed");

    std::vector<u8> dst(kBytes, 0);
    DMA_ASSERT(mem.ReadBlock(kDst, dst.data(), dst.size()));
    DMA_ASSERT(std::memcmp(src.data(), dst.data(), kBytes) == 0,
               "pitch->pitch copy is byte-exact");

    // --- Pitched multi-line copy (pitch != line length). ---
    constexpr u32 kLines = 4, kLen = 64, kPitch = 128;
    constexpr vaddr_t kSrc2 = 0x0092000000ULL, kDst2 = 0x0093000000ULL;
    DMA_ASSERT(mem.Map(kSrc2, 0x1000, memory::MemoryPermission::All));
    DMA_ASSERT(mem.Map(kDst2, 0x1000, memory::MemoryPermission::All));
    std::vector<u8> src2(kLines * kPitch, 0);
    for (u32 l = 0; l < kLines; ++l)
        for (u32 c = 0; c < kLen; ++c)
            src2[l * kPitch + c] = static_cast<u8>(l * 16 + c);
    DMA_ASSERT(mem.WriteBlock(kSrc2, src2.data(), src2.size()));

    dma.CallMethod(MaxwellDma::REG_LINE_LENGTH_IN, kLen);
    dma.CallMethod(MaxwellDma::REG_LINE_COUNT, kLines);
    dma.CallMethod(MaxwellDma::REG_OFFSET_IN, static_cast<u32>(kSrc2 & 0xFFFFFFFFULL));
    dma.CallMethod(MaxwellDma::REG_OFFSET_IN + 1, static_cast<u32>(kSrc2 >> 32));
    dma.CallMethod(MaxwellDma::REG_OFFSET_OUT, static_cast<u32>(kDst2 & 0xFFFFFFFFULL));
    dma.CallMethod(MaxwellDma::REG_OFFSET_OUT + 1, static_cast<u32>(kDst2 >> 32));
    dma.CallMethod(MaxwellDma::REG_PITCH_IN, kPitch);
    dma.CallMethod(MaxwellDma::REG_PITCH_OUT, kPitch);
    dma.CallMethod(MaxwellDma::REG_LAUNCH, (1u << 2) | (1u << 4));
    DMA_ASSERT(dma.GetCopyCount() == 2, "second DMA copy executed");

    std::vector<u8> dst2(kLines * kPitch, 0);
    DMA_ASSERT(mem.ReadBlock(kDst2, dst2.data(), dst2.size()));
    // Each line's kLen bytes must match; pitch gap stays zero.
    for (u32 l = 0; l < kLines; ++l) {
        DMA_ASSERT(std::memcmp(src2.data() + l * kPitch, dst2.data() + l * kPitch, kLen) == 0,
                   "line content matches");
        for (u32 c = kLen; c < kPitch; ++c) {
            DMA_ASSERT(dst2[l * kPitch + c] == 0, "pitch gap untouched");
        }
    }

    // --- Zero-extent launch must be rejected (no copy executed). ---
    dma.CallMethod(MaxwellDma::REG_LINE_LENGTH_IN, 0);
    dma.CallMethod(MaxwellDma::REG_LAUNCH, (1u << 2) | (1u << 4));
    DMA_ASSERT(dma.GetCopyCount() == 2, "zero-extent launch rejected");

    // --- Pitch -> block-linear: true GOB swizzle round-trip. ---
    // Swizzled-then-deswizzled data must equal the original linear source.
    constexpr u32 kW2 = 64, kH2 = 32, kBH = 4; // width, height, block-height gobs
    constexpr vaddr_t kSrc3 = 0x0094000000ULL, kDst3 = 0x0095000000ULL;
    constexpr vaddr_t kDst4 = 0x0096000000ULL;
    DMA_ASSERT(mem.Map(kSrc3, 0x4000, memory::MemoryPermission::All));
    DMA_ASSERT(mem.Map(kDst3, 0x4000, memory::MemoryPermission::All));
    DMA_ASSERT(mem.Map(kDst4, 0x4000, memory::MemoryPermission::All));
    std::vector<u8> src3(kW2 * kH2);
    for (u32 i = 0; i < src3.size(); ++i) src3[i] = static_cast<u8>(i * 7 + 3);
    DMA_ASSERT(mem.WriteBlock(kSrc3, src3.data(), src3.size()));

    dma.CallMethod(MaxwellDma::REG_LINE_LENGTH_IN, kW2);
    dma.CallMethod(MaxwellDma::REG_LINE_COUNT, kH2);
    dma.CallMethod(MaxwellDma::REG_OFFSET_IN, static_cast<u32>(kSrc3 & 0xFFFFFFFFULL));
    dma.CallMethod(MaxwellDma::REG_OFFSET_IN + 1, static_cast<u32>(kSrc3 >> 32));
    dma.CallMethod(MaxwellDma::REG_OFFSET_OUT, static_cast<u32>(kDst3 & 0xFFFFFFFFULL));
    dma.CallMethod(MaxwellDma::REG_OFFSET_OUT + 1, static_cast<u32>(kDst3 >> 32));
    dma.CallMethod(MaxwellDma::REG_PITCH_IN, kW2);
    dma.CallMethod(MaxwellDma::REG_DST_PARAMS_WIDTH, kW2);
    dma.CallMethod(MaxwellDma::REG_DST_PARAMS_HEIGHT, kH2);
    dma.CallMethod(MaxwellDma::REG_DST_PARAMS_BLOCK_SIZE, (kBH / 4u) << 4); // log2 encoding
    dma.CallMethod(MaxwellDma::REG_LAUNCH, (1u << 2)); // src pitch, dst block-linear
    DMA_ASSERT(dma.GetCopyCount() == 3, "swizzle copy executed");

    // Now reverse: block-linear -> pitch using the swizzled data as source.
    dma.CallMethod(MaxwellDma::REG_LINE_LENGTH_IN, kW2);
    dma.CallMethod(MaxwellDma::REG_LINE_COUNT, kH2);
    dma.CallMethod(MaxwellDma::REG_OFFSET_IN, static_cast<u32>(kDst3 & 0xFFFFFFFFULL));
    dma.CallMethod(MaxwellDma::REG_OFFSET_IN + 1, static_cast<u32>(kDst3 >> 32));
    dma.CallMethod(MaxwellDma::REG_OFFSET_OUT, static_cast<u32>(kDst4 & 0xFFFFFFFFULL));
    dma.CallMethod(MaxwellDma::REG_OFFSET_OUT + 1, static_cast<u32>(kDst4 >> 32));
    dma.CallMethod(MaxwellDma::REG_PITCH_OUT, kW2);
    dma.CallMethod(MaxwellDma::REG_SRC_PARAMS_WIDTH, kW2);
    dma.CallMethod(MaxwellDma::REG_SRC_PARAMS_HEIGHT, kH2);
    dma.CallMethod(MaxwellDma::REG_SRC_PARAMS_BLOCK_SIZE, (kBH / 4u) << 4);
    dma.CallMethod(MaxwellDma::REG_LAUNCH, (1u << 4)); // src block-linear, dst pitch
    DMA_ASSERT(dma.GetCopyCount() == 4, "deswizzle copy executed");

    std::vector<u8> roundtrip(kW2 * kH2, 0);
    DMA_ASSERT(mem.ReadBlock(kDst4, roundtrip.data(), roundtrip.size()));
    DMA_ASSERT(src3 == roundtrip, "swizzle->deswizzle round-trip is byte-exact");

    // --- Remap CONST_A fast clear: destination filled with the 8-byte pattern. ---
    constexpr vaddr_t kClear = 0x0097000000ULL;
    constexpr u32 kClearLen = 256, kClearLines = 4, kClearPitch = 512;
    DMA_ASSERT(mem.Map(kClear, 0x2000, memory::MemoryPermission::All));
    std::vector<u8> pre(kClearLines * kClearPitch, 0xEE);
    DMA_ASSERT(mem.WriteBlock(kClear, pre.data(), pre.size()));

    dma.CallMethod(MaxwellDma::REG_LINE_LENGTH_IN, kClearLen);
    dma.CallMethod(MaxwellDma::REG_LINE_COUNT, kClearLines);
    dma.CallMethod(MaxwellDma::REG_OFFSET_OUT, static_cast<u32>(kClear & 0xFFFFFFFFULL));
    dma.CallMethod(MaxwellDma::REG_OFFSET_OUT + 1, static_cast<u32>(kClear >> 32));
    dma.CallMethod(MaxwellDma::REG_PITCH_OUT, kClearPitch);
    dma.CallMethod(MaxwellDma::REG_REMAP_COMPONENTS, 0x11); // 2 components, 2 bytes each
    dma.CallMethod(MaxwellDma::REG_REMAP_CONST_A, 0xDEADBEEF);
    dma.CallMethod(MaxwellDma::REG_REMAP_CONST_B, 0xCAFEBABE);
    dma.CallMethod(MaxwellDma::REG_LAUNCH, 0x1); // remap_enable -> CONST_A clear
    DMA_ASSERT(dma.GetCopyCount() == 5, "fast clear executed");

    std::vector<u8> cleared(kClearLines * kClearPitch, 0);
    DMA_ASSERT(mem.ReadBlock(kClear, cleared.data(), cleared.size()));
    const u64 pattern8 = 0xCAFEBABEDEADBEEFULL;
    for (u32 l = 0; l < kClearLines; ++l) {
        // Cleared region repeats the 8-byte pattern.
        for (u32 c = 0; c < kClearLen; c += 8) {
            u64 got = 0;
            std::memcpy(&got, cleared.data() + l * kClearPitch + c,
                        std::min<size_t>(8, kClearLen - c));
            const u64 want = (c + 8 <= kClearLen) ? pattern8
                                : (pattern8 & ((1ULL << ((kClearLen - c) * 8)) - 1));
            DMA_ASSERT(got == want, "cleared bytes match remap constant");
        }
        // Pitch gap between lines untouched.
        for (u32 c = kClearLen; c < kClearPitch; ++c) {
            DMA_ASSERT(cleared[l * kClearPitch + c] == 0xEE, "pitch gap preserved");
        }
    }

    std::cout << "  - Maxwell DMA (1D + pitched + swizzle round-trip + remap fast-clear): PASSED" << std::endl;
    return 0;
}
