// SPDX-FileCopyrightText: Copyright 2020 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later
//
// Ported from citron-neo/emulator src/video_core/host1x/nvdec.h and adapted
// to NEMU's Channel/CallMethod engine model.

#pragma once

#include "nvdec_common.hpp"
#include "core/types.hpp"
#include <span>
#include <string>
#include <vector>

namespace nemu::core::memory {
class VirtualMemory;
}

namespace nemu::core::gpu::nvhost {

/// Decoded video frame (host-side presentation surface).
struct DecodedVideoFrame {
    u32 width{0};
    u32 height{0};
    // NEMU-FFMPEG: when the ffmpeg host path is compiled in, decoded planes
    // land here. Without it, frames stay empty and titles continue with
    // frozen (but never hanging) video.
    std::vector<u8> y_plane;
    std::vector<u8> uv_plane; // NV12 interleaved
    u32 y_stride{0};
    u32 uv_stride{0};
    u64 frame_number{0};
};

/// NVDEC engine: processes host1x methods and decodes video streams.
/// Engine class 0x7F (bind via AllocObjCtx on the nvhost-nvdec channel).
class Nvdec {
public:
    explicit Nvdec(memory::VirtualMemory* memory) : memory_(memory) {}
    virtual ~Nvdec() = default;

    /// Process one pushbuffer method targeting this engine.
    void CallMethod(u32 method, u32 argument);

    /// Current target codec (register 0x400).
    [[nodiscard]] VideoCodec GetCodec() const noexcept { return codec_; }

    /// Total successfully decoded frames since channel open.
    [[nodiscard]] u64 GetDecodedFrameCount() const noexcept { return frames_decoded_; }

    /// Pop the next decoded frame (empty optional-equivalent: frame_number==0
    /// with empty planes means "no frame ready yet" — callers must not block).
    [[nodiscard]] bool PopFrame(DecodedVideoFrame& out);

    /// Statistics for diagnostics.
    struct Stats {
        u64 execute_calls{0};
        u64 decode_attempts{0};
        u64 frames_decoded{0};
        u64 unsupported_codec_calls{0};
    };
    [[nodiscard]] const Stats& GetStats() const noexcept { return stats_; }

protected:
    /// Read the guest bitstream at a GPU-mapped address. Overridable in tests.
    virtual bool ReadGuest(vaddr_t addr, u8* dst, size_t len) const;

private:
    void Execute();

    memory::VirtualMemory* memory_;
    NvdecRegisters regs_{};
    VideoCodec codec_{VideoCodec::None};
    u64 frames_decoded_{0};
    std::vector<DecodedVideoFrame> frame_queue_;
    Stats stats_{};

    // Per-codec decode state (populated as codec composers are ported).
    struct H264State {
        bool initialized{false};
    } h264_;
};

} // namespace nemu::core::gpu::nvhost
