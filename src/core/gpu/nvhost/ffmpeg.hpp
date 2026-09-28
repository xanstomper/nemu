// SPDX-FileCopyrightText: Copyright 2020 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later
//
// Ported from citron-neo/emulator src/video_core/host1x/ffmpeg/ffmpeg.cpp/.h,
// adapted to NEMU: minimal decode-API surface (init per codec, send packet,
// receive NV12 frames) guarded behind NEMU_FFMPEG.

#pragma once

#include "core/types.hpp"
#include "nvdec_common.hpp"
#include <memory>
#include <vector>
#include <span>

#if NEMU_FFMPEG
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/avutil.h>
}

#include <cstring>
#endif

namespace nemu::core::gpu::nvhost::ffmpeg {

/// One decoded output frame in NV12 layout (VIC presentation format).
struct Frame {
    u32 width{0};
    u32 height{0};
    std::vector<u8> data; // NV12: Y plane then interleaved UV
};

/// Host-side decoder for one codec instance.
class DecodeApi {
public:
    DecodeApi() = default;
    ~DecodeApi();

    DecodeApi(const DecodeApi&) = delete;
    DecodeApi& operator=(const DecodeApi&) = delete;

    /// Initialize for the given codec. Returns false when unsupported.
    [[nodiscard]] bool Initialize(VideoCodec codec);

    /// Current codec this API was initialized for.
    [[nodiscard]] VideoCodec GetCodec() const noexcept { return codec_; }

    /// Send a composed bitstream packet to the decoder.
    [[nodiscard]] bool SendPacket(std::span<const u8> data, size_t configuration_size);

    /// Drain decoded frames (NV12).
    void ReceiveFrames(std::vector<Frame>& out_frames);

private:
    VideoCodec codec_{VideoCodec::None};
#if NEMU_FFMPEG
    const AVCodec* av_codec_{nullptr};
    AVCodecContext* av_context_{nullptr};
    AVCodecParserContext* av_parser_{nullptr};
    AVFrame* av_frame_{nullptr};
#endif
};

} // namespace nemu::core::gpu::nvhost::ffmpeg
