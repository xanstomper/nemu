#include "null_backend.hpp"
#include "pipeline/graphics_optimizer.hpp"
#include "platform/logger.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace nemu::core::gpu {

namespace {
    // Right-handed edge function: signed twice-area of (a,b,c) in pixel space.
    constexpr float EdgeFunc(float ax, float ay, float bx, float by, float cx, float cy) noexcept {
        return (cx - ax) * (by - ay) - (cy - ay) * (bx - ax);
    }
}

NullGpuBackend::NullGpuBackend() = default;
NullGpuBackend::~NullGpuBackend() {
    Shutdown();
}

bool NullGpuBackend::Initialize(u32 render_width, u32 render_height) {
    if (render_width == 0 || render_height == 0) {
        NEMU_LOG_ERROR("GPU", "Null backend: invalid resolution {}x{}", render_width, render_height);
        return false;
    }
    width_ = render_width;
    height_ = render_height;
    current_viewport_ = Viewport{
        .x = 0.0f,
        .y = 0.0f,
        .width = static_cast<float>(render_width),
        .height = static_cast<float>(render_height),
        .min_depth = 0.0f,
        .max_depth = 1.0f
    };
    current_scissor_ = ScissorRect{
        .left = 0,
        .top = 0,
        .right = render_width,
        .bottom = render_height
    };
    framebuffer_.assign(static_cast<size_t>(render_width) * render_height * 4, 0u);
    depth_.assign(static_cast<size_t>(render_width) * render_height, 0.0f);
    initialized_ = true;
    NEMU_LOG_INFO("GPU", "Initialized Null GPU backend ({}x{})", render_width, render_height);
    return true;
}

void NullGpuBackend::Shutdown() {
    if (initialized_) {
        NEMU_LOG_INFO("GPU", "Shutdown Null GPU backend");
        framebuffer_.clear();
        depth_.clear();
        vertices_.clear();
        indices_.clear();
        initialized_ = false;
    }
}

void NullGpuBackend::BeginFrame() {
    in_frame_ = true;
}

void NullGpuBackend::EndFrame() {
    in_frame_ = false;
}

void NullGpuBackend::Present() {
    ApplyOptimizerPipeline();
    stats_.frames_presented++;
    NEMU_LOG_INFO("GPU", "Present frame #{} ({} draw calls, {} vertices)",
                  stats_.frames_presented, stats_.draw_calls, stats_.vertices_submitted);
}

void NullGpuBackend::DispatchCompute(u32 block_x, u32 block_y, u32 block_z) {
    // Tier-A3: software compute dispatch — counted for diagnostics. Real
    // compute work (postFX/particles) requires the shader translation chain;
    // the Null backend records the dispatch shape so tests can assert it.
    stats_.draw_calls++; // folded into draw accounting for observability
    NEMU_LOG_DEBUG("GPU", "DispatchCompute: {}x{}x{} blocks (software accounting)",
                   block_x, block_y, block_z);
}

