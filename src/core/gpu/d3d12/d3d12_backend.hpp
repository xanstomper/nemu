#pragma once

#include "core/gpu/gpu_interface.hpp"

#ifdef _WIN32
#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl/client.h>

namespace nemu::core::gpu {

class D3D12GpuBackend final : public IGpuBackend {
public:
    D3D12GpuBackend();
    ~D3D12GpuBackend() override;

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
    [[nodiscard]] std::string_view GetBackendName() const noexcept override { return "Direct3D 12 (Xbox Series S/X & Win32)"; }

    [[nodiscard]] bool IsDeviceCreated() const noexcept { return device_ != nullptr; }

private:
    void WaitForGpu();

    bool initialized_{false};
    bool in_frame_{false};
    u32 width_{1280};
    u32 height_{720};
    GpuStats stats_{};

    D3D12_VIEWPORT d3d_viewport_{};
    D3D12_RECT d3d_scissor_{};

    Microsoft::WRL::ComPtr<IDXGIFactory4> dxgi_factory_;
    Microsoft::WRL::ComPtr<ID3D12Device> device_;
    Microsoft::WRL::ComPtr<ID3D12CommandQueue> command_queue_;
    Microsoft::WRL::ComPtr<ID3D12CommandAllocator> command_allocator_;
    Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> command_list_;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> rtv_heap_;
    Microsoft::WRL::ComPtr<ID3D12Fence> fence_;
    UINT64 fence_value_{0};
    HANDLE fence_event_{nullptr};
};

} // namespace nemu::core::gpu
#endif // _WIN32
