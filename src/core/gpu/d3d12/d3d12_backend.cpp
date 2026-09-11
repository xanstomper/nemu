#include "d3d12_backend.hpp"

#ifdef _WIN32
#include "platform/logger.hpp"
#include <d3dcompiler.h>
#include <cstring>

namespace nemu::core::gpu {

// Minimal embedded shaders used to bring up a real D3D12 raster pipeline.
// These are tiny author-authored HLSL fragments (not copied from any project);
// they pass through a per-vertex {x,y} + {r,g,b,a} through to the pixel shader.
static const char* kVertexShaderHLSL = R"(
struct VSOut {
    float4 pos : SV_Position;
    float4 color : COLOR0;
};
VSOut main(float2 pos : POSITION, float4 color : COLOR0) {
    VSOut o;
    o.pos = float4(pos, 0.0f, 1.0f);
    o.color = color;
    return o;
}
)";

static const char* kPixelShaderHLSL = R"(
struct PSIn {
    float4 pos : SV_Position;
    float4 color : COLOR0;
};
float4 main(PSIn input) : SV_Target {
    return input.color;
}
)";

D3D12GpuBackend::D3D12GpuBackend() = default;

D3D12GpuBackend::~D3D12GpuBackend() {
    Shutdown();
}

bool D3D12GpuBackend::Initialize(u32 render_width, u32 render_height) {
    width_ = render_width;
    height_ = render_height;

    HRESULT hr = CreateDXGIFactory1(IID_PPV_ARGS(&dxgi_factory_));
    if (FAILED(hr)) {
        NEMU_LOG_WARN("D3D12", "Failed to create DXGIFactory: HRESULT 0x{:08X}", static_cast<u32>(hr));
        return false;
    }

    Microsoft::WRL::ComPtr<IDXGIAdapter1> adapter;
    for (UINT i = 0; dxgi_factory_->EnumAdapters1(i, &adapter) != DXGI_ERROR_NOT_FOUND; ++i) {
        DXGI_ADAPTER_DESC1 desc;
        adapter->GetDesc1(&desc);
        if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) {
            continue;
        }
        hr = D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_12_0, IID_PPV_ARGS(&device_));
        if (SUCCEEDED(hr)) {
            std::wstring wdesc(desc.Description);
            std::string sdesc(wdesc.begin(), wdesc.end());
            NEMU_LOG_INFO("D3D12", "Created D3D12 Device on hardware adapter: {}", sdesc);
            break;
        }
    }

    if (!device_) {
        hr = D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device_));
        if (FAILED(hr)) {
            NEMU_LOG_WARN("D3D12", "D3D12CreateDevice failed: HRESULT 0x{:08X} (Hardware D3D12 unavailable)", static_cast<u32>(hr));
            return false;
        }
    }

    D3D12_COMMAND_QUEUE_DESC queue_desc{};
    queue_desc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    queue_desc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
    hr = device_->CreateCommandQueue(&queue_desc, IID_PPV_ARGS(&command_queue_));
    if (FAILED(hr)) {
        NEMU_LOG_ERROR("D3D12", "CreateCommandQueue failed: 0x{:08X}", static_cast<u32>(hr));
        return false;
    }

    hr = device_->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&command_allocator_));
    if (FAILED(hr)) {
        NEMU_LOG_ERROR("D3D12", "CreateCommandAllocator failed: 0x{:08X}", static_cast<u32>(hr));
        return false;
    }

    hr = device_->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, command_allocator_.Get(), nullptr, IID_PPV_ARGS(&command_list_));
    if (FAILED(hr)) {
        NEMU_LOG_ERROR("D3D12", "CreateCommandList failed: 0x{:08X}", static_cast<u32>(hr));
        return false;
    }
    command_list_->Close();

    hr = device_->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence_));
    if (FAILED(hr)) {
        NEMU_LOG_ERROR("D3D12", "CreateFence failed: 0x{:08X}", static_cast<u32>(hr));
        return false;
    }
    fence_value_ = 1;
    fence_event_ = CreateEvent(nullptr, FALSE, FALSE, nullptr);

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

    // Bring up the real render path (swap chain, RTV heap, PSO, geometry).
    // Failure is non-fatal: the backend stays usable for clears/commands and
    // reports pipeline state via IsRenderPipelineReady().
    CreateSwapChainAndTargets();
    CreatePipelineAndBuffers();

    initialized_ = true;
    NEMU_LOG_INFO("D3D12", "Direct3D 12 backend initialized ({}x{}) render_pipeline={}",
                  width_, height_, IsRenderPipelineReady());
    return true;
}

