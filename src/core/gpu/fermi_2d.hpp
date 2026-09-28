#pragma once

#include "core/types.hpp"
#include <memory>

namespace nemu::core::memory {
class VirtualMemory;
}

namespace nemu::core::gpu {

// ---------------------------------------------------------------------------
// Fermi 2D blit engine (class 0xF1) — ported from yuzu's fermi_2d.cpp
// (GPL-2.0). Commercial games use it for resolution changes, HUD compositing,
// and surface copies. Compact HLE: software blit between pitch (linear)
// surfaces through guest virtual memory, SrcCopy operation with
// nearest/linear sampling like yuzu's SoftwareBlitEngine.
// ---------------------------------------------------------------------------
class Fermi2D {
public:
    // Render-target pixel formats (yuzu Fermi2D::SurfaceFormat).
    enum class Format : u32 {
        RGBA8_UNORM = 0x01, // 4 bytes/px
        RGB8_UNORM = 0x07,  // 3 bytes/px (padded sources rare in blits)
        RGBX8_UNORM = 0x06,
        R8_UNORM = 0x29,    // 1 byte/px
    };

    // Register offsets (public Fermi 2D method map; offsets marked ~ are
    // best-effort from community reverse-engineering docs, not from a verified
    // header — the engine contract (trigger=1 -> blit) is what games rely on).
    enum Reg : u32 {
        REG_OPERATION = 0x0B4,        // 0 = SrcCopy
        REG_BLIT_TRIGGER = 0x08C,     // write 1 = launch blit
        // Surface parameter blocks (compact HLE layout; distinct region).
        REG_SURFACE_SRC = 0x0C0,      // src format
        REG_SURFACE_SRC_PITCH = 0x0C1,
        REG_SURFACE_SRC_WIDTH = 0x0C2,
        REG_SURFACE_SRC_HEIGHT = 0x0C3,
        REG_SURFACE_DST = 0x0E0,      // dst format
        REG_SURFACE_DST_PITCH = 0x0E1,
        REG_SURFACE_DST_WIDTH = 0x0E2,
        REG_SURFACE_DST_HEIGHT = 0x0E3,
        // Pixels-from-memory parameters.
        REG_PFM_SRC_ADDRESS = 0x100,  // src lower (upper at +1)
        REG_PFM_SRC_ADDRESS_U = 0x101,
        REG_PFM_DST_ADDRESS = 0x104,
        REG_PFM_DST_ADDRESS_U = 0x105,
        REG_PFM_SRC_X0 = 0x108,       // fixed-point 32.32
        REG_PFM_SRC_Y0 = 0x10C,
        REG_PFM_DST_X0 = 0x110,
        REG_PFM_DST_Y0 = 0x114,
        REG_PFM_DST_WIDTH = 0x118,
        REG_PFM_DST_HEIGHT = 0x11C,
        REG_PFM_DU_DX = 0x120,        // src step per dst px (32.32)
        REG_PFM_DV_DY = 0x124,
    };

    static constexpr size_t BytesPerPixel(Format f) noexcept {
        switch (f) {
        case Format::RGBA8_UNORM:
        case Format::RGBX8_UNORM: return 4;
        case Format::RGB8_UNORM: return 3;
        case Format::R8_UNORM: return 1;
        default: return 4;
        }
    }

    explicit Fermi2D(memory::VirtualMemory* memory);

    void CallMethod(u32 method, u32 argument);
    void Reset();

    [[nodiscard]] u64 GetBlitCount() const noexcept { return blit_count_; }

private:
    void Blit();

    memory::VirtualMemory* memory_;

    // Latched registers (only the ones the compact HLE consumes).
    u32 operation_{0};
    u32 src_format_{0};
    u32 dst_format_{0};
    u32 src_width_{0}, src_height_{0}, src_pitch_{0};
    u32 dst_width_{0}, dst_height_{0}, dst_pitch_{0};
    u32 src_layer_addr_{0}; // linear pitch source
    u32 dst_layer_addr_{0};
    u64 src_addr_{0};
    u64 dst_addr_{0};
    s64 src_x0_{0}, src_y0_{0};
    s64 du_dx_{0}, dv_dy_{0};
    u32 dst_x0_{0}, dst_y0_{0};

    u64 blit_count_{0};
};

} // namespace nemu::core::gpu
