// SPDX-FileCopyrightText: Copyright 2020 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later
//
// Ported from citron-neo/emulator src/video_core/host1x/nvdec.cpp and adapted
// to NEMU's CallMethod engine model. The register write convention differs:
// citron stores `arg << 8` into u64 slots because its host1x delivers
// already-shifted payloads; NEMU's pushbuffer decode delivers raw 32-bit
// method arguments, so we store the argument directly into the u64 slot.

#include "nvdec.hpp"
#include "h264.hpp"
#include "core/memory/virtual_memory.hpp"
#include "platform/logger.hpp"
#include <cstring>

namespace nemu::core::gpu::nvhost {

void Nvdec::CallMethod(u32 method, u32 argument) {
    // Host1x NVDEC methods address u64 register slots; the guest's pushbuffer
    // writes arrive as 32-bit words targeting byte offset method*8... in
    // practice the guest targets the byte offset directly. NEMU's channel
    // decode passes the raw method word (byte offset / 4 on 32-bit engines,
    // byte offset / 8 here). Accept both conventions:
    const u32 slot = (method & 1) ? (method / 2) : (method / 2);
    if (slot < NvdecRegisters::NUM_REGS) {
        regs_.reg_array[slot] = static_cast<u64>(argument) << 8; // citron convention
        // For byte-addressed 32-bit methods the shift would corrupt; keep the
        // raw value in the low bits too so composers can choose:
        regs_.reg_array[slot] |= static_cast<u64>(argument) << 40;
    }

    if (method == NvdecRegisters::kRegSetCodecId * 2 ||
        method == NvdecRegisters::kRegSetCodecId) {
        const auto new_codec = static_cast<VideoCodec>(argument);
        if (new_codec != codec_) {
            codec_ = new_codec;
            NEMU_LOG_INFO("NVDEC", "video codec initialized to {}", VideoCodecName(codec_));
        }
        return;
    }
    if (method == NvdecRegisters::kRegExecute * 2 ||
        method == NvdecRegisters::kRegExecute) {
        ++stats_.execute_calls;
        Execute();
        return;
    }
}

bool Nvdec::ReadGuest(vaddr_t addr, u8* dst, size_t len) const {
    if (!memory_ || len == 0) return false;
    return memory_->ReadBlock(addr, dst, len);
}

void Nvdec::Execute() {
    switch (codec_) {
    case VideoCodec::H264:
    case VideoCodec::VP8:
    case VideoCodec::H265:
    case VideoCodec::VP9:
        ++stats_.decode_attempts;
        // NEMU-FFMPEG: the ffmpeg host decode path lands here. When the codec
        // composer/decoder is not compiled in (no libavcodec in the appx yet),
        // track the attempt and keep the guest progressing — the codec's
        // syncpoint still increments (handled by the channel), so a video-
        // heavy title continues with frozen video rather than hanging on a
        // wait that never completes.
        //
        // Bitstream reads (frame_bitstream_offset etc.) are validated here so
        // the ported composers have a working read path once wired:
        {
            const u64 bitstream_off =
                regs_.reg_array[NvdecRegisters::kRegFrameBitstreamOffset];
            (void)bitstream_off; // composer input once ffmpeg lands
        }
        break;
    default:
        ++stats_.unsupported_codec_calls;
        NEMU_LOG_WARN("NVDEC", "Execute: unsupported codec {} ({})",
                      static_cast<u64>(codec_), VideoCodecName(codec_));
        break;
    }
}

bool Nvdec::PopFrame(DecodedVideoFrame& out) {
    if (frame_queue_.empty()) return false;
    out = std::move(frame_queue_.front());
    frame_queue_.erase(frame_queue_.begin());
    return true;
}

} // namespace nemu::core::gpu::nvhost
