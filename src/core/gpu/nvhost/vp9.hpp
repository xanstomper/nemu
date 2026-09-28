// SPDX-FileCopyrightText: Copyright 2021 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later
//
// Ported from citron-neo/emulator src/video_core/host1x/codecs/vp9.h and
// vp9.cpp (with vp9_types.h), adapted to NEMU:
//  - Common::Stream -> NemuStream (minimal byte-stream with the same interface)
//  - Common::ScratchBuffer -> std::vector
//  - GMMU().ReadBlock -> memory::VirtualMemory::ReadBlock
// The range encoder + bit writer + probability-update machinery is ported
// bit-exact from the upstream implementation.

#pragma once

#include "nvdec_common.hpp"
#include "vp9_types.h"
#include "core/types.hpp"
#include <array>
#include <cstddef>
#include <span>
#include <string>
#include <vector>

namespace nemu::core::memory {
class VirtualMemory;
}

namespace nemu::core::gpu::nvhost::decoder {

/// Minimal replacement for Common::Stream over a growable byte buffer.
class NemuStream {
public:
    void Write(u8 value) { buffer_.push_back(value); }
    [[nodiscard]] const std::vector<u8>& GetBuffer() const noexcept { return buffer_; }
    void Seek(std::size_t pos) { cursor_ = pos; }
    void SeekSet(std::size_t pos) { cursor_ = pos; }
    void SeekRel(std::ptrdiff_t delta) { cursor_ = static_cast<std::size_t>(static_cast<std::ptrdiff_t>(cursor_) + delta); }
    [[nodiscard]] u8 ReadByte() noexcept { return buffer_.empty() ? u8{0} : buffer_[cursor_++ % buffer_.size()]; }
    [[nodiscard]] std::size_t Tell() const noexcept { return cursor_; }
private:
    std::vector<u8> buffer_;
    std::size_t cursor_{0};
};

/// The VpxRangeEncoder and VpxBitStreamWriter classes compose the VP9
/// compressed headers (compressed header + probability updates + tile data).
class VpxRangeEncoder {
public:
    VpxRangeEncoder();
    ~VpxRangeEncoder();

    void Write(s32 value, s32 value_size);
    void Write(bool bit);
    void Write(bool bit, s32 probability);
    void End();
    [[nodiscard]] const std::vector<u8>& GetBuffer() const noexcept;

private:
    u8 PeekByte();
    void WriteU(u8 value);
    void WriteS(s32 value);
    void WriteDeltaQ(u32 value);
    void WriteBit(bool state);
    void Flush();
    void WriteBits(s64 value, s32 bit_count);
    s32 GetFreeBufferBits();

    NemuStream base_stream_{};
    u32 low_value_{0};
    u32 range_{0xff};
    s32 count_{-24};
    s32 half_probability_{128};
};

class VpxBitStreamWriter {
public:
    static constexpr s32 kStreamBufferSize = 8;
    VpxBitStreamWriter();
    ~VpxBitStreamWriter();

    void WriteBit(bool state);
    void WriteU(u32 value, u32 value_size);
    void WriteU(s32 value, u32 value_size) { WriteU(static_cast<u32>(value), value_size); }
    void WriteS(s32 value, u32 value_size);
    void WriteDeltaQ(u32 value);
    void WriteU64(u64 value, s32 bit_count);
    void WriteBits(u32 value, u32 bit_count);
    [[nodiscard]] s32 GetFreeBufferBits();
    void Flush();
    [[nodiscard]] std::vector<u8>& GetByteArray() noexcept { return out_buffer_; }
    [[nodiscard]] const std::vector<u8>& GetByteArray() const noexcept { return out_buffer_; }

private:
    s32 buffer_{0};
    u32 buffer_pos_{0};
    std::vector<u8> out_buffer_{};
};

/// VP9 composer.
class VP9 {
public:
    explicit VP9(memory::VirtualMemory* memory);
    ~VP9();

    VP9(const VP9&) = delete;
    VP9& operator=(const VP9&) = delete;

    /// Composes the VP9 frame from the GPU state information,
    /// based on the official VP9 spec documentation.
    void ComposeFrame(const NvdecRegisters& state);

    /// Returns true if the most recent frame was a hidden frame.
    [[nodiscard]] bool WasFrameHidden() const noexcept { return !current_frame_info_.show_frame; }

    /// Returns a const span to the composed frame data.
    [[nodiscard]] std::span<const u8> GetFrameBytes() const noexcept { return frame_; }

private:
    template <typename T, std::size_t N>
    void WriteProbabilityUpdate(VpxRangeEncoder& writer,
                                const std::array<T, N>& new_prob,
                                const std::array<T, N>& old_prob);
    void WriteProbabilityUpdate(VpxRangeEncoder& writer, u8 new_prob, u8 old_prob);
    void WriteProbabilityDelta(VpxRangeEncoder& writer, u8 new_prob, u8 old_prob);
    template <typename T, std::size_t N>
    void WriteProbabilityDelta(VpxRangeEncoder& writer,
                               const std::array<T, N>& new_prob,
                               const std::array<T, N>& old_prob);
    /// Inverse of 6.3.4 Decode term subexp
    void EncodeTermSubExp(VpxRangeEncoder& writer, s32 value);
    /// Writes if the value is less than the test value
    bool WriteLessThan(VpxRangeEncoder& writer, s32 value, s32 test);
    /// Writes probability updates for the Coef probabilities
    void WriteCoefProbabilityUpdate(VpxRangeEncoder& writer, s32 tx_mode,
                                    const std::array<u8, 1728>& new_prob,
                                    const std::array<u8, 1728>& old_prob);
    /// Write probabilities for 4-byte aligned structures
    template <typename T, std::size_t N>
    void WriteProbabilityUpdateAligned4(VpxRangeEncoder& writer,
                                        const std::array<T, N>& new_prob,
                                        const std::array<T, N>& old_prob);
    /// Write motion vector probability updates. 6.3.17 in the spec
    void WriteMvProbabilityUpdate(VpxRangeEncoder& writer, s32 new_prob, s32 old_prob);
    /// Returns VP9 information from NVDEC provided offset and size
    [[nodiscard]] Vp9PictureInfo GetVp9PictureInfo(const NvdecRegisters& state);
    /// Read and convert NVDEC provided entropy probs to Vp9EntropyProbs struct
    void InsertEntropy(u64 offset, Vp9EntropyProbs& dst);
    /// Returns frame to be decoded after buffering
    [[nodiscard]] Vp9FrameContainer GetCurrentFrame(const NvdecRegisters& state);
    /// Use NVDEC provided information to compose the headers for the current frame
    [[nodiscard]] std::vector<u8> ComposeCompressedHeader();
    [[nodiscard]] VpxBitStreamWriter ComposeUncompressedHeader();

    Vp9PictureInfo current_frame_info_{};

    memory::VirtualMemory* memory_;
    std::vector<u8> frame_;

    Vp9FrameContainer next_frame_{};
    std::array<Vp9EntropyProbs, 4> frame_ctxs_{};
    std::array<s8, 4> loop_filter_ref_deltas_{};
    std::array<s8, 2> loop_filter_mode_deltas_{};
    bool swap_ref_indices_{false};
    Vp9EntropyProbs prev_frame_probs_{};
};

} // namespace nemu::core::gpu::nvhost::decoder
