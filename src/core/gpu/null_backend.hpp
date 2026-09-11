#pragma once

#include "gpu_interface.hpp"
#include <vector>
#include <span>

namespace nemu::core::gpu {

// Software-rendering GPU backend. Rasterizes DrawArrays/DrawIndexed into a
// host-side framebuffer (no GPU required), so a first real frame is observable
// even in headless/CI environments via DumpFramePPM().
class NullGpuBackend final : public IGpuBackend {
public:
    NullGpuBackend();
    ~NullGpuBackend() override;

    bool Initialize(u32 render_width, u32 render_height) override;
    void Shutdown() override;

    void BeginFrame() override;
    void EndFrame() override;
    void Present() override;

    void SetViewport(const Viewport& viewport) override;
    void SetScissor(const ScissorRect& scissor) override;
    void ClearRenderTarget(const ClearColor& color) override;
    void ClearDepthStencil(float depth, u8 stencil) override;

    void DrawArrays(PrimitiveTopology topology, u32 first_vertex, u32 vertex_count) override;
    void DrawIndexed(PrimitiveTopology topology, u32 index_count, u32 first_index, u32 base_vertex) override;

    void SetRasterVertices(std::span<const RasterVertex> vertices) override;
    void SetRasterIndices(std::span<const u32> indices) override;
    bool DumpFramePPM(const char* path) override;

    [[nodiscard]] GpuStats GetStats() const noexcept override { return stats_; }
    [[nodiscard]] std::string_view GetBackendName() const noexcept override { return "Null / Software Rasterizer"; }

    [[nodiscard]] const ClearColor& GetLastClearColor() const noexcept { return last_clear_color_; }
    [[nodiscard]] const Viewport& GetCurrentViewport() const noexcept { return current_viewport_; }
    [[nodiscard]] const ScissorRect& GetCurrentScissor() const noexcept { return current_scissor_; }

    /// Access to the raw host framebuffer (R8G8B8A8, row-major) for tests.
    [[nodiscard]] const u8* Framebuffer() const noexcept { return framebuffer_.data(); }
    [[nodiscard]] size_t FramebufferSize() const noexcept { return framebuffer_.size(); }

private:
    // Coordinate transforms: NDC [-1,1] -> pixel [0,w) x [0,h).
    int NdcToPixelX(float ndc) const noexcept;
    int NdcToPixelY(float ndc) const noexcept;
    void RasterizeTriangle(const RasterVertex& a, const RasterVertex& b, const RasterVertex& c);
    void PutPixel(int x, int y, const RasterVertex& v);

    bool initialized_{false};
    bool in_frame_{false};
    u32 width_{0};
    u32 height_{0};
    Viewport current_viewport_{};
    ScissorRect current_scissor_{};
    ClearColor last_clear_color_{};

    std::vector<RasterVertex> vertices_;
    std::vector<u32> indices_;
    std::vector<u8> framebuffer_; // R8G8B8A8
    std::vector<float> depth_;   // 1/z buffer
    GpuStats stats_{};
};

} // namespace nemu::core::gpu
