// Engine upload (inline-to-memory) — clean-room port of the yuzu/citron
// engine_upload semantics (GPL-2.0-or-later reference; this port is GPL-3.0-or-later).
//
// Games upload constant buffers, uniforms, and texture lines GPU-side through
// two Maxwell3D methods that NEMU previously dropped on the floor:
//   LaunchDma  (0x6C) — latches the destination + layout, resets the cursor
//   InlineData (0x6D) — appends 32-bit words; flushed to guest memory on the
//                        last call (is_last_call / CallMultiMethod bulk form)
//
// Registers (yuzu Upload::Registers, at method 0x60):
//   0x60 line_length_in   0x61 line_count
//   0x62 dst.address_high 0x63 dst.address_low 0x64 dst.pitch
//   0x65 dst.block_size (bw:4 bh:4 bd:4) 0x66 dst.width 0x67 dst.height
//   0x68 dst.depth 0x69 dst.layer 0x6A dst.x 0x6B dst.y
#pragma once

#include "core/types.hpp"
#include <array>
#include <span>
#include <vector>

namespace nemu::core::gpu {
class GpuMemoryManager;
}

namespace nemu::core::gpu {

class EngineUpload {
public:
    // Register offsets within the Maxwell3D method space.
    enum Reg : u32 {
        REG_LINE_LENGTH_IN = 0x60,
        REG_LINE_COUNT = 0x61,
        REG_DST_ADDR_HIGH = 0x62,
        REG_DST_ADDR_LOW = 0x63,
        REG_DST_PITCH = 0x64,
        REG_DST_BLOCK_SIZE = 0x65, // bits[3:0] bw, [7:4] bh, [11:8] bd
        REG_DST_WIDTH = 0x66,
        REG_DST_HEIGHT = 0x67,
        REG_DST_DEPTH = 0x68,
        REG_DST_LAYER = 0x69,
        REG_DST_X = 0x6A,
        REG_DST_Y = 0x6B,
    };

    explicit EngineUpload(GpuMemoryManager* memory) : memory_(memory) {}

    void SetReg(u32 method, u32 argument) noexcept;
    [[nodiscard]] u32 GetReg(u32 method) const noexcept;

    /// LaunchDMA register write: latch dest, reset cursor (is_linear = pitch).
    void ProcessExec(bool is_linear) noexcept;

    /// InlineData single-word write; flushes when is_last_call.
    void ProcessData(u32 data, bool is_last_call) noexcept;

    /// CallMultiMethod bulk form (mode-2 inline pushbuffer streams).
    void ProcessData(std::span<const u32> data) noexcept;

    [[nodiscard]] u64 DestAddress() const noexcept {
        return (static_cast<u64>(dst_addr_high_) << 32) | dst_addr_low_;
    }
    [[nodiscard]] u32 UploadSize() const noexcept { return copy_size_; }

private:
    void Flush() noexcept;

    GpuMemoryManager* memory_{nullptr};
    u32 line_length_in_{0};
    u32 line_count_{0};
    u32 dst_addr_high_{0};
    u32 dst_addr_low_{0};
    u32 dst_pitch_{0};
    u32 dst_block_height_{0};
    u32 dst_width_{0};
    u32 dst_height_{0};
    u32 dst_x_{0};
    u32 dst_y_{0};
    u32 write_offset_{0};
    u32 copy_size_{0};
    bool is_linear_{false};
    std::vector<u8> inner_buffer_;
};

} // namespace nemu::core::gpu
