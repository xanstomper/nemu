// SPDX-FileCopyrightText: Copyright 2020 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later
//
// Ported from citron-neo/emulator src/video_core/host1x/ffmpeg/ffmpeg.cpp,
// adapted to NEMU's NV12-only presentation contract.

#include "ffmpeg.hpp"
#include "platform/logger.hpp"
#include <span>
#include <cstring>

namespace nemu::core::gpu::nvhost::ffmpeg {

#if NEMU_FFMPEG
DecodeApi::~DecodeApi() {
    if (av_parser_) av_parser_close(av_parser_);
    if (av_frame_) av_frame_free(&av_frame_);
    if (av_context_) {
        avcodec_free_context(&av_context_);
    }
}

bool DecodeApi::Initialize(VideoCodec codec) {
    codec_ = codec;
    switch (codec) {
    case VideoCodec::H264:
        av_codec_ = avcodec_find_decoder(AV_CODEC_ID_H264);
        break;
    case VideoCodec::VP8:
        av_codec_ = avcodec_find_decoder(AV_CODEC_ID_VP8);
        break;
    case VideoCodec::VP9:
        av_codec_ = avcodec_find_decoder(AV_CODEC_ID_VP9);
        break;
    case VideoCodec::H265:
        av_codec_ = avcodec_find_decoder(AV_CODEC_ID_HEVC);
        break;
    default:
        return false;
    }
    if (!av_codec_) {
        NEMU_LOG_WARN("NVDEC.FFmpeg", "decoder for codec {} not available", VideoCodecName(codec));
        return false;
    }

    av_context_ = avcodec_alloc_context3(av_codec_);
    if (!av_context_) return false;
    av_context_->flags |= AV_CODEC_FLAG_OUTPUT_CORRUPT; // games tolerate late frames
    av_context_->flags2 |= AV_CODEC_FLAG2_FAST;

    av_parser_ = av_parser_init(av_codec_->id);
    if (av_parser_) {
        // Annex-B assembly is done by NEMU composers; parser only splits.
        av_parser_->flags |= PARSER_FLAG_COMPLETE_FRAMES;
    }

    if (avcodec_open2(av_context_, av_codec_, nullptr) < 0) {
        NEMU_LOG_ERROR("NVDEC.FFmpeg", "avcodec_open2 failed for {}", VideoCodecName(codec));
        return false;
    }
    av_frame_ = av_frame_alloc();
    if (!av_frame_) return false;

    NEMU_LOG_INFO("NVDEC.FFmpeg", "host decoder ready: {} ({})", VideoCodecName(codec),
                  av_codec_->name);
    return true;
}

bool DecodeApi::SendPacket(std::span<const u8> data, size_t configuration_size) {
    (void)configuration_size;
    if (!av_context_ || data.empty()) return false;
    // Feed the packet through the parser (if present) or directly.
    int ret;
    const u8* ptr = data.data();
    size_t remaining = data.size();
    while (remaining > 0) {
        u8* out_data = nullptr;
        int out_size = 0;
        if (av_parser_) {
            const int parsed = av_parser_parse2(av_parser_, av_context_, &out_data, &out_size,
                                                ptr, static_cast<int>(remaining),
                                                AV_NOPTS_VALUE, AV_NOPTS_VALUE, 0);
            if (parsed < 0) {
                NEMU_LOG_WARN("NVDEC.FFmpeg", "parser error {}", parsed);
                return false;
            }
            ptr += parsed;
            remaining -= static_cast<size_t>(parsed);
        } else {
            out_data = const_cast<u8*>(ptr);
            out_size = static_cast<int>(remaining);
            remaining = 0;
        }
        if (out_size > 0) {
            AVPacket* pkt = av_packet_alloc();
            if (!pkt) return false;
            ret = av_new_packet(pkt, out_size);
            if (ret == 0) {
                std::memcpy(pkt->data, out_data, static_cast<size_t>(out_size));
                ret = avcodec_send_packet(av_context_, pkt);
            }
            av_packet_free(&pkt);
            if (ret < 0 && ret != AVERROR(EAGAIN)) {
                NEMU_LOG_WARN("NVDEC.FFmpeg", "send_packet error {}", ret);
                return false;
            }
        }
    }
    return true;
}

void DecodeApi::ReceiveFrames(std::vector<Frame>& out_frames) {
    if (!av_context_ || !av_frame_) return;
    for (;;) {
        const int ret = avcodec_receive_frame(av_context_, av_frame_);
        if (ret < 0) break; // EAGAIN or error: drained
        Frame f;
        f.width = static_cast<u32>(av_frame_->width);
        f.height = static_cast<u32>(av_frame_->height);
        // Convert first frame to NV12 via the frame's own data when already
        // YUV420P (Y plane + interleaved U/V); full swscale path when formats
        // diverge. Keep it minimal: handle YUV420P directly, skip others.
        if (av_frame_->format == AV_PIX_FMT_YUV420P) {
            const u32 y_size = f.width * f.height;
            f.data.resize(y_size * 3 / 2);
            const u8* y_src = av_frame_->data[0];
            const size_t y_stride = static_cast<size_t>(av_frame_->linesize[0]);
            for (u32 row = 0; row < f.height; ++row) {
                std::memcpy(f.data.data() + row * f.width, y_src + row * y_stride, f.width);
            }
            // Interleave U/V into NV12 UV plane.
            u8* uv_dst = f.data.data() + y_size;
            const u8* u_src = av_frame_->data[1];
            const u8* v_src = av_frame_->data[2];
            const size_t uv_stride = static_cast<size_t>(av_frame_->linesize[1]);
            for (u32 row = 0; row < f.height / 2; ++row) {
                for (u32 col = 0; col < f.width / 2; ++col) {
                    uv_dst[(row * (f.width / 2) + col) * 2 + 0] = u_src[row * uv_stride + col];
                    uv_dst[(row * (f.width / 2) + col) * 2 + 1] = v_src[row * uv_stride + col];
                }
            }
            out_frames.push_back(std::move(f));
        } else {
            NEMU_LOG_DEBUG("NVDEC.FFmpeg", "frame format {} not yet handled", av_frame_->format);
        }
        av_frame_unref(av_frame_);
        if (out_frames.size() > 10) break; // citron overflow cap
    }
}
#else
DecodeApi::~DecodeApi() = default;
bool DecodeApi::Initialize(VideoCodec) { return false; }
bool DecodeApi::SendPacket(std::span<const u8>, size_t) { return false; }
void DecodeApi::ReceiveFrames(std::vector<Frame>&) {}
#endif

} // namespace nemu::core::gpu::nvhost::ffmpeg
