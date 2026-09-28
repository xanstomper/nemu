// SPDX-FileCopyrightText: Ryujinx Team and Contributors
// SPDX-License-Identifier: MIT
//
// Ported from citron-neo/emulator (originally Ryujinx) h264.cpp, adapted to
// NEMU's memory model. Bit-exact header construction per the H.264 spec.

#include "h264.hpp"
#include "core/memory/virtual_memory.hpp"
#include "platform/logger.hpp"
#include <cstring>

namespace nemu::core::gpu::nvhost::decoder {

namespace {

// ZigZag LUTs from libavcodec (clause 9.1 reference tables).
constexpr std::array<u8, 64> kZigZagDirect{
    0,  1,  8,  16, 9,  2,  3,  10, 17, 24, 32, 25, 18, 11, 4,  5,  12, 19, 26, 33, 40, 48,
    41, 34, 27, 20, 13, 6,  7,  14, 21, 28, 35, 42, 49, 56, 57, 50, 43, 36, 29, 22, 15, 23,
    30, 37, 44, 51, 58, 59, 52, 45, 38, 31, 39, 46, 53, 60, 61, 54, 47, 55, 62, 63,
};

constexpr std::array<u8, 16> kZigZagScan{
    0 + 0 * 4, 1 + 0 * 4, 0 + 1 * 4, 0 + 2 * 4, 1 + 1 * 4, 2 + 0 * 4, 3 + 0 * 4, 2 + 1 * 4,
    1 + 2 * 4, 0 + 3 * 4, 1 + 3 * 4, 2 + 2 * 4, 3 + 1 * 4, 3 + 2 * 4, 2 + 3 * 4, 3 + 3 * 4,
};

} // namespace

void H264BitWriter::WriteBits(s32 value, s32 bit_count) {
    for (s32 i = 0; i < bit_count; ++i) {
        const u32 mask = 1U << static_cast<u32>(bit_count - 1 - i);
        const bool bit = (static_cast<u32>(value) & mask) != 0;
        if (bit) {
            buffer_ |= (1 << (kBufferSize - 1 - buffer_pos_));
        }
        buffer_pos_++;
        if (buffer_pos_ == kBufferSize) {
            Flush();
        }
    }
}

s32 H264BitWriter::GetFreeBufferBits() {
    return static_cast<s32>(kBufferSize) - buffer_pos_;
}

void H264BitWriter::Flush() {
    if (buffer_pos_ == 0) return;
    byte_array_.push_back(static_cast<u8>(buffer_));
    buffer_ = 0;
    buffer_pos_ = 0;
}

void H264BitWriter::WriteExpGolombCodedInt(s32 value) {
    const s32 preroll = value <= 0 ? (-2 * value) : (2 * value - 1);
    WriteExpGolombCodedUInt(static_cast<u32>(preroll));
}

void H264BitWriter::WriteExpGolombCodedUInt(u32 value) {
    value++;
    const u32 log = static_cast<u32>(std::bit_width(value)) - 1;
    for (u32 i = 0; i < log; i++) WriteBit(false);
    WriteBit(true);
    WriteBits(static_cast<s32>(value), static_cast<s32>(log));
}

void H264BitWriter::WriteU(s32 value, s32 value_sz) { WriteBits(value, value_sz); }
void H264BitWriter::WriteSe(s32 value) { WriteExpGolombCodedInt(value); }
void H264BitWriter::WriteUe(u32 value) { WriteExpGolombCodedUInt(value); }
void H264BitWriter::End() { WriteBit(true); Flush(); }
void H264BitWriter::WriteBit(bool state) { WriteBits(state ? 1 : 0, 1); }

void H264BitWriter::WriteScalingList(std::span<u8> scan, std::span<const u8> list,
                                     s32 start, s32 count) {
    if (scan.size() != static_cast<size_t>(count)) return;
    if (count == 16) {
        std::memcpy(scan.data(), kZigZagScan.data(), scan.size());
    } else {
        std::memcpy(scan.data(), kZigZagDirect.data(), scan.size());
    }
    u8 last_scale = 8;
    u8 next_scale = 8;
    for (size_t index = 0; index < scan.size(); index++) {
        const u8 curr_scale = list[static_cast<size_t>(start) + scan[index]];
        if (next_scale != curr_scale) {
            s32 delta_scale = static_cast<s32>(curr_scale) - static_cast<s32>(last_scale);
            WriteSe(delta_scale);
            last_scale = curr_scale;
            next_scale = curr_scale;
        }
    }
}

bool H264::ComposeFrame(const NvdecRegisters& regs, std::vector<u8>& out_frame,
                        size_t* out_configuration_size, bool is_first_frame) {
    if (!memory_) return false;

    H264DecoderContext context{};
    const u64 pic_info = regs.reg_array[NvdecRegisters::kRegPictureInfoOffset] >> 8;
    if (!memory_->ReadBlock(pic_info, &context, sizeof(context))) {
        NEMU_LOG_WARN("NVDEC.H264", "failed to read picture_info at 0x{:X}", pic_info);
        return false;
    }

    const u64 frame_number = PsFrameNumber(context.h264_parameter_set);
    const u64 bitstream_addr =
        regs.reg_array[NvdecRegisters::kRegFrameBitstreamOffset] >> 8;

    if (!is_first_frame && frame_number != 0) {
        // Pass-through frame: raw bitstream only.
        out_frame.resize(context.stream_len);
        if (!memory_->ReadBlock(bitstream_addr, out_frame.data(), out_frame.size())) {
            NEMU_LOG_WARN("NVDEC.H264", "failed to read bitstream at 0x{:X}", bitstream_addr);
            return false;
        }
        *out_configuration_size = 0;
        return true;
    }

    // ---- Encode SPS (header) ----
    H264BitWriter writer{};
    writer.WriteU(1, 24);  // start code prefix
    writer.WriteU(0, 1);   // forbidden zero
    writer.WriteU(3, 2);   // nal_ref_idc
    writer.WriteU(7, 5);   // nal_unit_type = 7 (SPS)
    writer.WriteU(100, 8); // profile_idc
    writer.WriteU(0, 8);   // constraint flags
    writer.WriteU(31, 8);  // level_idc
    writer.WriteUe(0);     // seq_parameter_set_id
    const u32 chroma_format_idc = static_cast<u32>(PsChromaFormatIdc(context.h264_parameter_set));
    writer.WriteUe(chroma_format_idc);
    if (chroma_format_idc == 3) writer.WriteBit(false); // separate_colour_plane_flag

    writer.WriteUe(0); // bit_depth_luma_minus8
    writer.WriteUe(0); // bit_depth_chroma_minus8
    writer.WriteBit(false); // qpprime_y_zero_transform_bypass_flag
    writer.WriteBit(false); // seq_scaling_matrix_present_flag

    writer.WriteUe(static_cast<u32>(PsLog2MaxFrameNumMinus4(context.h264_parameter_set)));
    const u32 order_cnt_type =
        static_cast<u32>(PsPicOrderCntType(context.h264_parameter_set));
    writer.WriteUe(order_cnt_type);
    if (order_cnt_type == 0) {
        writer.WriteUe(static_cast<u32>(context.h264_parameter_set.log2_max_pic_order_cnt_lsb_minus4));
    } else if (order_cnt_type == 1) {
        writer.WriteBit(context.h264_parameter_set.delta_pic_order_always_zero_flag != 0);
        writer.WriteSe(0);
        writer.WriteSe(0);
    }

    const u32 max_num_ref_frames =
        static_cast<u32>(context.h264_parameter_set.num_refidx_l0_default_active);
    writer.WriteUe(max_num_ref_frames);
    writer.WriteBit(false); // gaps_in_frame_num_value_allowed_flag
    writer.WriteUe(context.h264_parameter_set.pic_width_in_mbs - 1);
    writer.WriteUe(context.h264_parameter_set.frame_height_in_map_units - 1);
    writer.WriteBit(context.h264_parameter_set.frame_mbs_only_flag != 0);

    if (context.h264_parameter_set.frame_mbs_only_flag == 0) {
        writer.WriteBit((PsFlags(context.h264_parameter_set) & 1) != 0); // mbaff
    }
    writer.WriteBit((PsFlags(context.h264_parameter_set) & 2) != 0); // direct_8x8_inference
    writer.WriteBit(false); // frame_cropping_flag
    writer.WriteBit(false); // vui_parameters_present_flag
    writer.End();

    // ---- Encode PPS ----
    writer.WriteU(1, 24);  // start code prefix
    writer.WriteU(0, 1);   // forbidden zero
    writer.WriteU(3, 2);   // nal_ref_idc
    writer.WriteU(8, 5);   // nal_unit_type = 8 (PPS)
    writer.WriteUe(0);     // pic_parameter_set_id
    writer.WriteUe(0);     // seq_parameter_set_id

    writer.WriteBit(context.h264_parameter_set.entropy_coding_mode_flag != 0);
    writer.WriteBit(context.h264_parameter_set.pic_order_present_flag != 0);
    writer.WriteUe(0); // num_slice_groups_minus1
    writer.WriteUe(static_cast<u32>(context.h264_parameter_set.num_refidx_l0_default_active));
    writer.WriteUe(static_cast<u32>(context.h264_parameter_set.num_refidx_l1_default_active));
    writer.WriteBit((PsFlags(context.h264_parameter_set) & 4) != 0); // weighted_pred
    writer.WriteU(static_cast<s32>(PsWeightedBipredIdc(context.h264_parameter_set)), 2);
    const s32 pic_init_qp =
        static_cast<s32>(PsPicInitQpMinus26(context.h264_parameter_set));
    writer.WriteSe(pic_init_qp);
    writer.WriteSe(0); // pic_init_qs_minus26
    const s32 chroma_qp_index_offset =
        static_cast<s32>(PsChromaQpIndexOffset(context.h264_parameter_set));
    writer.WriteSe(chroma_qp_index_offset);
    writer.WriteBit(context.h264_parameter_set.deblocking_filter_control_present_flag != 0);
    writer.WriteBit((PsFlags(context.h264_parameter_set) & 8) != 0); // constrained_intra_pred
    writer.WriteBit(context.h264_parameter_set.redundant_pic_cnt_present_flag != 0);
    writer.WriteBit(context.h264_parameter_set.transform_8x8_mode_flag != 0);

    writer.WriteBit(true); // pic_scaling_matrix_present_flag
    std::array<u8, 64> scan{};
    for (s32 index = 0; index < 6; index++) {
        writer.WriteBit(true);
        writer.WriteScalingList(std::span<u8>(scan.data(), 16),
                                context.weight_scale, index * 16, 16);
    }
    if (context.h264_parameter_set.transform_8x8_mode_flag != 0) {
        for (s32 index = 0; index < 2; index++) {
            writer.WriteBit(true);
            writer.WriteScalingList(std::span<u8>(scan.data(), 64),
                                    context.weight_scale_8x8, index * 64, 64);
        }
    }
    writer.WriteSe(static_cast<s32>(PsSecondChromaQpIndexOffset(context.h264_parameter_set)));
    writer.End();

    // ---- Assemble header + bitstream ----
    const auto& encoded_header = writer.GetByteArray();
    out_frame.resize(encoded_header.size() + context.stream_len);
    std::memcpy(out_frame.data(), encoded_header.data(), encoded_header.size());
    if (context.stream_len > 0) {
        if (!memory_->ReadBlock(bitstream_addr,
                                out_frame.data() + encoded_header.size(),
                                context.stream_len)) {
            NEMU_LOG_WARN("NVDEC.H264", "failed to read bitstream at 0x{:X}", bitstream_addr);
            return false;
        }
    }
    *out_configuration_size = encoded_header.size();
    return true;
}

} // namespace nemu::core::gpu::nvhost::decoder