bool D3D12GpuBackend::CreateSwapChainAndTargets() {
    if (!dxgi_factory_ || !command_queue_) {
        return false;
    }
    // Descriptor heap for the back-buffer render target views.
    D3D12_DESCRIPTOR_HEAP_DESC rtv_heap_desc{};
    rtv_heap_desc.NumDescriptors = kBackBufferCount;
    rtv_heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    rtv_heap_desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    if (FAILED(device_->CreateDescriptorHeap(&rtv_heap_desc, IID_PPV_ARGS(&rtv_heap_)))) {
        NEMU_LOG_WARN("D3D12", "CreateDescriptorHeap(RTV) failed; clears only");
        return false;
    }
    rtv_descriptor_size_ = device_->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

    DXGI_SWAP_CHAIN_DESC1 swap_desc{};
    swap_desc.Width = width_;
    swap_desc.Height = height_;
    swap_desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    swap_desc.Stereo = FALSE;
    swap_desc.SampleDesc = {.Count = 1, .Quality = 0};
    swap_desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    swap_desc.BufferCount = kBackBufferCount;
    swap_desc.Scaling = DXGI_SCALING_STRETCH;
    swap_desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    swap_desc.AlphaMode = DXGI_ALPHA_MODE_UNSPECIFIED;

    Microsoft::WRL::ComPtr<IDXGISwapChain1> swap1;
    // CreateSwapChainForHwnd requires an hwnd; on headless/off-screen Windows we
    // still can provide a message-only window. If that fails we degrade gracefully.
    HWND hwnd = GetConsoleWindow();
    if (hwnd == nullptr) {
        hwnd = CreateWindowExW(0, L"STATIC", L"Nemu", WS_OVERLAPPED, 0, 0,
                               static_cast<int>(width_), static_cast<int>(height_),
                               nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    }
    hr_ = dxgi_factory_->CreateSwapChainForHwnd(command_queue_.Get(), hwnd, &swap_desc,
                                                nullptr, nullptr, swap1.GetAddressOf());
    if (FAILED(hr_)) {
        NEMU_LOG_WARN("D3D12", "CreateSwapChainForHwnd failed 0x{:08X}; clears only", static_cast<u32>(hr_));
        return false;
    }
    if (FAILED(swap1.As(&swap_chain_))) {
        NEMU_LOG_WARN("D3D12", "SwapChain3 query failed; clears only");
        return false;
    }

    back_buffers_.resize(kBackBufferCount);
    for (UINT i = 0; i < kBackBufferCount; ++i) {
        if (FAILED(swap_chain_->GetBuffer(i, IID_PPV_ARGS(&back_buffers_[i])))) {
            return false;
        }
        D3D12_CPU_DESCRIPTOR_HANDLE rtv = rtv_heap_->GetCPUDescriptorHandleForHeapStart();
        rtv.ptr += static_cast<SIZE_T>(i * rtv_descriptor_size_);
        device_->CreateRenderTargetView(back_buffers_[i].Get(), nullptr, rtv);
    }

    NEMU_LOG_INFO("D3D12", "Swap chain created ({} back buffers)", kBackBufferCount);
    return true;
}

bool D3D12GpuBackend::CreatePipelineAndBuffers() {
    if (!device_ || !swap_chain_) {
        return false;
    }
    // Root signature: only the vertex layout is driven through input assembly;
    // no root parameters are required for this minimal pipeline.
    D3D12_ROOT_SIGNATURE_DESC root_desc{};
    root_desc.NumParameters = 0;
    root_desc.NumStaticSamplers = 0;
    Microsoft::WRL::ComPtr<ID3DBlob> sig_blob, sig_error;
    hr_ = D3D12SerializeRootSignature(&root_desc, D3D_ROOT_SIGNATURE_VERSION_1, &sig_blob, &sig_error);
    if (FAILED(hr_)) {
        NEMU_LOG_WARN("D3D12", "SerializeRootSignature failed 0x{:08X}; clears only", static_cast<u32>(hr_));
        return false;
    }
    if (FAILED(device_->CreateRootSignature(0, sig_blob->GetBufferPointer(), sig_blob->GetBufferSize(),
                                            IID_PPV_ARGS(&root_signature_)))) {
        NEMU_LOG_WARN("D3D12", "CreateRootSignature failed; clears only");
        return false;
    }

    // Compile the minimal VS/PS at runtime.
    Microsoft::WRL::ComPtr<ID3DBlob> vs_blob, ps_blob;
    hr_ = D3DCompile(kVertexShaderHLSL, std::strlen(kVertexShaderHLSL), "NemuVS", nullptr, nullptr,
                     "main", "vs_5_0", 0, 0, &vs_blob, nullptr);
    if (FAILED(hr_)) {
        NEMU_LOG_WARN("D3D12", "D3DCompile(VS) failed 0x{:08X}; clears only", static_cast<u32>(hr_));
        return false;
    }
    hr_ = D3DCompile(kPixelShaderHLSL, std::strlen(kPixelShaderHLSL), "NemuPS", nullptr, nullptr,
                     "main", "ps_5_0", 0, 0, &ps_blob, nullptr);
    if (FAILED(hr_)) {
        NEMU_LOG_WARN("D3D12", "D3DCompile(PS) failed 0x{:08X}; clears only", static_cast<u32>(hr_));
        return false;
    }

    D3D12_INPUT_ELEMENT_DESC layout[] = {
        {"POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 8, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
    };

    D3D12_GRAPHICS_PIPELINE_STATE_DESC pso_desc{};
    pso_desc.pRootSignature = root_signature_.Get();
    pso_desc.VS = {vs_blob->GetBufferPointer(), vs_blob->GetBufferSize()};
    pso_desc.PS = {ps_blob->GetBufferPointer(), ps_blob->GetBufferSize()};
    pso_desc.InputLayout = {layout, 2};
    pso_desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    pso_desc.NumRenderTargets = 1;
    pso_desc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
    pso_desc.SampleDesc = {.Count = 1, .Quality = 0};
    std::memset(&pso_desc.RasterizerState, 0, sizeof(pso_desc.RasterizerState));
    pso_desc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    pso_desc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    pso_desc.RasterizerState.FrontCounterClockwise = FALSE;
    pso_desc.RasterizerState.DepthBias = 0;
    pso_desc.RasterizerState.DepthBiasClamp = 0.0f;
    pso_desc.RasterizerState.SlopeScaledDepthBias = 0.0f;
    pso_desc.RasterizerState.DepthClipEnable = TRUE;
    pso_desc.RasterizerState.MultisampleEnable = FALSE;
    pso_desc.RasterizerState.AntialiasedLineEnable = FALSE;
    pso_desc.RasterizerState.ForcedSampleCount = 0;
    pso_desc.RasterizerState.ConservativeRaster = D3D12_CONSERVATIVE_RASTERIZATION_MODE_OFF;
    pso_desc.SampleMask = UINT_MAX;
    std::memset(&pso_desc.BlendState, 0, sizeof(pso_desc.BlendState));
    for (UINT i = 0; i < 8; ++i) {
        pso_desc.BlendState.RenderTarget[i].BlendEnable = FALSE;
        pso_desc.BlendState.RenderTarget[i].LogicOpEnable = FALSE;
        pso_desc.BlendState.RenderTarget[i].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    }
    std::memset(&pso_desc.DepthStencilState, 0, sizeof(pso_desc.DepthStencilState));
    pso_desc.DepthStencilState.DepthEnable = FALSE;

    if (FAILED(device_->CreateGraphicsPipelineState(&pso_desc, IID_PPV_ARGS(&pso_)))) {
        NEMU_LOG_WARN("D3D12", "CreateGraphicsPipelineState failed; clears only");
        return false;
    }

    NEMU_LOG_INFO("D3D12", "Graphics pipeline created (root signature + PSO ready)");
    return true;
}

void D3D12GpuBackend::Shutdown() {
    if (initialized_) {
        WaitForGpu();
        if (fence_event_) {
            CloseHandle(fence_event_);
            fence_event_ = nullptr;
        }
        index_buffer_.Reset();
        vertex_buffer_.Reset();
        pso_.Reset();
        root_signature_.Reset();
        back_buffers_.clear();
        swap_chain_.Reset();
        command_list_.Reset();
        command_allocator_.Reset();
        command_queue_.Reset();
        rtv_heap_.Reset();
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

    if (IsRenderPipelineReady()) {
        back_buffer_index_ = swap_chain_->GetCurrentBackBufferIndex();
        const D3D12_CPU_DESCRIPTOR_HANDLE rtv = CurrentRtv();
        command_list_->OMSetRenderTargets(1, &rtv, FALSE, nullptr);
        D3D12_RESOURCE_BARRIER barrier{};
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition.pResource = back_buffers_[back_buffer_index_].Get();
        barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
        barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
        command_list_->ResourceBarrier(1, &barrier);
        command_list_->ClearRenderTargetView(rtv, &clear_color_.r, 0, nullptr);
    }
    command_list_->RSSetViewports(1, &d3d_viewport_);
    command_list_->RSSetScissorRects(1, &d3d_scissor_);
    in_frame_ = true;
}

D3D12_CPU_DESCRIPTOR_HANDLE D3D12GpuBackend::CurrentRtv() const noexcept {
    D3D12_CPU_DESCRIPTOR_HANDLE rtv = rtv_heap_->GetCPUDescriptorHandleForHeapStart();
    rtv.ptr += static_cast<SIZE_T>(back_buffer_index_ * rtv_descriptor_size_);
    return rtv;
}

void D3D12GpuBackend::EndFrame() {
    if (!initialized_ || !in_frame_) return;
    if (IsRenderPipelineReady() && back_buffer_index_ < kBackBufferCount) {
        D3D12_RESOURCE_BARRIER barrier{};
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition.pResource = back_buffers_[back_buffer_index_].Get();
        barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
        barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
        command_list_->ResourceBarrier(1, &barrier);
    }
    command_list_->Close();
    ID3D12CommandList* pp_lists[] = { command_list_.Get() };
    command_queue_->ExecuteCommandLists(1, pp_lists);
    in_frame_ = false;
}

void D3D12GpuBackend::Present() {
    if (!initialized_) return;
    if (IsRenderPipelineReady()) {
        swap_chain_->Present(0, 0);
    }
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
    clear_color_ = color;
    if (in_frame_ && command_list_ && IsRenderPipelineReady()) {
        const D3D12_CPU_DESCRIPTOR_HANDLE rtv = CurrentRtv();
        command_list_->ClearRenderTargetView(rtv, &clear_color_.r, 0, nullptr);
    }
    NEMU_LOG_DEBUG("D3D12", "ClearRenderTarget: ({:.2f}, {:.2f}, {:.2f}, {:.2f})", color.r, color.g, color.b, color.a);
}

void D3D12GpuBackend::ClearDepthStencil([[maybe_unused]] float depth, [[maybe_unused]] u8 stencil) {
    NEMU_LOG_DEBUG("D3D12", "ClearDepthStencil: depth={:.2f}, stencil={}", depth, stencil);
}

void D3D12GpuBackend::SetRasterVertices(std::span<const RasterVertex> vertices) {
    vertices_.assign(vertices.begin(), vertices.end());
}

void D3D12GpuBackend::SetRasterIndices(std::span<const u32> indices) {
    indices_.assign(indices.begin(), indices.end());
}

void D3D12GpuBackend::DrawArrays(PrimitiveTopology topology, u32 first_vertex, u32 vertex_count) {
    stats_.draw_calls++;
    stats_.vertices_submitted += vertex_count;
    if (!in_frame_ || !command_list_ || !IsRenderPipelineReady() || vertices_.empty()) {
        return;
    }
    UploadGeometry();
    BindPipelineAndTopology(topology);
    command_list_->DrawInstanced(vertex_count, 1, first_vertex, 0);
}

void D3D12GpuBackend::DrawIndexed(PrimitiveTopology topology, u32 index_count, u32 first_index, u32 base_vertex) {
    stats_.draw_calls++;
    stats_.vertices_submitted += index_count;
    if (!in_frame_ || !command_list_ || !IsRenderPipelineReady() || vertices_.empty()) {
        return;
    }
    UploadGeometry();
    BindPipelineAndTopology(topology);
    command_list_->DrawIndexedInstanced(index_count, 1, first_index, static_cast<INT>(base_vertex), 0);
}

void D3D12GpuBackend::UploadGeometry() {
    if (!device_ || vertices_.empty()) {
        return;
    }
    const UINT vb_size = static_cast<UINT>(vertices_.size() * sizeof(D3D12Vertex));
    const UINT ib_size = static_cast<UINT>(indices_.size() * sizeof(u32));

    if (vb_size != vertex_buffer_size_ || ib_size != index_buffer_size_) {
        // Recreate the buffers only when the size actually changes.
        vertex_buffer_.Reset();
        index_buffer_.Reset();

        D3D12_HEAP_PROPERTIES heap = {D3D12_HEAP_TYPE_UPLOAD, D3D12_CPU_PAGE_PROPERTY_UNKNOWN,
                                      D3D12_MEMORY_POOL_UNKNOWN, 1, 1};
        D3D12_RESOURCE_DESC vb_desc = {};
        vb_desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        vb_desc.Alignment = 0;
        vb_desc.Width = vb_size;
        vb_desc.Height = 1;
        vb_desc.DepthOrArraySize = 1;
        vb_desc.MipLevels = 1;
        vb_desc.Format = DXGI_FORMAT_UNKNOWN;
        vb_desc.SampleDesc = {.Count = 1, .Quality = 0};
        vb_desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        vb_desc.Flags = D3D12_RESOURCE_FLAG_NONE;
        if (FAILED(device_->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &vb_desc,
                                                    D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                                                    IID_PPV_ARGS(&vertex_buffer_)))) {
            return;
        }
        if (ib_size > 0) {
            D3D12_RESOURCE_DESC ib_desc = vb_desc;
            ib_desc.Width = ib_size;
            if (FAILED(device_->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &ib_desc,
                                                        D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                                                        IID_PPV_ARGS(&index_buffer_)))) {
                index_buffer_.Reset();
            }
        }
        vertex_buffer_size_ = vb_size;
        index_buffer_size_ = ib_size;
    }

    if (vertex_buffer_) {
        D3D12Vertex* mapped = nullptr;
        if (SUCCEEDED(vertex_buffer_->Map(0, nullptr, reinterpret_cast<void**>(&mapped)))) {
            for (size_t i = 0; i < vertices_.size(); ++i) {
                mapped[i].x = vertices_[i].x;
                mapped[i].y = vertices_[i].y;
                mapped[i].r = vertices_[i].r;
                mapped[i].g = vertices_[i].g;
                mapped[i].b = vertices_[i].b;
                mapped[i].a = vertices_[i].a;
            }
            vertex_buffer_->Unmap(0, nullptr);
        }
    }
    if (index_buffer_ && !indices_.empty()) {
        void* mapped = nullptr;
        if (SUCCEEDED(index_buffer_->Map(0, nullptr, &mapped))) {
            std::memcpy(mapped, indices_.data(), index_buffer_size_);
            index_buffer_->Unmap(0, nullptr);
        }
    }
}

void D3D12GpuBackend::BindPipelineAndTopology(PrimitiveTopology topology) {
    if (!pso_) {
        return;
    }
    command_list_->SetPipelineState(pso_.Get());
    if (root_signature_) {
        command_list_->SetGraphicsRootSignature(root_signature_.Get());
    }
    D3D12_PRIMITIVE_TOPOLOGY d3d_topo = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
    switch (topology) {
        case PrimitiveTopology::Points: d3d_topo = D3D_PRIMITIVE_TOPOLOGY_POINTLIST; break;
        case PrimitiveTopology::Lines: d3d_topo = D3D_PRIMITIVE_TOPOLOGY_LINELIST; break;
        case PrimitiveTopology::LineStrip: d3d_topo = D3D_PRIMITIVE_TOPOLOGY_LINESTRIP; break;
        case PrimitiveTopology::Triangles: d3d_topo = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST; break;
        case PrimitiveTopology::TriangleStrip: d3d_topo = D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP; break;
        default: break;
    }
    command_list_->IASetPrimitiveTopology(d3d_topo);

    const UINT stride = sizeof(D3D12Vertex);
    D3D12_VERTEX_BUFFER_VIEW view{};
    if (vertex_buffer_) {
        view.BufferLocation = vertex_buffer_->GetGPUVirtualAddress();
        view.StrideInBytes = stride;
        view.SizeInBytes = vertex_buffer_size_;
        command_list_->IASetVertexBuffers(0, 1, &view);
    }
    if (index_buffer_ && !indices_.empty()) {
        D3D12_INDEX_BUFFER_VIEW ibv{};
        ibv.BufferLocation = index_buffer_->GetGPUVirtualAddress();
        ibv.Format = index_format();
        ibv.SizeInBytes = index_buffer_size_;
        command_list_->IASetIndexBuffer(&ibv);
    }
}

} // namespace nemu::core::gpu
#endif // _WIN32