#include "d3d12_backend.hpp"

#ifdef _WIN32
#include "platform/logger.hpp"

namespace nemu::core::gpu {

D3D12GpuBackend::D3D12GpuBackend() = default;

D3D12GpuBackend::~D3D12GpuBackend() {
    Shutdown();
}

bool D3D12GpuBackend::Initialize(u32 render_width, u32 render_height) {
    width_ = render_width;
    height_ = render_height;

    // 1. Create DXGI Factory
    HRESULT hr = CreateDXGIFactory1(IID_PPV_ARGS(&dxgi_factory_));
    if (FAILED(hr)) {
        NEMU_LOG_WARN("D3D12", "Failed to create DXGIFactory: HRESULT 0x{:08X}", static_cast<u32>(hr));
        return false;
    }

    // 2. Enumerate Adapters and pick hardware adapter
    Microsoft::WRL::ComPtr<IDXGIAdapter1> adapter;
    for (UINT i = 0; dxgi_factory_->EnumAdapters1(i, &adapter) != DXGI_ERROR_NOT_FOUND; ++i) {
        DXGI_ADAPTER_DESC1 desc;
        adapter->GetDesc1(&desc);
        if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) {
            continue;
        }

        // Try creating device with Feature Level 12_0 (or 11_0 fallback for Wine testing)
        hr = D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_12_0, IID_PPV_ARGS(&device_));
        if (SUCCEEDED(hr)) {
            std::wstring wdesc(desc.Description);
            std::string sdesc(wdesc.begin(), wdesc.end());
            NEMU_LOG_INFO("D3D12", "Created D3D12 Device on hardware adapter: {}", sdesc);
            break;
        }
    }

    if (!device_) {
        // Fallback: try default adapter
        hr = D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device_));
        if (FAILED(hr)) {
            NEMU_LOG_WARN("D3D12", "D3D12CreateDevice failed: HRESULT 0x{:08X} (Hardware D3D12 unavailable)", static_cast<u32>(hr));
            return false;
        }
    }

    // 3. Create Direct Command Queue
    D3D12_COMMAND_QUEUE_DESC queue_desc{};
    queue_desc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    queue_desc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
    hr = device_->CreateCommandQueue(&queue_desc, IID_PPV_ARGS(&command_queue_));
    if (FAILED(hr)) {
        NEMU_LOG_ERROR("D3D12", "CreateCommandQueue failed: 0x{:08X}", static_cast<u32>(hr));
        return false;
    }

    // 4. Create Command Allocator
    hr = device_->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&command_allocator_));
    if (FAILED(hr)) {
        NEMU_LOG_ERROR("D3D12", "CreateCommandAllocator failed: 0x{:08X}", static_cast<u32>(hr));
        return false;
    }

    // 5. Create Command List
    hr = device_->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, command_allocator_.Get(), nullptr, IID_PPV_ARGS(&command_list_));
    if (FAILED(hr)) {
        NEMU_LOG_ERROR("D3D12", "CreateCommandList failed: 0x{:08X}", static_cast<u32>(hr));
        return false;
    }
    command_list_->Close();

    // 6. Create Synchronization Fence
    hr = device_->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence_));
    if (FAILED(hr)) {
        NEMU_LOG_ERROR("D3D12", "CreateFence failed: 0x{:08X}", static_cast<u32>(hr));
        return false;
    }
    fence_value_ = 1;
    fence_event_ = CreateEvent(nullptr, FALSE, FALSE, nullptr);

    // Setup initial Viewport and Scissor
    d3d_viewport_ = D3D12_VIEWPORT{
        .TopLeftX = 0.0f,
        .TopLeftY = 0.0f,
        .Width = static_cast<float>(width_),
        .Height = static_cast<float>(height_),
        .MinDepth = 0.0f,
        .MaxDepth = 1.0f
    };
    d3d_scissor_ = D3D12_RECT{
        .left = 0,
        .top = 0,
        .right = static_cast<LONG>(width_),
        .bottom = static_cast<LONG>(height_)
    };

    initialized_ = true;
    NEMU_LOG_INFO("D3D12", "Direct3D 12 backend initialized successfully ({}x{})", width_, height_);
    return true;
}

void D3D12GpuBackend::Shutdown() {
    if (initialized_) {
        WaitForGpu();
        if (fence_event_) {
            CloseHandle(fence_event_);
            fence_event_ = nullptr;
        }
        command_list_.Reset();
        command_allocator_.Reset();
        command_queue_.Reset();
        fence_.Reset();
        device_.Reset();
        dxgi_factory_.Reset();
        initialized_ = false;
        NEMU_LOG_INFO("D3D12", "Direct3D 12 backend shutdown complete");
    }
}

void D3D12GpuBackend::WaitForGpu() {
    if (!command_queue_ || !fence_ || !fence_event_) return;

    const UINT64 fence_to_wait = fence_value_;
    command_queue_->Signal(fence_.Get(), fence_to_wait);
    fence_value_++;

    if (fence_->GetCompletedValue() < fence_to_wait) {
        fence_->SetEventOnCompletion(fence_to_wait, fence_event_);
        WaitForSingleObject(fence_event_, INFINITE);
    }
}

