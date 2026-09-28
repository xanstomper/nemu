// SPDX-FileCopyrightText: Ryujinx Team and Contributors
// SPDX-License-Identifier: MIT
//
// Ported from citron-neo/emulator (originally Ryujinx) src/video_core/
// host1x/codecs/h264.cpp/.h, adapted to NEMU's memory model
// (memory::VirtualMemory::ReadBlock instead of host1x.GMMU()).

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

/// H.264 Annex-B bitstream writer (clause 9.1 of the H.264 spec).
class H264BitWriter {
public:
    H264BitWriter() = default;
    ~H264BitWriter() = default;

    void WriteU(s32 value, s32 value_sz);
    void WriteSe(s32 value);
    void WriteUe(u32 value);
    void End();
    void WriteBit(bool state);
    void WriteScalingList(std::span<u8> scan, std::span<const u8> list, s32 start, s32 count);

    [[nodiscard]] std::vector<u8>& GetByteArray() noexcept { return byte_array_; }
    [[nodiscard]] const std::vector<u8>& GetByteArray() const noexcept { return byte_array_; }

private:
    void WriteBits(s32 value, s32 bit_count);
    void WriteExpGolombCodedInt(s32 value);
    void WriteExpGolombCodedUInt(u32 value);
    [[nodiscard]] s32 GetFreeBufferBits();
    void Flush();

    static constexpr s32 kBufferSize = 8;
    s32 buffer_{0};
    s32 buffer_pos_{0};
    std::vector<u8> byte_array_;
};

/// H264 hardware parameter structures (offsets per Tegra X1 decode ring).
struct H264ParameterSet {
    s32 log2_max_pic_order_cnt_lsb_minus4;      ///< 0x00
    s32 delta_pic_order_always_zero_flag;       ///< 0x04
    s32 frame_mbs_only_flag;                    ///< 0x08
    u32 pic_width_in_mbs;                       ///< 0x0C
    u32 frame_height_in_map_units;              ///< 0x10
    u32 tile_format_gob_height;                 ///< 0x14 (bitfield on citron)
    u32 entropy_coding_mode_flag;               ///< 0x18
    s32 pic_order_present_flag;                 ///< 0x1C
    s32 num_refidx_l0_default_active;           ///< 0x20
    s32 num_refidx_l1_default_active;           ///< 0x24
    s32 deblocking_filter_control_present_flag; ///< 0x28
    s32 redundant_pic_cnt_present_flag;         ///< 0x2C
    u32 transform_8x8_mode_flag;                ///< 0x30
    u32 pitch_luma;                             ///< 0x34
    u32 pitch_chroma;                           ///< 0x38
    u32 luma_top_offset;                        ///< 0x3C
    u32 luma_bot_offset;                        ///< 0x40
    u32 luma_frame_offset;                      ///< 0x44
    u32 chroma_top_offset;                      ///< 0x48
    u32 chroma_bot_offset;                      ///< 0x4C
    u32 chroma_frame_offset;                    ///< 0x50
    u32 hist_buffer_size;                       ///< 0x54
    u64 parameter_flags;                        ///< 0x58 (citron's big BitField union)
};
static_assert(sizeof(H264ParameterSet) == 0x60, "H264ParameterSet size must match HW");

// Citron's union accessor helpers (parameter_flags bit positions preserved):
[[nodiscard]] inline u64 PsFlags(const H264ParameterSet& ps) noexcept {
    return ps.parameter_flags & 0xFFULL;
}
[[nodiscard]] inline u64 PsLog2MaxFrameNumMinus4(const H264ParameterSet& ps) noexcept {
    return (ps.parameter_flags >> 8) & 0xFULL;
}
[[nodiscard]] inline u64 PsChromaFormatIdc(const H264ParameterSet& ps) noexcept {
    return (ps.parameter_flags >> 12) & 0x3ULL;
}
[[nodiscard]] inline u64 PsPicOrderCntType(const H264ParameterSet& ps) noexcept {
    return (ps.parameter_flags >> 14) & 0x3ULL;
}
[[nodiscard]] inline s64 PsPicInitQpMinus26(const H264ParameterSet& ps) noexcept {
    const u64 raw = (ps.parameter_flags >> 16) & 0x1FULL;
    return static_cast<s64>(raw) - 12; // stored biased per citron BitField<s64>
}
[[nodiscard]] inline s64 PsChromaQpIndexOffset(const H264ParameterSet& ps) noexcept {
    const u64 raw = (ps.parameter_flags >> 22) & 0x1FULL;
    return static_cast<s64>(raw) - 16;
}
[[nodiscard]] inline s64 PsSecondChromaQpIndexOffset(const H264ParameterSet& ps) noexcept {
    const u64 raw = (ps.parameter_flags >> 27) & 0x1FULL;
    return static_cast<s64>(raw) - 16;
}
[[nodiscard]] inline u64 PsWeightedBipredIdc(const H264ParameterSet& ps) noexcept {
    return (ps.parameter_flags >> 32) & 0x3ULL;
}
[[nodiscard]] inline u64 PsFrameNumber(const H264ParameterSet& ps) noexcept {
    return (ps.parameter_flags >> 46) & 0xFFFFULL;
}

struct H264DecoderContext {
    std::array<u8, 0x48> padding0;         ///< 0x0000 (18 words)
    u32 stream_len;                        ///< 0x0048
    std::array<u8, 0xC> padding1;          ///< 0x004C (3 words)
    H264ParameterSet h264_parameter_set;   ///< 0x0058
    std::array<u8, 0x108> padding2;        ///< 0x00B8 (66 words)
    std::array<u8, 0x60> weight_scale;     ///< 0x01C0
    std::array<u8, 0x80> weight_scale_8x8; ///< 0x0220
};
static_assert(sizeof(H264DecoderContext) == 0x2A0, "H264DecoderContext size must match HW");

/// H264 composer: assembles Annex-B packets from guest decode registers.
class H264 {
public:
    explicit H264(memory::VirtualMemory* memory) : memory_(memory) {}
    ~H264() = default;

    /// Compose the H264 frame for host decoding.
    /// Returns the packet; *out_configuration_size = header size (0 when the
    /// frame is pass-through bitstream only).
    [[nodiscard]] bool ComposeFrame(const NvdecRegisters& regs,
                                    std::vector<u8>& out_frame,
                                    size_t* out_configuration_size,
                                    bool is_first_frame);

private:
    memory::VirtualMemory* memory_;
    std::vector<u8> frame_;
};

} // namespace nemu::core::gpu::nvhost::decoder
