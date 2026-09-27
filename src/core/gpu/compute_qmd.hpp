#pragma once

#include "core/types.hpp"
#include <array>
#include <cstddef>
#include <cstring>
#include <span>

namespace nemu::core::gpu {

// ---------------------------------------------------------------------------
// ComputeQmd — Kepler / Maxwell Queue Meta Descriptor (QMD) (Tier-A3).
//
// The Maxwell GPU launches compute workloads via method 0xAF (Launch) which
// reads a 256-byte (64 x 32-bit words) QMD struct from the GMMU address
// specified in launch_desc_loc (method 0xAD << 8).
//
// Documented in Yuzu (kepler_compute.h / LaunchParams, GPL-2.0-or-later) and
// Ryujinx (ComputeQmd.cs / ComputeClass.cs, MIT).
// ---------------------------------------------------------------------------
struct ComputeQmd {
    static constexpr size_t kWordCount = 64;
    static constexpr size_t kByteSize = kWordCount * sizeof(u32); // 256 bytes
    static constexpr size_t kMaxConstantBuffers = 8;

    std::array<u32, kWordCount> words{};

    /// Populate QMD from raw bytes (e.g. read from GMMU).
    static ComputeQmd FromBytes(std::span<const u8> bytes) noexcept {
        ComputeQmd qmd{};
        const size_t copy_len = std::min(bytes.size(), kByteSize);
        std::memcpy(qmd.words.data(), bytes.data(), copy_len);
        return qmd;
    }

    /// Offset from the shader code base address to the compute program entry.
    [[nodiscard]] u32 ProgramOffset() const noexcept {
        return words[8];
    }

    /// Grid dimensions (number of workgroups / blocks).
    [[nodiscard]] u32 GridDimX() const noexcept {
        return std::max<u32>(1, words[12] & 0x7FFFFFFF);
    }

    [[nodiscard]] u32 GridDimY() const noexcept {
        return std::max<u32>(1, words[13] & 0xFFFF);
    }

    [[nodiscard]] u32 GridDimZ() const noexcept {
        return std::max<u32>(1, (words[13] >> 16) & 0xFFFF);
    }

    /// Workgroup dimensions (threads per block).
    [[nodiscard]] u32 BlockDimX() const noexcept {
        const u32 val = (words[18] >> 16) & 0xFFFF;
        return std::max<u32>(1, val);
    }

    [[nodiscard]] u32 BlockDimY() const noexcept {
        const u32 val = words[19] & 0xFFFF;
        return std::max<u32>(1, val);
    }

    [[nodiscard]] u32 BlockDimZ() const noexcept {
        const u32 val = (words[19] >> 16) & 0xFFFF;
        return std::max<u32>(1, val);
    }

    /// Thread dimensions alias (matches Ryujinx CtaThreadDimension names).
    [[nodiscard]] u32 CtaThreadDimension0() const noexcept { return BlockDimX(); }
    [[nodiscard]] u32 CtaThreadDimension1() const noexcept { return BlockDimY(); }
    [[nodiscard]] u32 CtaThreadDimension2() const noexcept { return BlockDimZ(); }

    /// Shared memory allocation size in bytes.
    [[nodiscard]] u32 SharedMemorySize() const noexcept {
        return words[17] & 0x3FFFF; // bits [17:0]
    }

    /// Bitmask indicating which constant buffers are enabled (bits 0..7).
    [[nodiscard]] u8 ConstantBufferEnableMask() const noexcept {
        return static_cast<u8>(words[20] & 0xFF);
    }

    [[nodiscard]] bool ConstantBufferValid(size_t index) const noexcept {
        if (index >= kMaxConstantBuffers) return false;
        return (ConstantBufferEnableMask() & (1u << index)) != 0;
    }

    /// 64-bit GPU virtual address for the given constant buffer slot (0..7).
    [[nodiscard]] u64 ConstantBufferAddress(size_t index) const noexcept {
        if (index >= kMaxConstantBuffers) return 0;
        const size_t word_idx = 29 + (index * 2);
        const u32 addr_low = words[word_idx];
        const u32 addr_high = words[word_idx + 1] & 0xFF;
        return (static_cast<u64>(addr_high) << 32) | static_cast<u64>(addr_low);
    }

    /// Size in bytes for the given constant buffer slot (0..7).
    [[nodiscard]] u32 ConstantBufferSize(size_t index) const noexcept {
        if (index >= kMaxConstantBuffers) return 0;
        const size_t word_idx = 29 + (index * 2);
        return (words[word_idx + 1] >> 15) & 0x1FFFF;
    }
};

} // namespace nemu::core::gpu