void NullGpuBackend::ApplyOptimizerPipeline() {
    const auto& opt = optimizer_settings_;
    if (!opt.enabled || framebuffer_.empty() || width_ == 0 || height_ == 0) {
        has_prev_frame_ = false;
        return;
    }

    using pipeline::GraphicsOptimizer;
    using pipeline::UpscalerMode;
    using pipeline::AntiAliasingMode;
    using pipeline::FrameGenMode;

    const size_t pixel_count = static_cast<size_t>(width_) * height_;
    const auto* fb_u32 = reinterpret_cast<const u32*>(framebuffer_.data());

    // --- 1. Upscaling (source frame -> optimizer target resolution) --------
    // The rasterizer renders at width_ x height_; the optimizer pipeline
    // upscales in-place to the configured scale (target == source when the
    // resolution scale is 1.0). We upscale to 2x the raster resolution when
    // FSR/Bicubic is selected (the Xbox presents at 4K via D3D12; the
    // software path demonstrates the same pipeline at 2x).
    u32 target_w = width_;
    u32 target_h = height_;
    if (opt.upscaler == UpscalerMode::FSR_1_0 || opt.upscaler == UpscalerMode::FSR_2_0 ||
        opt.upscaler == UpscalerMode::Bicubic) {
        target_w = width_ * 2;
        target_h = height_ * 2;
    }

    if (target_w != width_ || target_h != height_) {
        std::vector<u32> upscaled(static_cast<size_t>(target_w) * target_h);
        if (GraphicsOptimizer::ApplyUpscale(
                std::span<const u32>(fb_u32, pixel_count),
                width_, height_,
                upscaled, target_w, target_h,
                opt.upscaler, opt.fsr_sharpness)) {
            // Copy back into the framebuffer at the new resolution.
            framebuffer_.resize(static_cast<size_t>(target_w) * target_h * 4);
            std::memcpy(framebuffer_.data(), upscaled.data(), framebuffer_.size());
            width_ = target_w;
            height_ = target_h;
            depth_.assign(static_cast<size_t>(target_w) * target_h, 0.0f);
        }
    }

    // --- 2. Anti-aliasing post-process --------------------------------------
    if (opt.anti_aliasing == AntiAliasingMode::FXAA || opt.anti_aliasing == AntiAliasingMode::SMAA) {
        auto* mutable_u32 = reinterpret_cast<u32*>(framebuffer_.data());
        GraphicsOptimizer::ApplyAntiAliasing(
            std::span<u32>(mutable_u32, static_cast<size_t>(width_) * height_),
            width_, height_, opt.anti_aliasing);
    }

    // --- 3. Frame generation (2x motion interpolation) ----------------------
    if (opt.frame_generation != FrameGenMode::Disabled) {
        std::vector<u32> out_frame(static_cast<size_t>(width_) * height_);
        const auto* prev_u32 = has_prev_frame_
            ? reinterpret_cast<const u32*>(prev_frame_raw_.data()) : nullptr;
        if (prev_u32 && prev_frame_raw_.size() == framebuffer_.size()) {
            if (GraphicsOptimizer::GenerateIntermediateFrame(
                    std::span<const u32>(prev_u32, pixel_count),
                    std::span<const u32>(fb_u32, pixel_count),
                    out_frame, width_, height_)) {
                std::memcpy(framebuffer_.data(), out_frame.data(),
                            static_cast<size_t>(width_) * height_ * 4);
            }
        }
        // Save the current raw frame for the next interpolation cycle.
        prev_frame_raw_.assign(framebuffer_.begin(), framebuffer_.end());
        has_prev_frame_ = true;
        stats_.frames_generated++;
    }

    stats_.frames_upscaled++;
}

void NullGpuBackend::SetViewport(const Viewport& viewport) {
    current_viewport_ = viewport;
}

void NullGpuBackend::SetScissor(const ScissorRect& scissor) {
    current_scissor_ = scissor;
}

void NullGpuBackend::ClearRenderTarget(const ClearColor& color) {
    last_clear_color_ = color;
    const u8 r = static_cast<u8>(std::clamp(color.r, 0.0f, 1.0f) * 255.0f);
    const u8 g = static_cast<u8>(std::clamp(color.g, 0.0f, 1.0f) * 255.0f);
    const u8 b = static_cast<u8>(std::clamp(color.b, 0.0f, 1.0f) * 255.0f);
    const u8 a = static_cast<u8>(std::clamp(color.a, 0.0f, 1.0f) * 255.0f);
    for (size_t i = 0; i < framebuffer_.size(); i += 4) {
        framebuffer_[i + 0] = r;
        framebuffer_[i + 1] = g;
        framebuffer_[i + 2] = b;
        framebuffer_[i + 3] = a;
    }
    std::fill(depth_.begin(), depth_.end(), 0.0f);
    NEMU_LOG_DEBUG("GPU", "Clear render target: ({:.2f}, {:.2f}, {:.2f}, {:.2f})", color.r, color.g, color.b, color.a);
}

void NullGpuBackend::ClearDepthStencil(float depth, u8 /*stencil*/) {
    std::fill(depth_.begin(), depth_.end(), depth);
    NEMU_LOG_DEBUG("GPU", "Clear depth ({:.2f})", depth);
}

void NullGpuBackend::SetRasterVertices(std::span<const RasterVertex> vertices) {
    vertices_.assign(vertices.begin(), vertices.end());
}

void NullGpuBackend::SetRasterIndices(std::span<const u32> indices) {
    indices_.assign(indices.begin(), indices.end());
}

int NullGpuBackend::NdcToPixelX(float ndc) const noexcept {
    // NDC x in [-1, 1] -> [0, width). +0.5 to center the pixel.
    return static_cast<int>(std::floor((ndc * 0.5f + 0.5f) * static_cast<float>(width_) + 0.5f));
}

int NullGpuBackend::NdcToPixelY(float ndc) const noexcept {
    // NDC y is up; pixel rows go top-down.
    return static_cast<int>(std::floor((0.5f - ndc * 0.5f) * static_cast<float>(height_) + 0.5f));
}

