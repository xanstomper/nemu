#include "engine_upload.hpp"
#include "gmmu.hpp"
#include "platform/logger.hpp"
#include <cstring>

namespace nemu::core::gpu {

void EngineUpload::SetReg(u32 method, u32 argument) noexcept {
    switch (method) {
    case REG_LINE_LENGTH_IN: line_length_in_ = argument; return;
    case REG_LINE_COUNT: line_count_ = argument; return;
    case REG_DST_ADDR_HIGH: dst_addr_high_ = argument; return;
    case REG_DST_ADDR_LOW: dst_addr_low_ = argument; return;
    case REG_DST_PITCH: dst_pitch_ = argument; return;
    case REG_DST_BLOCK_SIZE: dst_block_height_ = (argument >> 4) & 0xF; return;
    case REG_DST_WIDTH: dst_width_ = argument; return;
    case REG_DST_HEIGHT: dst_height_ = argument; return;
    case REG_DST_X: dst_x_ = argument; return;
    case REG_DST_Y: dst_y_ = argument; return;
    default: return;
    }
}

u32 EngineUpload::GetReg(u32 method) const noexcept {
    switch (method) {
    case REG_LINE_LENGTH_IN: return line_length_in_;
    case REG_LINE_COUNT: return line_count_;
    case REG_DST_ADDR_HIGH: return dst_addr_high_;
    case REG_DST_ADDR_LOW: return dst_addr_low_;
    default: return 0;
    }
}

void EngineUpload::ProcessExec(bool is_linear) noexcept {
    write_offset_ = 0;
    copy_size_ = line_length_in_ * line_count_;
    inner_buffer_.resize(copy_size_);
    is_linear_ = is_linear;
}

void EngineUpload::ProcessData(u32 data, bool is_last_call) noexcept {
    if (copy_size_ == 0 || write_offset_ >= copy_size_) {
        return; // exec not launched or cursor past end (yuzu asserts; we guard)
    }
    const u32 sub = (copy_size_ - write_offset_ < 4u) ? copy_size_ - write_offset_ : 4u;
    std::memcpy(inner_buffer_.data() + write_offset_, &data, sub);
    write_offset_ += sub;
    if (!is_last_call) {
        return;
    }
    Flush();
}

void EngineUpload::ProcessData(std::span<const u32> data) noexcept {
    const size_t bytes = data.size() * sizeof(u32);
    if (copy_size_ == 0 || write_offset_ + bytes > copy_size_) {
        return;
    }
    std::memcpy(inner_buffer_.data() + write_offset_, data.data(), bytes);
    write_offset_ += static_cast<u32>(bytes);
    Flush();
}

void EngineUpload::Flush() noexcept {
    if (inner_buffer_.empty() || memory_ == nullptr) {
        write_offset_ = 0;
        return;
    }
    const u64 address = DestAddress();
    if (is_linear_) {
        // Pitch layout: one linear write per line at address + line*pitch.
        for (u32 line = 0; line < line_count_; ++line) {
            const u64 dest_line = address + static_cast<u64>(line) * dst_pitch_;
            const u8* src = inner_buffer_.data() + static_cast<size_t>(line) * line_length_in_;
            memory_->Write(dest_line, src, line_length_in_);
        }
    } else {
        // Block-linear: games uploading to a swizzled surface via inline data.
        // yuzu routes through SwizzleSubrect; NEMU's equivalent is a linear
        // fallback write of the subrect rows at width-stride (conservative:
        // correct for pitch-compatible surfaces; block-linear targets hit the
        // texture cache invalidation path instead).
        const u32 width = dst_width_ ? dst_width_ : line_length_in_;
        (void)dst_block_height_; // subrect swizzle handled by texture cache
        for (u32 line = 0; line < line_count_; ++line) {
            const u64 dest_line = address + static_cast<u64>(dst_y_ + line) * width
                                + dst_x_;
            const u8* src = inner_buffer_.data() + static_cast<size_t>(line) * line_length_in_;
            memory_->Write(dest_line, src, line_length_in_);
        }
    }
    write_offset_ = 0;
}

} // namespace nemu::core::gpu
