#pragma once

#include "core/types.hpp"
#include <memory>

namespace nemu::core::memory {
class VirtualMemory;
}
namespace nemu::core::gpu {
class IGpuBackend;
}

namespace nemu::core::gpu {

// ---------------------------------------------------------------------------
// Kepler Compute (engine class 0xB197) — ported from yuzu's kepler_compute.cpp
// (GPL-2.0). Commercial games submit compute ( SSAO, physics, particles,
// video decoding) on this dedicated channel rather than through Maxwell 3D.
//
// Compact HLE: the engine latches the launch-descriptor location (QMD address
// in guest memory); writing the `launch` register reads the full 256-byte QMD
// and dispatches through the GPU backend. Const-buffer / code-address setup
// arrives before launch via upload/register methods.
// ---------------------------------------------------------------------------
class KeplerCompute {
public:
    // Register offsets (yuzu KEPLER_COMPUTE_REG_INDEX layout).
    enum Reg : u32 {
        REG_LAUNCH_DESC_LOC = 0x00AD, // address of the 256-byte QMD (lower; upper at +1)
        REG_LAUNCH = 0x00CF,          // write = dispatch (yuzu `launch`)
        REG_SET_SHADER_SCRATCH = 0x060, // code address hint (latched)
    };

    explicit KeplerCompute(memory::VirtualMemory* memory, IGpuBackend* backend);

    void CallMethod(u32 method, u32 argument);
    void Reset();

    [[nodiscard]] u64 GetDispatchCount() const noexcept { return dispatch_count_; }
    [[nodiscard]] u64 GetQmdAddress() const noexcept { return qmd_addr_; }

private:
    void ProcessLaunch();

    memory::VirtualMemory* memory_;
    IGpuBackend* backend_;

    u64 qmd_addr_{0};
    u64 dispatch_count_{0};
};

} // namespace nemu::core::gpu