void NullGpuBackend::PutPixel(int x, int y, const RasterVertex& v) {
    if (x < 0 || x >= static_cast<int>(width_) || y < 0 || y >= static_cast<int>(height_)) {
        return;
    }
    size_t idx = (static_cast<size_t>(y) * width_ + static_cast<size_t>(x)) * 4;
    framebuffer_[idx + 0] = static_cast<u8>(std::clamp(v.r, 0.0f, 1.0f) * 255.0f);
    framebuffer_[idx + 1] = static_cast<u8>(std::clamp(v.g, 0.0f, 1.0f) * 255.0f);
    framebuffer_[idx + 2] = static_cast<u8>(std::clamp(v.b, 0.0f, 1.0f) * 255.0f);
    framebuffer_[idx + 3] = static_cast<u8>(std::clamp(v.a, 0.0f, 1.0f) * 255.0f);
}

void NullGpuBackend::RasterizeTriangle(const RasterVertex& va, const RasterVertex& vb, const RasterVertex& vc) {
    const int x0 = NdcToPixelX(va.x), y0 = NdcToPixelY(va.y);
    const int x1 = NdcToPixelX(vb.x), y1 = NdcToPixelY(vb.y);
    const int x2 = NdcToPixelX(vc.x), y2 = NdcToPixelY(vc.y);

    const int min_x = std::max(0, std::min({x0, x1, x2}));
    const int max_x = std::min(static_cast<int>(width_) - 1, std::max({x0, x1, x2}));
    const int min_y = std::max(0, std::min({y0, y1, y2}));
    const int max_y = std::min(static_cast<int>(height_) - 1, std::max({y0, y1, y2}));

    if (min_x > max_x || min_y > max_y) {
        return;
    }

    const float area = EdgeFunc(static_cast<float>(x0), static_cast<float>(y0),
                                static_cast<float>(x1), static_cast<float>(y1),
                                static_cast<float>(x2), static_cast<float>(y2));
    if (std::fabs(area) < 1e-6f) {
        return; // degenerate
    }

    for (int y = min_y; y <= max_y; ++y) {
        for (int x = min_x; x <= max_x; ++x) {
            const float fx = static_cast<float>(x);
            const float fy = static_cast<float>(y);
            const float w0 = EdgeFunc(static_cast<float>(x1), static_cast<float>(y1),
                                      static_cast<float>(x2), static_cast<float>(y2), fx, fy);
            const float w1 = EdgeFunc(static_cast<float>(x2), static_cast<float>(y2),
                                      static_cast<float>(x0), static_cast<float>(y0), fx, fy);
            const float w2 = EdgeFunc(static_cast<float>(x0), static_cast<float>(y0),
                                      static_cast<float>(x1), static_cast<float>(y1), fx, fy);
            if (area > 0.0f ? (w0 < 0.0f || w1 < 0.0f || w2 < 0.0f)
                            : (w0 > 0.0f || w1 > 0.0f || w2 > 0.0f)) {
                continue;
            }
            const float inv_area = 1.0f / area;
            const float l0 = w1 * inv_area; // barycentric for vertex A
            const float l1 = w2 * inv_area; // B
            const float l2 = w0 * inv_area; // C
            RasterVertex out;
            out.x = fx;
            out.y = fy;
            out.r = l0 * va.r + l1 * vb.r + l2 * vc.r;
            out.g = l0 * va.g + l1 * vb.g + l2 * vc.g;
            out.b = l0 * va.b + l1 * vb.b + l2 * vc.b;
            out.a = l0 * va.a + l1 * vb.a + l2 * vc.a;
            PutPixel(x, y, out);
        }
    }
}

