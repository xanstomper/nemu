#pragma once

#include "core/types.hpp"
#include <span>
#include <string_view>
#include <memory>

namespace nemu::core::gpu {

enum class PrimitiveTopology : u32 {
    Points = 0,
    Lines = 1,
    LineStrip = 2,
    Triangles = 3,
    TriangleStrip = 4,
    TriangleFan = 5
};

enum class PixelFormat : u32 {
    R8G8B8A8_UNORM,
    B8G8R8A8_UNORM,
    R16G16B16A16_FLOAT,
    D24_UNORM_S8_UINT,
    D32_FLOAT
};

struct Viewport {
    float x{0.0f};
    float y{0.0f};
    float width{1280.0f};
    float height{720.0f};
    float min_depth{0.0f};
    float max_depth{1.0f};
};

struct ScissorRect {
    u32 left{0};
    u32 top{0};
    u32 right{1280};
    u32 bottom{720};
};

struct ClearColor {
    float r{0.0f};
    float g{0.0f};
    float b{0.0f};
    float a{1.0f};
};

struct GpuStats {
    u64 frames_presented{0};
    u64 draw_calls{0};
    u64 vertices_submitted{0};
};

// A rasterizer vertex: 2D normalized-device position (x,y in [-1,1]) plus an
// RGBA color. This is the minimal geometric primitive the software rasterizer
// (and future guest shader output) feed into the backend.
struct RasterVertex {
    float x{0.0f};
    float y{0.0f};
    float r{1.0f};
    float g{1.0f};
    float b{1.0f};
    float a{1.0f};
};

class IGpuBackend {
public:
    virtual ~IGpuBackend() = default;

    virtual bool Initialize(u32 render_width, u32 render_height) = 0;
    virtual void Shutdown() = 0;

    virtual void BeginFrame() = 0;
    virtual void EndFrame() = 0;
    virtual void Present() = 0;

    virtual void SetViewport(const Viewport& viewport) = 0;
    virtual void SetScissor(const ScissorRect& scissor) = 0;
    virtual void ClearRenderTarget(const ClearColor& color) = 0;
    virtual void ClearDepthStencil(float depth, u8 stencil) = 0;

    virtual void DrawArrays(PrimitiveTopology topology, u32 first_vertex, u32 vertex_count) = 0;
    virtual void DrawIndexed(PrimitiveTopology topology, u32 index_count, u32 first_index, u32 base_vertex) = 0;

    // --- Vertex / material binding and host-observable frame capture ---
    // These are the software-rasterizable entry points. Backends that only
    // forward to a hardware API (e.g. D3D12) may leave them as no-ops; the
    // software/Null backend rasterizes them into a host framebuffer so a first
    // real frame is observable even in a headless/CI environment.
    virtual void SetRasterVertices(std::span<const RasterVertex> vertices) { vertices_readonly_ = vertices; }
    virtual void SetRasterIndices(std::span<const u32> indices) { indices_readonly_ = indices; }
    /// Dump the current host framebuffer to a P6-binary PPM file (for headless
    /// visual verification). Returns false if no framebuffer is available.
    virtual bool DumpFramePPM(const char* path) { (void)path; return false; }

    // --- Optional crisp UI overlay -----------------------------------------
    // Backends with real font/image capability (e.g. SDL2 desktop) composite
    // anti-aliased text and cover images over the rasterized frame at present
    // time. The frontend queues overlay ops while building UI geometry and the
    // backend flushes them inside Present(). Coordinates are in the same UI
    // space as the raster vertices (default 1280x720). Default implementations
    // are no-ops so other backends (Null, D3D12) are unaffected.
    [[nodiscard]] virtual bool SupportsUiOverlay() const noexcept { return false; }
    /// Queue a text draw. align: -1 left, 0 center, 1 right (relative to x).
    virtual void UiTextOverlay(std::string_view /*text*/, float /*x*/, float /*y*/,
                               float /*size_px*/, float /*r*/, float /*g*/, float /*b*/,
                               float /*a*/, int /*align*/) {}
    /// Queue a cover image drawn into the given rect (cached by key/path).
    virtual void UiImageOverlay(std::string_view /*key*/, std::string_view /*host_path*/,
                                float /*x*/, float /*y*/, float /*w*/, float /*h*/) {}

    [[nodiscard]] virtual GpuStats GetStats() const noexcept = 0;
    [[nodiscard]] virtual std::string_view GetBackendName() const noexcept = 0;

protected:
    // Retained vertex/index data for backends that rasterize on the host.
    std::span<const RasterVertex> vertices_readonly_;
    std::span<const u32> indices_readonly_;
};

} // namespace nemu::core::gpu
