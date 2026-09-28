#include "core/gpu/fermi_2d.hpp"
#include "core/memory/virtual_memory.hpp"
#include "core/types.hpp"
#include <iostream>
#include <cstdlib>
#include <vector>
#include <cstring>

using namespace nemu;
using namespace nemu::core;
using namespace nemu::core::gpu;

#define F2D_ASSERT(cond, ...)                                                  \
    do {                                                                       \
        if (!(cond)) {                                                         \
            std::cerr << "Assertion failed: " #cond << " at "                  \
                      << __FILE__ << ":" << __LINE__ << std::endl;             \
            std::exit(1);                                                      \
        }                                                                      \
    } while (0)

int main() {
    std::cout << "[Test: Fermi 2D blit engine (0xF1)]" << std::endl;

    memory::VirtualMemory mem;
    constexpr vaddr_t kSrc = 0x00A0000000ULL;
    constexpr vaddr_t kDst = 0x00A1000000ULL;
    constexpr u32 kW = 16, kH = 16, kBpp = 4;
    constexpr u32 kSrcPitch = kW * kBpp;
    constexpr u32 kDstPitch = 32 * kBpp; // wider dst (stride padding)
    F2D_ASSERT(mem.Map(kSrc, 0x1000, memory::MemoryPermission::All));
    F2D_ASSERT(mem.Map(kDst, 0x2000, memory::MemoryPermission::All));

    // Source: 16x16 gradient; dst starts zeroed.
    std::vector<u8> src(kH * kSrcPitch);
    for (u32 y = 0; y < kH; ++y)
        for (u32 x = 0; x < kW; ++x) {
            u8* px = src.data() + (y * kSrcPitch) + x * kBpp;
            px[0] = static_cast<u8>(x * 16); px[1] = static_cast<u8>(y * 16);
            px[2] = 0x40; px[3] = 0xFF;
        }
    F2D_ASSERT(mem.WriteBlock(kSrc, src.data(), src.size()));

    Fermi2D fermi(&mem);
    fermi.CallMethod(Fermi2D::REG_OPERATION, 0); // SrcCopy
    fermi.CallMethod(Fermi2D::REG_PFM_SRC_ADDRESS, static_cast<u32>(kSrc & 0xFFFFFFFFULL));
    fermi.CallMethod(Fermi2D::REG_PFM_SRC_ADDRESS_U, static_cast<u32>(kSrc >> 32));
    fermi.CallMethod(Fermi2D::REG_PFM_DST_ADDRESS, static_cast<u32>(kDst & 0xFFFFFFFFULL));
    fermi.CallMethod(Fermi2D::REG_PFM_DST_ADDRESS_U, static_cast<u32>(kDst >> 32));
    fermi.CallMethod(Fermi2D::REG_PFM_SRC_X0, 0);
    fermi.CallMethod(Fermi2D::REG_PFM_SRC_Y0, 0);
    fermi.CallMethod(Fermi2D::REG_PFM_DST_X0, 0);
    fermi.CallMethod(Fermi2D::REG_PFM_DST_Y0, 0);
    fermi.CallMethod(Fermi2D::REG_PFM_DST_WIDTH, kW);
    fermi.CallMethod(Fermi2D::REG_PFM_DST_HEIGHT, kH);
    // Null derivatives -> 1:1 sampling (yuzu null_derivative = 1<<32).
    fermi.CallMethod(Fermi2D::REG_PFM_DU_DX, 0);
    fermi.CallMethod(Fermi2D::REG_PFM_DV_DY, 0);
    // Surface params consumed by the compact HLE.
    fermi.CallMethod(Fermi2D::REG_SURFACE_SRC, static_cast<u32>(Fermi2D::Format::RGBA8_UNORM));
    fermi.CallMethod(Fermi2D::REG_SURFACE_SRC_PITCH, kSrcPitch);
    fermi.CallMethod(Fermi2D::REG_SURFACE_SRC_WIDTH, kW);
    fermi.CallMethod(Fermi2D::REG_SURFACE_SRC_HEIGHT, kH);
    fermi.CallMethod(Fermi2D::REG_SURFACE_DST, static_cast<u32>(Fermi2D::Format::RGBA8_UNORM));
    fermi.CallMethod(Fermi2D::REG_SURFACE_DST_PITCH, kDstPitch);
    fermi.CallMethod(Fermi2D::REG_SURFACE_DST_WIDTH, kW);
    fermi.CallMethod(Fermi2D::REG_SURFACE_DST_HEIGHT, kH);

    // Trigger the blit.
    fermi.CallMethod(Fermi2D::REG_BLIT_TRIGGER, 1);
    F2D_ASSERT(fermi.GetBlitCount() == 1, "one blit executed");

    // Verify 1:1 copy landed on the dst rows (pitch gap stays zero).
    std::vector<u8> dst(kH * kDstPitch, 0);
    F2D_ASSERT(mem.ReadBlock(kDst, dst.data(), dst.size()));
    for (u32 y = 0; y < kH; ++y) {
        F2D_ASSERT(std::memcmp(src.data() + y * kSrcPitch,
                               dst.data() + y * kDstPitch, kW * kBpp) == 0,
                   "row content matches");
        for (u32 c = kW * kBpp; c < kDstPitch; ++c) {
            F2D_ASSERT(dst[y * kDstPitch + c] == 0, "dst pitch gap untouched");
        }
    }

    // Degenerate blit (zero width) must be skipped.
    fermi.CallMethod(Fermi2D::REG_PFM_DST_WIDTH, 0);
    fermi.CallMethod(Fermi2D::REG_BLIT_TRIGGER, 1);
    F2D_ASSERT(fermi.GetBlitCount() == 1, "degenerate blit skipped");

    std::cout << "  - Fermi 2D blit (1:1 SrcCopy + pitch handling + guards): PASSED" << std::endl;
    return 0;
}
