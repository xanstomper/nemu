// SPDX-FileCopyrightText: Copyright 2021 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later
//
// Ported from citron-neo/emulator (originally yuzu) vp8.cpp, adapted to
// NEMU's memory model. Header rebuild per page 30 of RFC 6386.

#include "vp8.hpp"
#include "core/memory/virtual_memory.hpp"
#include "platform/logger.hpp"
#include <cstring>

namespace nemu::core::gpu::nvhost::decoder {

bool VP8::ComposeFrame(const NvdecRegisters& regs, std::vector<u8>& out_frame) {
    if (!memory_) return false;

    VP8PictureInfo info{};
    const u64 pic_info = regs.reg_array[NvdecRegisters::kRegPictureInfoOffset] >> 8;
    if (!memory_->ReadBlock(pic_info, &info, sizeof(info))) {
        NEMU_LOG_WARN("NVDEC.VP8", "failed to read picture_info at 0x{:X}", pic_info);
        return false;
    }

    const bool is_key_frame = info.key_frame == 1u;
    const auto bitstream_size = static_cast<size_t>(info.vld_buffer_size);
    const size_t header_size = is_key_frame ? 10u : 3u;
    out_frame.resize(header_size + bitstream_size);

    // Page 30 of the VP8 specification (RFC 6386).
    out_frame[0] = is_key_frame ? 0u : 1u;                    // frame type
    out_frame[0] |= static_cast<u8>((info.version & 7u) << 1u); // version
    out_frame[0] |= static_cast<u8>(1u << 4u);                // show_frame
    // 19-bit first partition size.
    out_frame[0] |= static_cast<u8>((info.first_part_size & 7u) << 5u);
    out_frame[1] = static_cast<u8>((info.first_part_size & 0x7f8u) >> 3u);
    out_frame[2] = static_cast<u8>((info.first_part_size & 0x7f800u) >> 11u);

    if (is_key_frame) {
        out_frame[3] = 0x9du;  // start code
        out_frame[4] = 0x01u;
        out_frame[5] = 0x2au;
        // TODO(upstream): Horizontal/Vertical Scale
        out_frame[6] = static_cast<u8>(info.frame_width & 0xff);
        out_frame[7] = static_cast<u8>((info.frame_width >> 8) & 0x3f);
        out_frame[8] = static_cast<u8>(info.frame_height & 0xff);
        out_frame[9] = static_cast<u8>((info.frame_height >> 8) & 0x3f);
    }

    if (bitstream_size > 0) {
        const u64 bitstream_addr = regs.reg_array[NvdecRegisters::kRegFrameBitstreamOffset] >> 8;
        if (!memory_->ReadBlock(bitstream_addr, out_frame.data() + header_size, bitstream_size)) {
            NEMU_LOG_WARN("NVDEC.VP8", "failed to read bitstream at 0x{:X}", bitstream_addr);
            return false;
        }
    }
    return true;
}

} // namespace nemu::core::gpu::nvhost::decoder
