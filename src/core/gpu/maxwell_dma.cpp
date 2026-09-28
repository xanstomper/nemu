#include "maxwell_dma.hpp"
#include "core/memory/virtual_memory.hpp"
#include "core/gpu/deswizzle.hpp"
#include "platform/logger.hpp"
#include <vector>

namespace nemu::core::gpu {

MaxwellDma::MaxwellDma(memory::VirtualMemory* memory) : memory_(memory) {}

void MaxwellDma::Reset() {
    line_length_in_ = 0;
    line_count_ = 0;
    offset_in_ = 0;
    offset_out_ = 0;
    pitch_in_ = 0;
    pitch_out_ = 0;
    launch_ = {};
    dst_width_ = dst_height_ = dst_block_height_gobs_ = 0;
    src_width_ = src_height_ = src_block_height_gobs_ = 0;
}

void MaxwellDma::CallMethod(u32 method, u32 argument) {
    switch (method) {
    case REG_LINE_LENGTH_IN: line_length_in_ = argument; return;
    case REG_LINE_COUNT: line_count_ = argument; return;
    case REG_OFFSET_IN:
        // Lower 32 bits; upper 32 latch at method+1 (yuzu pairs offset regs).
        offset_in_ = (offset_in_ & 0xFFFFFFFF00000000ULL) | argument;
        return;
    case REG_OFFSET_IN + 1:
        offset_in_ = (offset_in_ & 0x00000000FFFFFFFFULL) | (static_cast<u64>(argument) << 32);
        return;
    case REG_OFFSET_OUT:
        offset_out_ = (offset_out_ & 0xFFFFFFFF00000000ULL) | argument;
        return;
    case REG_OFFSET_OUT + 1:
        offset_out_ = (offset_out_ & 0x00000000FFFFFFFFULL) | (static_cast<u64>(argument) << 32);
        return;
    case REG_PITCH_IN: pitch_in_ = argument; return;
    case REG_PITCH_OUT: pitch_out_ = argument; return;
    case REG_DST_PARAMS_WIDTH: dst_width_ = argument; return;
    case REG_DST_PARAMS_HEIGHT: dst_height_ = argument; return;
    case REG_DST_PARAMS_BLOCK_SIZE:
        // yuzu block_size: bits [7:4] = block height (gobs, log2).
        dst_block_height_gobs_ = 1u << ((argument >> 4) & 0xF);
        return;
    case REG_SRC_PARAMS_WIDTH: src_width_ = argument; return;
    case REG_SRC_PARAMS_HEIGHT: src_height_ = argument; return;
    case REG_SRC_PARAMS_BLOCK_SIZE:
        src_block_height_gobs_ = 1u << ((argument >> 4) & 0xF);
        return;
    case REG_LAUNCH:
        launch_.raw = argument;
        DoLaunch(); // launch register write triggers the copy (yuzu: is_last_call)
        return;
    default:
        return; // uninteresting method for HLE copies
    }
}

void MaxwellDma::DoLaunch() {
    if (line_length_in_ == 0 || line_count_ == 0) {
        NEMU_LOG_WARN("MaxwellDMA", "Launch with zero extent (len={} lines={})",
                      line_length_in_, line_count_);
        return;
    }

    const bool src_pitch = launch_.SrcIsPitch();
    const bool dst_pitch = launch_.DstIsPitch();

    if (src_pitch && dst_pitch) {
        CopyPitchToPitch();
    } else if (src_pitch && !dst_pitch) {
        CopyPitchToBlockLinear();
    } else if (!src_pitch && dst_pitch) {
        CopyBlockLinearToPitch();
    } else {
        CopyBlockLinearToBlockLinear();
    }
    ++copy_count_;
}

void MaxwellDma::CopyPitchToPitch() {
    // Fast path: contiguous byte copy of line_length * line_count honoring pitches.
    if (line_count_ == 1 || (pitch_in_ == line_length_in_ && pitch_out_ == line_length_in_)) {
        std::vector<u8> buf(static_cast<size_t>(line_length_in_) * line_count_);
        if (memory_->ReadBlock(offset_in_, buf.data(), buf.size())) {
            memory_->WriteBlock(offset_out_, buf.data(), buf.size());
        }
        return;
    }
    std::vector<u8> line(line_length_in_);
    for (u32 l = 0; l < line_count_; ++l) {
        if (memory_->ReadBlock(offset_in_ + static_cast<u64>(l) * pitch_in_,
                               line.data(), line.size())) {
            memory_->WriteBlock(offset_out_ + static_cast<u64>(l) * pitch_out_,
                                line.data(), line.size());
        }
    }
}

void MaxwellDma::CopyPitchToBlockLinear() {
    // Linear source -> GOB-swizzled destination. Uses the latched dst_params
    // surface description (width/height/block height) for a true swizzle; falls
    // back to a layout-compatible stream when the guest didn't program them.
    const u32 width = dst_width_ ? dst_width_ : line_length_in_;
    const u32 height = dst_height_ ? dst_height_ : line_count_;
    const u32 bh = dst_block_height_gobs_ ? dst_block_height_gobs_ : 1;

    std::vector<u8> linear(static_cast<size_t>(width) * height);
    for (u32 l = 0; l < height && l < line_count_; ++l) {
        memory_->ReadBlock(offset_in_ + static_cast<u64>(l) * pitch_in_,
                           linear.data() + static_cast<size_t>(l) * width, width);
    }
    std::vector<u8> swizzled(linear.size());
    if (TextureSwizzler::SwizzleBlockLinear(linear, swizzled, width, height, 1, bh)) {
        memory_->WriteBlock(offset_out_, swizzled.data(), swizzled.size());
    } else {
        memory_->WriteBlock(offset_out_, linear.data(), linear.size());
    }
}

void MaxwellDma::CopyBlockLinearToPitch() {
    // GOB-swizzled source -> linear destination (true deswizzle via dst_params/
    // src_params surface description; streaming fallback when not programmed).
    const u32 width = src_width_ ? src_width_ : line_length_in_;
    const u32 height = src_height_ ? src_height_ : line_count_;
    const u32 bh = src_block_height_gobs_ ? src_block_height_gobs_ : 1;

    std::vector<u8> swizzled(static_cast<size_t>(width) * height);
    if (!memory_->ReadBlock(offset_in_, swizzled.data(), swizzled.size())) {
        return;
    }
    std::vector<u8> linear(swizzled.size());
    if (TextureSwizzler::DeswizzleBlockLinear(swizzled, linear, width, height, 1, bh)) {
        for (u32 l = 0; l < height; ++l) {
            memory_->WriteBlock(offset_out_ + static_cast<u64>(l) * pitch_out_,
                                linear.data() + static_cast<size_t>(l) * width, width);
        }
    } else {
        for (u32 l = 0; l < line_count_; ++l) {
            memory_->WriteBlock(offset_out_ + static_cast<u64>(l) * pitch_out_,
                                swizzled.data() + static_cast<size_t>(l) * line_length_in_,
                                line_length_in_);
        }
    }
}

void MaxwellDma::CopyBlockLinearToBlockLinear() {
    std::vector<u8> buf(static_cast<size_t>(line_length_in_) * line_count_);
    if (memory_->ReadBlock(offset_in_, buf.data(), buf.size())) {
        memory_->WriteBlock(offset_out_, buf.data(), buf.size());
    }
}

} // namespace nemu::core::gpu
