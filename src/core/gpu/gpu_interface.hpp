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

    [[nodiscard]] virtual GpuStats GetStats() const noexcept = 0;
    [[nodiscard]] virtual std::string_view GetBackendName() const noexcept = 0;
};

} // namespace nemu::core::gpu
