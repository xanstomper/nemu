#pragma once

#include "core/types.hpp"
#include <span>
#include <memory>

namespace nemu::core::memory {
class VirtualMemory;
}

namespace nemu::core::gpu {

// ---------------------------------------------------------------------------
// Maxwell DMA (engine class 0xB0B7) — GPU-side copy engine, ported from yuzu's
// maxwell_dma.cpp (GPL-2.0). Commercial games use this for texture streaming,
// mipmap-generation inputs, and render-target copies. Compact HLE: the copy
// executes through guest virtual memory immediately when the Launch register
// (method 0x06C0) is written. GPU VA -> guest VA resolution happens upstream in
// the nvhost channel (AddressSpace::GpuVaToGuestVa), matching how the pushbuffer
// itself is read.
// ---------------------------------------------------------------------------
class MaxwellDma {
public:
    // Register offsets within the DMA engine's method space (yuzu Regs layout).
    enum Reg : u32 {
        REG_LINE_LENGTH_IN = 0x060C,   // bytes for pitch copies
        REG_LINE_COUNT = 0x0638,       // lines
        REG_OFFSET_IN = 0x063C,        // src guest address (upper 32 bits at +1)
        REG_OFFSET_OUT = 0x0644,       // dst guest address
        REG_PITCH_IN = 0x0650,
        REG_PITCH_OUT = 0x0658,
        REG_LAUNCH = 0x06C0,           // LaunchDMA config
        // Dst block-linear surface params (yuzu dst_params).
        REG_DST_PARAMS_WIDTH = 0x06A8,
        REG_DST_PARAMS_HEIGHT = 0x06AC,
        REG_DST_PARAMS_BLOCK_SIZE = 0x06B0, // bits [7:4] = block height gobs log2
        // Remap (yuzu remap_const / remap_components): component swizzle +
        // CONST_A fast-clear, and per-component repack on copies.
        REG_REMAP_CONST_A = 0x0700,    // consta value word 1
        REG_REMAP_CONST_B = 0x0704,    // consta value word 2 (8-byte clear)
        REG_REMAP_COMPONENTS = 0x070C, // num components-1 [1:0], sizes-1 [5:4], src swizzle [11:8]
        // Src block-linear surface params (yuzu src_params; offset after remap).
        REG_SRC_PARAMS_WIDTH = 0x071C,
        REG_SRC_PARAMS_HEIGHT = 0x0720,
        REG_SRC_PARAMS_BLOCK_SIZE = 0x0724,
    };

    // Remap component control (yuzu RemapConst).
    struct Remap {
        u32 components_reg{};
        // bits [1:0] num_dst_components_minus_one; [5:4] component_size_minus_one
        [[nodiscard]] u32 NumComponents() const noexcept {
            return (components_reg & 0x3) + 1;
        }
        [[nodiscard]] u32 ComponentSize() const noexcept {
            return ((components_reg >> 4) & 0x3) + 1;
        }
    };

    // LaunchDMA bit fields (yuzu launch_dma union).
    struct Launch {
        u32 raw{};
        // bit 2: src_memory_layout (0 = BLOCK_LINEAR, 1 = PITCH)
        // bit 4: dst_memory_layout (0 = BLOCK_LINEAR, 1 = PITCH)
        static constexpr u32 kSrcPitchBit = 2;
        static constexpr u32 kDstPitchBit = 4;
        [[nodiscard]] bool SrcIsPitch() const noexcept { return raw & (1u << kSrcPitchBit); }
        [[nodiscard]] bool DstIsPitch() const noexcept { return raw & (1u << kDstPitchBit); }
    };

    explicit MaxwellDma(memory::VirtualMemory* memory);

    // Method write from the pushbuffer command stream.
    void CallMethod(u32 method, u32 argument);

    // Reset per-copy register state (channel re-use).
    void Reset();

    [[nodiscard]] u64 GetCopyCount() const noexcept { return copy_count_; }

private:
    void DoLaunch();
    void FastClear();
    void CopyPitchToPitch();
    void CopyPitchToBlockLinear();
    void CopyBlockLinearToPitch();
    void CopyBlockLinearToBlockLinear();

    memory::VirtualMemory* memory_;

    // Copy parameters (registers latched from the command stream).
    u32 line_length_in_{0};
    u32 line_count_{0};
    u64 offset_in_{0};
    u64 offset_out_{0};
    u32 pitch_in_{0};
    u32 pitch_out_{0};
    Launch launch_{};

    // Block-linear surface params (latched; consumed by the swizzle paths).
    u32 dst_width_{0}, dst_height_{0}, dst_block_height_gobs_{0};
    u32 src_width_{0}, src_height_{0}, src_block_height_gobs_{0};

    // Remap state (const-A fast clear + component repack).
    u64 remap_const_{0};       // 8-byte clear value (two latched words)
    Remap remap_{};

    u64 copy_count_{0};
};

} // namespace nemu::core::gpu