void NullGpuBackend::DrawArrays(PrimitiveTopology topology, u32 first_vertex, u32 vertex_count) {
    stats_.draw_calls++;
    stats_.vertices_submitted += vertex_count;
    if (vertices_.empty() || vertex_count < 3) {
        NEMU_LOG_DEBUG("GPU", "DrawArrays: {} vertices (topology {}) skipped (empty or degenerate)",
                       vertex_count, static_cast<u32>(topology));
        return;
    }

    // Software-triangle-family helpers shared with DrawIndexed.
    const auto tri = [&](size_t a, size_t b, size_t c) {
        if (a < vertices_.size() && b < vertices_.size() && c < vertices_.size()) {
            RasterizeTriangle(vertices_[a], vertices_[b], vertices_[c]);
        }
    };

    switch (topology) {
        case PrimitiveTopology::Triangles:
            for (u32 i = 0; i + 2 < vertex_count; i += 3) {
                tri(first_vertex + i, first_vertex + i + 1, first_vertex + i + 2);
            }
            break;
        case PrimitiveTopology::TriangleStrip:
            // Alternate winding per triangle to match D3D12/Maxwell strips.
            for (u32 i = 0; i + 2 < vertex_count; ++i) {
                if ((i & 1) == 0) tri(first_vertex + i, first_vertex + i + 1, first_vertex + i + 2);
                else              tri(first_vertex + i + 1, first_vertex + i, first_vertex + i + 2);
            }
            break;
        case PrimitiveTopology::TriangleFan:
            for (u32 i = 1; i + 1 < vertex_count; ++i) {
                tri(first_vertex, first_vertex + i, first_vertex + i + 1);
            }
            break;
        default:
            // Points/Lines/LineStrip: not rasterizable into the current 2D
            // RasterVertex framebuffer path; keep D3D12 parity for the triangle
            // family (the ones the geometry model can produce).
            NEMU_LOG_DEBUG("GPU", "DrawArrays: topology {} not rasterizable by software backend",
                           static_cast<u32>(topology));
            break;
    }
}

void NullGpuBackend::DrawIndexed(PrimitiveTopology topology, u32 index_count, u32 first_index, u32 base_vertex) {
    stats_.draw_calls++;
    stats_.vertices_submitted += index_count;
    if (indices_.empty() || vertices_.empty() || index_count < 3) {
        NEMU_LOG_DEBUG("GPU", "DrawIndexed: {} indices (topology {}) skipped (empty or degenerate)",
                       index_count, static_cast<u32>(topology));
        return;
    }

    // Resolve an index (relative to first_index, applying base_vertex) to a
    // host vertex index, then rasterize the triangle if all three are in range.
    const auto idx = [&](u32 slot) -> size_t {
        if (first_index + slot >= indices_.size()) return SIZE_MAX;
        return base_vertex + static_cast<size_t>(indices_[first_index + slot]);
    };
    const auto tri3 = [&](size_t a, size_t b, size_t c) {
        if (a < vertices_.size() && b < vertices_.size() && c < vertices_.size()) {
            RasterizeTriangle(vertices_[a], vertices_[b], vertices_[c]);
        }
    };

    switch (topology) {
        case PrimitiveTopology::Triangles:
            for (u32 i = 0; i + 2 < index_count; i += 3) {
                tri3(idx(i), idx(i + 1), idx(i + 2));
            }
            break;
        case PrimitiveTopology::TriangleStrip:
            for (u32 i = 0; i + 2 < index_count; ++i) {
                if ((i & 1) == 0) tri3(idx(i), idx(i + 1), idx(i + 2));
                else              tri3(idx(i + 1), idx(i), idx(i + 2));
            }
            break;
        case PrimitiveTopology::TriangleFan:
            for (u32 i = 1; i + 1 < index_count; ++i) {
                tri3(idx(0), idx(i), idx(i + 1));
            }
            break;
        default:
            NEMU_LOG_DEBUG("GPU", "DrawIndexed: topology {} not rasterizable by software backend",
                           static_cast<u32>(topology));
            break;
    }
}

bool NullGpuBackend::DumpFramePPM(const char* path) {
    if (framebuffer_.empty() || width_ == 0 || height_ == 0) {
        NEMU_LOG_ERROR("GPU", "Cannot dump PPM: no framebuffer");
        return false;
    }
    std::FILE* f = std::fopen(path, "wb");
    if (!f) {
        NEMU_LOG_ERROR("GPU", "Cannot open PPM for writing: {}", path);
        return false;
    }
    std::fprintf(f, "P6\n%u %u\n255\n", width_, height_);
    // P6 is RGB (no alpha). Convert R8G8B8A8 -> RGB.
    std::vector<u8> rgb(framebuffer_.size() / 4 * 3);
    for (size_t i = 0, o = 0; i + 3 < framebuffer_.size(); i += 4, o += 3) {
        rgb[o + 0] = framebuffer_[i + 0];
        rgb[o + 1] = framebuffer_[i + 1];
        rgb[o + 2] = framebuffer_[i + 2];
    }
    std::fwrite(rgb.data(), 1, rgb.size(), f);
    std::fclose(f);
    NEMU_LOG_INFO("GPU", "Dumped frame PPM: {} ({}x{})", path, width_, height_);
    return true;
}

} // namespace nemu::core::gpu
