#include "maxwell_dma.hpp"
#include "core/memory/virtual_memory.hpp"
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
    // Read the linear source, stream to the destination. Full GOB swizzling needs
    // the destination block dims (remap/block-dim registers); the dominant game
    // use is compatible-layout streaming, which a linear copy serves.
    std::vector<u8> linear(static_cast<size_t>(line_length_in_) * line_count_);
    for (u32 l = 0; l < line_count_; ++l) {
        memory_->ReadBlock(offset_in_ + static_cast<u64>(l) * pitch_in_,
                           linear.data() + static_cast<size_t>(l) * line_length_in_,
                           line_length_in_);
    }
    memory_->WriteBlock(offset_out_, linear.data(), linear.size());
}

void MaxwellDma::CopyBlockLinearToPitch() {
    std::vector<u8> linear(static_cast<size_t>(line_length_in_) * line_count_);
    // See CopyPitchToBlockLinear note: layout-compatible streaming copy.
    if (memory_->ReadBlock(offset_in_, linear.data(), linear.size())) {
        for (u32 l = 0; l < line_count_; ++l) {
            memory_->WriteBlock(offset_out_ + static_cast<u64>(l) * pitch_out_,
                                linear.data() + static_cast<size_t>(l) * line_length_in_,
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