void D3D12GpuBackend::BeginFrame() {
    if (!initialized_) return;
    command_allocator_->Reset();
    command_list_->Reset(command_allocator_.Get(), nullptr);
    command_list_->RSSetViewports(1, &d3d_viewport_);
    command_list_->RSSetScissorRects(1, &d3d_scissor_);
    in_frame_ = true;
}

void D3D12GpuBackend::EndFrame() {
    if (!initialized_ || !in_frame_) return;
    command_list_->Close();
    ID3D12CommandList* pp_lists[] = { command_list_.Get() };
    command_queue_->ExecuteCommandLists(1, pp_lists);
    in_frame_ = false;
}

void D3D12GpuBackend::Present() {
    if (!initialized_) return;
    WaitForGpu();
    stats_.frames_presented++;
}

void D3D12GpuBackend::SetViewport(const Viewport& viewport) {
    d3d_viewport_ = D3D12_VIEWPORT{
        .TopLeftX = viewport.x,
        .TopLeftY = viewport.y,
        .Width = viewport.width,
        .Height = viewport.height,
        .MinDepth = viewport.min_depth,
        .MaxDepth = viewport.max_depth
    };
    if (in_frame_ && command_list_) {
        command_list_->RSSetViewports(1, &d3d_viewport_);
    }
}

void D3D12GpuBackend::SetScissor(const ScissorRect& scissor) {
    d3d_scissor_ = D3D12_RECT{
        .left = static_cast<LONG>(scissor.left),
        .top = static_cast<LONG>(scissor.top),
        .right = static_cast<LONG>(scissor.right),
        .bottom = static_cast<LONG>(scissor.bottom)
    };
    if (in_frame_ && command_list_) {
        command_list_->RSSetScissorRects(1, &d3d_scissor_);
    }
}

void D3D12GpuBackend::ClearRenderTarget(const ClearColor& color) {
    if (!in_frame_ || !command_list_) return;
    // Clearing via command list when RTV descriptor is configured
    NEMU_LOG_DEBUG("D3D12", "ClearRenderTarget: ({:.2f}, {:.2f}, {:.2f}, {:.2f})", color.r, color.g, color.b, color.a);
}

void D3D12GpuBackend::ClearDepthStencil([[maybe_unused]] float depth, [[maybe_unused]] u8 stencil) {
    if (!in_frame_ || !command_list_) return;
    NEMU_LOG_DEBUG("D3D12", "ClearDepthStencil: depth={:.2f}, stencil={}", depth, stencil);
}

void D3D12GpuBackend::DrawArrays(PrimitiveTopology topology, u32 first_vertex, u32 vertex_count) {
    if (!in_frame_ || !command_list_) return;

    D3D12_PRIMITIVE_TOPOLOGY d3d_topology = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
    switch (topology) {
        case PrimitiveTopology::Points: d3d_topology = D3D_PRIMITIVE_TOPOLOGY_POINTLIST; break;
        case PrimitiveTopology::Lines: d3d_topology = D3D_PRIMITIVE_TOPOLOGY_LINELIST; break;
        case PrimitiveTopology::LineStrip: d3d_topology = D3D_PRIMITIVE_TOPOLOGY_LINESTRIP; break;
        case PrimitiveTopology::Triangles: d3d_topology = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST; break;
        case PrimitiveTopology::TriangleStrip: d3d_topology = D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP; break;
        default: break;
    }

    command_list_->IASetPrimitiveTopology(d3d_topology);
    command_list_->DrawInstanced(vertex_count, 1, first_vertex, 0);

    stats_.draw_calls++;
    stats_.vertices_submitted += vertex_count;
}

void D3D12GpuBackend::DrawIndexed(PrimitiveTopology topology, u32 index_count, u32 first_index, u32 base_vertex) {
    if (!in_frame_ || !command_list_) return;

    D3D12_PRIMITIVE_TOPOLOGY d3d_topology = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
    switch (topology) {
        case PrimitiveTopology::Points: d3d_topology = D3D_PRIMITIVE_TOPOLOGY_POINTLIST; break;
        case PrimitiveTopology::Lines: d3d_topology = D3D_PRIMITIVE_TOPOLOGY_LINELIST; break;
        case PrimitiveTopology::LineStrip: d3d_topology = D3D_PRIMITIVE_TOPOLOGY_LINESTRIP; break;
        case PrimitiveTopology::Triangles: d3d_topology = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST; break;
        case PrimitiveTopology::TriangleStrip: d3d_topology = D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP; break;
        default: break;
    }

    command_list_->IASetPrimitiveTopology(d3d_topology);
    command_list_->DrawIndexedInstanced(index_count, 1, first_index, static_cast<INT>(base_vertex), 0);

    stats_.draw_calls++;
    stats_.vertices_submitted += index_count;
}

} // namespace nemu::core::gpu
#endif // _WIN32
