// SPDX-FileCopyrightText: Copyright 2020 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later
//
// Ported from citron-neo/emulator src/video_core/host1x/nvdec_common.h
// and adapted to NEMU's memory + nvmap model.

#pragma once

#include "core/types.hpp"
#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace nemu::core::gpu::nvhost {

// NVDEC video codec identifiers (switchbrew / citron nvdec_common.h).
enum class VideoCodec : u64 {
    None = 0x0,
    H264 = 0x3,
    VP8 = 0x5,
    H265 = 0x7,
    VP9 = 0x9,
};

// NVDEC engine register file. Host1x methods address u64 slots; the guest
// writes them via 32-bit pushbuffer words (arg << 8 in citron, tracked here
// as raw u32 method/argument pairs — see Nvdec::CallMethod).
struct NvdecRegisters {
    static constexpr std::size_t NUM_REGS = 0x178;

    std::array<u64, NUM_REGS> reg_array{};

    // Register word indices (method addresses, in u64-slot units):
    static constexpr u32 kRegSetCodecId = 0x80;  // 0x400 byte offset / 8
    static constexpr u32 kRegExecute = 0xC0;     // 0x600 byte offset / 8
    static constexpr u32 kRegControlParams = 0x100;
    static constexpr u32 kRegPictureInfoOffset = 0x101;
    static constexpr u32 kRegFrameBitstreamOffset = 0x102;
    static constexpr u32 kRegFrameNumber = 0x103;
    static constexpr u32 kRegH264SliceDataOffsets = 0x104;
    static constexpr u32 kRegSurfaceLumaOffset = 0x10C;  // 17 entries
    static constexpr u32 kRegSurfaceChromaOffset = 0x11D; // 17 entries
    static constexpr u32 kRegVp8ProbDataOffset = 0x150;
    static constexpr u32 kRegVp8HeaderPartitionBufOffset = 0x151;
    static constexpr u32 kRegVp9EntropyProbsOffset = 0x170;
    static constexpr u32 kRegVp9BackwardUpdatesOffset = 0x171;
    static constexpr u32 kRegVp9LastFrameSegmapOffset = 0x172;
    static constexpr u32 kRegVp9CurrFrameSegmapOffset = 0x173;
    static constexpr u32 kRegVp9LastFrameMvsOffset = 0x175;
    static constexpr u32 kRegVp9CurrFrameMvsOffset = 0x176;
};
static_assert(sizeof(NvdecRegisters) == 0xBC0, "NvdecRegisters size must match hardware layout");

[[nodiscard]] constexpr std::string_view VideoCodecName(VideoCodec codec) {
    switch (codec) {
    case VideoCodec::H264: return "H264";
    case VideoCodec::VP8: return "VP8";
    case VideoCodec::H265: return "H265";
    case VideoCodec::VP9: return "VP9";
    case VideoCodec::None: return "None";
    default: return "Unknown";
    }
}

} // namespace nemu::core::gpu::nvhost
