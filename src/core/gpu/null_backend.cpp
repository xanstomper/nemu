#include "null_backend.hpp"
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
    stats_.frames_presented++;
    NEMU_LOG_INFO("GPU", "Present frame #{} ({} draw calls, {} vertices)",
                  stats_.frames_presented, stats_.draw_calls, stats_.vertices_submitted);
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
    if (topology != PrimitiveTopology::Triangles || vertices_.empty()) {
        NEMU_LOG_DEBUG("GPU", "DrawArrays: {} vertices (topology {}) ignored by software rasterizer",
                       vertex_count, static_cast<u32>(topology));
        return;
    }
    for (u32 i = 0; i + 2 < vertex_count; i += 3) {
        const size_t a = first_vertex + i;
        const size_t b = first_vertex + i + 1;
        const size_t c = first_vertex + i + 2;
        if (a < vertices_.size() && b < vertices_.size() && c < vertices_.size()) {
            RasterizeTriangle(vertices_[a], vertices_[b], vertices_[c]);
        }
    }
}

void NullGpuBackend::DrawIndexed(PrimitiveTopology topology, u32 index_count, u32 first_index, u32 base_vertex) {
    stats_.draw_calls++;
    stats_.vertices_submitted += index_count;
    if (topology != PrimitiveTopology::Triangles || indices_.empty() || vertices_.empty()) {
        NEMU_LOG_DEBUG("GPU", "DrawIndexed: {} indices (topology {}) ignored by software rasterizer",
                       index_count, static_cast<u32>(topology));
        return;
    }
    for (u32 i = 0; i + 2 < index_count; i += 3) {
        const size_t ia = first_index + i;
        const size_t ib = first_index + i + 1;
        const size_t ic = first_index + i + 2;
        if (ia >= indices_.size() || ib >= indices_.size() || ic >= indices_.size()) {
            continue;
        }
        const size_t a = base_vertex + indices_[ia];
        const size_t b = base_vertex + indices_[ib];
        const size_t c = base_vertex + indices_[ic];
        if (a < vertices_.size() && b < vertices_.size() && c < vertices_.size()) {
            RasterizeTriangle(vertices_[a], vertices_[b], vertices_[c]);
        }
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
