#pragma once

#include "gpu_interface.hpp"

namespace nemu::core::gpu {

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

    [[nodiscard]] GpuStats GetStats() const noexcept override { return stats_; }
    [[nodiscard]] std::string_view GetBackendName() const noexcept override { return "Null / Headless Backend"; }

    [[nodiscard]] const ClearColor& GetLastClearColor() const noexcept { return last_clear_color_; }
    [[nodiscard]] const Viewport& GetCurrentViewport() const noexcept { return current_viewport_; }
    [[nodiscard]] const ScissorRect& GetCurrentScissor() const noexcept { return current_scissor_; }

private:
    bool initialized_{false};
    bool in_frame_{false};
    Viewport current_viewport_{};
    ScissorRect current_scissor_{};
    ClearColor last_clear_color_{};
    GpuStats stats_{};
};

} // namespace nemu::core::gpu
