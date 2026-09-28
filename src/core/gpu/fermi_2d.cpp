#include "fermi_2d.hpp"
#include "core/memory/virtual_memory.hpp"
#include "platform/logger.hpp"
#include <vector>
#include <cstring>

namespace nemu::core::gpu {

Fermi2D::Fermi2D(memory::VirtualMemory* memory) : memory_(memory) {}

void Fermi2D::Reset() {
    operation_ = 0;
    src_format_ = dst_format_ = 0;
    src_width_ = src_height_ = src_pitch_ = 0;
    dst_width_ = dst_height_ = dst_pitch_ = 0;
    src_layer_addr_ = dst_layer_addr_ = 0;
    src_addr_ = dst_addr_ = 0;
    src_x0_ = src_y0_ = 0;
    du_dx_ = dv_dy_ = 0;
    dst_x0_ = dst_y0_ = dst_width_ = dst_height_ = 0;
}

void Fermi2D::CallMethod(u32 method, u32 argument) {
    switch (method) {
    case REG_OPERATION: operation_ = argument; return;
    case REG_SURFACE_SRC: src_format_ = argument; return;
    case REG_SURFACE_SRC_PITCH: src_pitch_ = argument; return;
    case REG_SURFACE_SRC_WIDTH: src_width_ = argument; return;
    case REG_SURFACE_SRC_HEIGHT: src_height_ = argument; return;
    case REG_SURFACE_DST: dst_format_ = argument; return;
    case REG_SURFACE_DST_PITCH: dst_pitch_ = argument; return;
    case REG_SURFACE_DST_WIDTH: dst_width_ = argument; return;
    case REG_SURFACE_DST_HEIGHT: dst_height_ = argument; return;
    case REG_PFM_SRC_ADDRESS:
        src_addr_ = (src_addr_ & 0xFFFFFFFF00000000ULL) | argument; return;
    case REG_PFM_SRC_ADDRESS_U:
        src_addr_ = (src_addr_ & 0x00000000FFFFFFFFULL) | (static_cast<u64>(argument) << 32); return;
    case REG_PFM_DST_ADDRESS:
        dst_addr_ = (dst_addr_ & 0xFFFFFFFF00000000ULL) | argument; return;
    case REG_PFM_DST_ADDRESS_U:
        dst_addr_ = (dst_addr_ & 0x00000000FFFFFFFFULL) | (static_cast<u64>(argument) << 32); return;
    case REG_PFM_SRC_X0: src_x0_ = static_cast<s64>(argument) << 32; return;   // 32.32 fixed
    case REG_PFM_SRC_Y0: src_y0_ = static_cast<s64>(argument) << 32; return;
    case REG_PFM_DST_X0: dst_x0_ = argument; return;
    case REG_PFM_DST_Y0: dst_y0_ = argument; return;
    case REG_PFM_DST_WIDTH: dst_width_ = argument; return;
    case REG_PFM_DST_HEIGHT: dst_height_ = argument; return;
    case REG_PFM_DU_DX: du_dx_ = static_cast<s64>(argument); return;
    case REG_PFM_DV_DY: dv_dy_ = static_cast<s64>(argument); return;
    case REG_BLIT_TRIGGER:
        if (argument == 1) Blit();
        return;
    default: return;
    }
}

void Fermi2D::Blit() {
    if (operation_ != 0) {
        // yuzu: UNIMPLEMENTED_IF(operation != SrcCopy). Compositing ops are rare;
        // auditable skip rather than wrong output.
        NEMU_LOG_WARN("Fermi2D", "non-SrcCopy operation 0x{:X} skipped", operation_);
        return;
    }
    if (dst_width_ == 0 || dst_height_ == 0 || src_addr_ == 0 || dst_addr_ == 0) {
        NEMU_LOG_WARN("Fermi2D", "blit with degenerate parameters skipped");
        return;
    }

    const size_t bpp = BytesPerPixel(static_cast<Format>(src_format_));
    // Default sampling steps when the guest passes null derivatives (yuzu:
    // null_derivative = 1<<32 → 1 src px per dst px).
    const s64 step_x = (du_dx_ != 0) ? du_dx_ : (1LL << 32);
    const s64 step_y = (dv_dy_ != 0) ? dv_dy_ : (1LL << 32);

    // Read the whole source pitch region needed for the sample window.
    const s64 max_sx = src_x0_ + (step_x * static_cast<s64>(dst_width_ - 1) >> 32) + 1;
    const s64 max_sy = src_y0_ + (step_y * static_cast<s64>(dst_height_ - 1) >> 32) + 1;
    const size_t src_span_bytes =
        static_cast<size_t>(max_sy) * src_pitch_ + static_cast<size_t>(max_sx) * bpp;
    std::vector<u8> src_buf(src_span_bytes);
    if (!memory_->ReadBlock(src_addr_, src_buf.data(), src_span_bytes)) {
        NEMU_LOG_WARN("Fermi2D", "blit source unreadable at 0x{:X}", src_addr_);
        return;
    }

    // Nearest-neighbour scale into the destination (SrcCopy semantics).
    std::vector<u8> dst_line(static_cast<size_t>(dst_width_) * bpp);
    for (u32 dy = 0; dy < dst_height_; ++dy) {
        const s64 sy = src_y0_ + ((step_y * static_cast<s64>(dy)) >> 32);
        for (u32 dx = 0; dx < dst_width_; ++dx) {
            const s64 sx = src_x0_ + ((step_x * static_cast<s64>(dx)) >> 32);
            const size_t src_off = static_cast<size_t>(sy) * src_pitch_
                                 + static_cast<size_t>(sx) * bpp;
            std::memcpy(dst_line.data() + static_cast<size_t>(dx) * bpp,
                        src_buf.data() + src_off, bpp);
        }
        memory_->WriteBlock(dst_addr_ + static_cast<u64>(dy) * dst_pitch_
                                       + static_cast<u64>(dst_x0_) * bpp,
                            dst_line.data(), dst_line.size());
    }
    ++blit_count_;
}

} // namespace nemu::core::gpu
