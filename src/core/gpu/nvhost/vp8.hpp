// SPDX-FileCopyrightText: Copyright 2021 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later
//
// Ported from citron-neo/emulator src/video_core/host1x/codecs/vp8.h,
// adapted to NEMU's memory model.

#pragma once

#include "nvdec_common.hpp"
#include "core/types.hpp"
#include <array>
#include <span>
#include <vector>

namespace nemu::core::memory {
class VirtualMemory;
}

namespace nemu::core::gpu::nvhost::decoder {

/// VP8 hardware picture info (Tegra decode ring layout).
struct VP8PictureInfo {
    std::array<u8, 0x38> padding0;     ///< 14 words
    u16 frame_width;                   ///< actual frame width
    u16 frame_height;                  ///< actual frame height
    u8 key_frame;
    u8 version;                        ///< codec version (3 bits used)
    u8 tile_format_gob_height;         ///< citron bitfield union (raw byte)
    u8 error_conceal_on;               ///< 1: error conceal on; 0: off
    u32 first_part_size;               ///< first partition size (header + mb)
    u32 hist_buffer_size;              ///< in units of 256
    u32 vld_buffer_size;               ///< in units of 1
    std::array<u32, 2> frame_stride;   ///< [y_c]
    u32 luma_top_offset;               ///< in units of 256
    u32 luma_bot_offset;
    u32 luma_frame_offset;
    u32 chroma_top_offset;
    u32 chroma_bot_offset;
    u32 chroma_frame_offset;
    std::array<u8, 0x1C> padding1;     ///< NvdecDisplayParams
    s8 current_output_memory_layout;
    std::array<s8, 3> output_memory_layout; ///< golden / altref / last
    u8 segmentation_feature_data_update;
    std::array<u8, 3> padding2;
    u32 result_value;                  ///< ucode return result
    std::array<u32, 8> partition_offset;
    std::array<u8, 12> padding3;       ///< 3 words
};
static_assert(sizeof(VP8PictureInfo) == 0xC0, "VP8PictureInfo size must match HW");

/// VP8 composer: rebuilds the RFC 6386 frame header from Tegra picture info.
class VP8 {
public:
    explicit VP8(memory::VirtualMemory* memory) : memory_(memory) {}
    ~VP8() = default;

    /// Compose the VP8 frame for host decoding. Returns false on read failure.
    [[nodiscard]] bool ComposeFrame(const NvdecRegisters& regs, std::vector<u8>& out_frame);

private:
    memory::VirtualMemory* memory_;
    std::vector<u8> frame_;
};

} // namespace nemu::core::gpu::nvhost::decoder
