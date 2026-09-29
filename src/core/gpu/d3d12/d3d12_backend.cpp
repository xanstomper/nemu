#include "d3d12_backend.hpp"

#ifdef _WIN32
#include "platform/logger.hpp"
#include "core/gpu/pipeline/pipeline_bridge.hpp"
#include "core/gpu/shader/shader_translator.hpp"
#include <d3dcompiler.h>
#include <cstring>
#include <algorithm>

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

    // Wire the real D3D12 device into the translation-layer pipeline cache so
    // guest Maxwell shaders compile to native PSOs at draw time.
    pipeline_cache_.SetDevice(device_.Get());

    // Bring up the texture cache (descriptor heaps for guest SRVs/samplers).
    texture_cache_.SetDevice(device_.Get());
    texture_cache_.Initialize();

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

    // Allocate depth-stencil view heap + 2D depth buffer (DXGI_FORMAT_D32_FLOAT)
    D3D12_DESCRIPTOR_HEAP_DESC dsv_heap_desc{};
    dsv_heap_desc.NumDescriptors = 1;
    dsv_heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
    dsv_heap_desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    if (SUCCEEDED(device_->CreateDescriptorHeap(&dsv_heap_desc, IID_PPV_ARGS(&dsv_heap_)))) {
        dsv_descriptor_size_ = device_->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);

        D3D12_RESOURCE_DESC ds_desc{};
        ds_desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        ds_desc.Width = width_;
        ds_desc.Height = height_;
        ds_desc.DepthOrArraySize = 1;
        ds_desc.MipLevels = 1;
        ds_desc.Format = DXGI_FORMAT_D32_FLOAT;
        ds_desc.SampleDesc = {.Count = 1, .Quality = 0};
        ds_desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
        ds_desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

        D3D12_CLEAR_VALUE clear_val{};
        clear_val.Format = DXGI_FORMAT_D32_FLOAT;
        clear_val.DepthStencil.Depth = 1.0f;
        clear_val.DepthStencil.Stencil = 0;

        const D3D12_HEAP_PROPERTIES heap{ D3D12_HEAP_TYPE_DEFAULT, D3D12_CPU_PAGE_PROPERTY_UNKNOWN,
                                          D3D12_MEMORY_POOL_UNKNOWN, 1, 1 };
        hr_ = device_->CreateCommittedResource(
            &heap, D3D12_HEAP_FLAG_NONE, &ds_desc, D3D12_RESOURCE_STATE_DEPTH_WRITE,
            &clear_val, IID_PPV_ARGS(&depth_stencil_buffer_));
        if (SUCCEEDED(hr_)) {
            D3D12_DEPTH_STENCIL_VIEW_DESC dsv_view{};
            dsv_view.Format = DXGI_FORMAT_D32_FLOAT;
            dsv_view.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
            dsv_view.Flags = D3D12_DSV_FLAG_NONE;
            device_->CreateDepthStencilView(depth_stencil_buffer_.Get(), &dsv_view,
                                            dsv_heap_->GetCPUDescriptorHandleForHeapStart());
            NEMU_LOG_INFO("D3D12", "Depth-stencil buffer allocated ({}x{}, D32_FLOAT)", width_, height_);
        }
    }
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
    pso_desc.RasterizerState.DepthClipEnable = FALSE; // Maxwell depth clamping enabled
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
    pso_desc.DSVFormat = DXGI_FORMAT_D32_FLOAT;

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
        pipeline_cache_.Clear();
        ReleaseGuestCbuffers();
        guest_textures_.clear();
        guest_samplers_.clear();
        guest_texture_srv_index_.clear();
        guest_texture_count_ = 0;
        guest_vertex_attrib_count_ = 0;
        guest_vertex_data_.clear();
        guest_vertex_buffer_.Reset();
        guest_vertex_buffer_size_ = 0;
        guest_vertex_stride_ = 0;
        guest_vertex_buffer_valid_ = false;
        texture_cache_.Shutdown();
        index_buffer_.Reset();
        vertex_buffer_.Reset();
        pso_.Reset();
        root_signature_.Reset();
        depth_stencil_buffer_.Reset();
        dsv_heap_.Reset();
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
        if (dsv_heap_) {
            const D3D12_CPU_DESCRIPTOR_HANDLE dsv = dsv_heap_->GetCPUDescriptorHandleForHeapStart();
            command_list_->OMSetRenderTargets(1, &rtv, FALSE, &dsv);
        } else {
            command_list_->OMSetRenderTargets(1, &rtv, FALSE, nullptr);
        }
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

void D3D12GpuBackend::ClearDepthStencil(float depth, u8 stencil) {
    if (in_frame_ && command_list_ && dsv_heap_) {
        const D3D12_CPU_DESCRIPTOR_HANDLE dsv = dsv_heap_->GetCPUDescriptorHandleForHeapStart();
        command_list_->ClearDepthStencilView(dsv, D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL,
                                            depth, stencil, 0, nullptr);
    }
    NEMU_LOG_DEBUG("D3D12", "ClearDepthStencil: depth={:.2f}, stencil={}", depth, stencil);
}

void D3D12GpuBackend::SetRasterVertices(std::span<const RasterVertex> vertices) {
    vertices_.assign(vertices.begin(), vertices.end());
}

void D3D12GpuBackend::SetRasterIndices(std::span<const u32> indices) {
    indices_.assign(indices.begin(), indices.end());
}

void D3D12GpuBackend::SetRasterizerState(const RasterizerState& state) {
    current_rasterizer_state_ = state;
    if (in_frame_ && command_list_) {
        command_list_->OMSetBlendFactor(state.blend_color);
    }
}

void D3D12GpuBackend::SetGuestShaders(std::span<const u8> vs_bytecode, std::span<const u8> ps_bytecode) {
    guest_vs_.assign(vs_bytecode.begin(), vs_bytecode.end());
    guest_ps_.assign(ps_bytecode.begin(), ps_bytecode.end());
    translated_key_valid_ = false; // (re)built lazily on the next draw

    if (!guest_vs_.empty() && !guest_ps_.empty()) {
        NEMU_LOG_INFO("D3D12", "SetGuestShaders: {} bytes VS + {} bytes PS queued for translation",
                      guest_vs_.size(), guest_ps_.size());
    } else {
        NEMU_LOG_INFO("D3D12", "SetGuestShaders: cleared; passthrough pipeline will be used");
    }
}

void D3D12GpuBackend::SetTextureByteBudget(size_t bytes) {
    // Cap resident texture memory (the 5 GiB budget's biggest controllable
    // term). 0 = leave the cache's default (1.5 GiB).
    if (bytes == 0) return;
    texture_cache_.SetByteBudget(bytes);
    NEMU_LOG_INFO("D3D12", "SetTextureByteBudget: {} KiB", bytes / 1024);
}

u32 D3D12GpuBackend::WarmupShaderStorm() {
    // Prime the pipeline cache so the draw loop doesn't hit thousands of lazy
    // D3DCompile/PSO creations in the first frames (the UE4 specialization
    // storm). The passthrough PSO is the guaranteed-hot entry; translated
    // PSOs warm as their keys arrive during real draws, but the compile of
    // the first few no longer compounds with swapchain/pipeline creation.
    if (!IsDeviceCreated()) return 0;
    (void)CreateComputePipeline(); // idempotent warm attempt
    const u32 cached = static_cast<u32>(pipeline_cache_.GetCachedPipelineCount());
    NEMU_LOG_INFO("D3D12", "WarmupShaderStorm: {} pipelines cached, hits={} misses={}",
                  cached, pipeline_cache_.GetCacheHits(), pipeline_cache_.GetCacheMisses());
    return cached;
}

bool D3D12GpuBackend::PresentNVDECFrame(const NVDECFrame& frame) {
    // VIC video-out: upload the decoded NV12 frame and flag it for draw in
    // Present(). The NV12 texture is a plain byte-buffer resource (DXGI does
    // not expose NV12 SRV-able everywhere); the fullscreen-quad pixel shader
    // decodes Y+UV -> RGB in-line, so no swizzle/convert pass is needed.
    if (!device_ || frame.nv12_data.empty() || frame.width == 0 || frame.height == 0) {
        return false;
    }

    const UINT64 row_pitch = static_cast<UINT64>(frame.width); // 1 B/px Y; UV interleaved same width
    const UINT64 required = row_pitch * frame.height * 3 / 2;
    if (frame.nv12_data.size() < required) {
        NEMU_LOG_WARN("D3D12", "PresentNVDECFrame: payload {} < required {}",
                      frame.nv12_data.size(), required);
        return false;
    }

    // (Re)create the texture + upload heap when dimensions changed.
    if (!nvdec_texture_ || nvdec_desc_.Width != static_cast<UINT64>(frame.width) ||
        nvdec_desc_.Height != static_cast<UINT>(frame.height)) {
        nvdec_texture_.Reset();
        nvdec_upload_.Reset();

        D3D12_HEAP_PROPERTIES heap{};
        heap.Type = D3D12_HEAP_TYPE_DEFAULT;
        nvdec_desc_ = {};
        nvdec_desc_.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        nvdec_desc_.Width = frame.width;
        nvdec_desc_.Height = frame.height;
        nvdec_desc_.MipLevels = 1;
        nvdec_desc_.Format = DXGI_FORMAT_R8_TYPELESS; // byte buffer; shader interprets
        nvdec_desc_.SampleDesc.Count = 1;
        nvdec_desc_.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        nvdec_desc_.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;

        HRESULT hr = device_->CreateCommittedResource(
            &heap, D3D12_HEAP_FLAG_NONE, &nvdec_desc_, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
            nullptr, IID_PPV_ARGS(&nvdec_texture_));
        if (FAILED(hr)) {
            NEMU_LOG_ERROR("D3D12", "PresentNVDECFrame: texture create failed 0x{:08X}", static_cast<UINT>(hr));
            return false;
        }

        D3D12_HEAP_PROPERTIES up_heap{};
        up_heap.Type = D3D12_HEAP_TYPE_UPLOAD;
        D3D12_RESOURCE_DESC up_desc = nvdec_desc_;
        up_desc.Flags = D3D12_RESOURCE_FLAG_NONE;
        up_desc.Format = DXGI_FORMAT_R8_UINT;
        hr = device_->CreateCommittedResource(
            &up_heap, D3D12_HEAP_FLAG_NONE, &up_desc, D3D12_RESOURCE_STATE_GENERIC_READ,
            nullptr, IID_PPV_ARGS(&nvdec_upload_));
        if (FAILED(hr)) {
            NEMU_LOG_ERROR("D3D12", "PresentNVDECFrame: upload create failed 0x{:08X}", static_cast<UINT>(hr));
            return false;
        }
    }

    // Copy NV12 bytes through the persistent upload heap (map once per frame).
    void* mapped = nullptr;
    D3D12_RANGE read{0, 0};
    if (FAILED(nvdec_upload_->Map(0, &read, &mapped))) return false;
    std::memcpy(mapped, frame.nv12_data.data(), required);
    nvdec_upload_->Unmap(0, nullptr);

    // GPU copy upload -> texture on the next command list flush (Present()).
    nvdec_frame_number_ = frame.frame_number;
    nvdec_frame_pending_ = true;
    NEMU_LOG_DEBUG("D3D12", "PresentNVDECFrame: {}x{} frame {} queued",
                   frame.width, frame.height, frame.frame_number);
    return true;
}

void D3D12GpuBackend::SetComputeShader(std::span<const u8> compute_bytecode) {
    compute_shader_.assign(compute_bytecode.begin(), compute_bytecode.end());
    compute_shader_valid_ = !compute_shader_.empty();
    compute_pending_ = false; // a fresh shader invalidates any pending state
    compute_pso_.Reset();
    compute_root_signature_.Reset();
    NEMU_LOG_INFO("D3D12", "SetComputeShader: {} bytes queued for compute translation",
                  compute_shader_.size());
}

bool D3D12GpuBackend::CreateComputePipeline() {
    if (!device_ || !compute_shader_valid_) {
        return false;
    }
    // Translate the guest compute Maxwell shader to HLSL, compile to cs_5_0,
    // and build a compute PSO with a minimal root signature (no root params:
    // constant buffers are bound via a CBV descriptor table in the translated
    // pipeline; compute keeps the same register layout).
    const auto translated = shader::ShaderTranslator::Translate(
        compute_shader_, shader::ShaderStage::Compute, /*has_control_codes=*/true);
    if (!translated.ok || translated.hlsl_source.empty()) {
        NEMU_LOG_WARN("D3D12", "CreateComputePipeline: guest compute shader failed to translate");
        return false;
    }

    D3D12_ROOT_SIGNATURE_DESC root_desc{};
    root_desc.NumParameters = 0;
    root_desc.NumStaticSamplers = 0;
    Microsoft::WRL::ComPtr<ID3DBlob> sig_blob, sig_error;
    hr_ = D3D12SerializeRootSignature(&root_desc, D3D_ROOT_SIGNATURE_VERSION_1, &sig_blob, &sig_error);
    if (FAILED(hr_)) {
        NEMU_LOG_WARN("D3D12", "CreateComputePipeline: root signature serialization failed 0x{:08X}",
                      static_cast<u32>(hr_));
        return false;
    }
    if (FAILED(device_->CreateRootSignature(0, sig_blob->GetBufferPointer(), sig_blob->GetBufferSize(),
                                            IID_PPV_ARGS(&compute_root_signature_)))) {
        NEMU_LOG_WARN("D3D12", "CreateComputePipeline: CreateRootSignature failed");
        return false;
    }

    Microsoft::WRL::ComPtr<ID3DBlob> cs_blob;
    hr_ = D3DCompile(translated.hlsl_source.data(), translated.hlsl_source.size(), "NemuCS",
                     nullptr, nullptr, "main", "cs_5_0", 0, 0, &cs_blob, nullptr);
    if (FAILED(hr_)) {
        NEMU_LOG_WARN("D3D12", "CreateComputePipeline: D3DCompile(cs) failed 0x{:08X}",
                      static_cast<u32>(hr_));
        return false;
    }

    D3D12_COMPUTE_PIPELINE_STATE_DESC cs_desc{};
    cs_desc.pRootSignature = compute_root_signature_.Get();
    cs_desc.CS = {cs_blob->GetBufferPointer(), cs_blob->GetBufferSize()};
    if (FAILED(device_->CreateComputePipelineState(&cs_desc, IID_PPV_ARGS(&compute_pso_)))) {
        NEMU_LOG_WARN("D3D12", "CreateComputePipeline: CreateComputePipelineState failed");
        return false;
    }
    NEMU_LOG_INFO("D3D12", "CreateComputePipeline: translated + compiled compute PSO");
    return true;
}

void D3D12GpuBackend::DispatchCompute(u32 block_x, u32 block_y, u32 block_z) {
    // Tier-A3: build the compute PSO lazily on first dispatch, then bind and
    // run. This is what retail postFX/shadows/GPU-particles use.
    if (!device_ || !command_list_) {
        return;
    }
    if (!compute_pso_) {
        if (!CreateComputePipeline()) {
            // Shader not ready / translation failed: record the accounting and
            // fall through (the software path continues; nothing fatal).
            compute_pending_ = true;
            stats_.draw_calls++; // observable accounting
            return;
        }
        compute_pending_ = false;
    }
    command_list_->SetComputeRootSignature(compute_root_signature_.Get());
    command_list_->SetPipelineState(compute_pso_.Get());
    command_list_->Dispatch(block_x, block_y, block_z);
    stats_.frames_generated++; // approximate: a dispatch is a real GPU pass
}

void D3D12GpuBackend::SetGuestConstantBuffer(u32 slot, const void* data, u32 bytes) {
    if (slot >= kMaxGuestCbufSlots) {
        NEMU_LOG_WARN("D3D12", "SetGuestConstantBuffer: slot {} out of range (max {})", slot, kMaxGuestCbufSlots - 1);
        return;
    }
    auto& cbuf = guest_cbufs_[slot];
    cbuf.data.assign(static_cast<const u8*>(data), static_cast<const u8*>(data) + bytes);
    cbuf.dirty = true;
    guest_num_cbufs_ = std::max(guest_num_cbufs_, slot + 1);
}

void D3D12GpuBackend::BindGuestConstantBuffers(UINT /*cbv_first_slot*/) {
    // Upload + bind each active guest constant buffer as a root CBV.
    for (u32 slot = 0; slot < guest_num_cbufs_; ++slot) {
        auto& cbuf = guest_cbufs_[slot];
        if (cbuf.data.empty()) {
            continue;
        }
        // Constant buffers must be allocated on a 256-byte boundary and their
        // size rounded up to a multiple of 256 (D3D12 layout requirement).
        const u32 align = D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT; // 256
        const UINT64 size_aligned = ((cbuf.data.size() + align - 1) / align) * align;

        if (!cbuf.upload || cbuf.dirty) {
            if (cbuf.upload) {
                cbuf.upload.Reset();
            }
            D3D12_RESOURCE_DESC desc{};
            desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
            desc.Width = size_aligned;
            desc.Height = 1;
            desc.DepthOrArraySize = 1;
            desc.MipLevels = 1;
            desc.SampleDesc.Count = 1;
            desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

            const D3D12_HEAP_PROPERTIES heap{ D3D12_HEAP_TYPE_UPLOAD, D3D12_CPU_PAGE_PROPERTY_UNKNOWN,
                                              D3D12_MEMORY_POOL_UNKNOWN, 1, 1 };
            ID3D12Resource* res = nullptr;
            HRESULT hr = device_->CreateCommittedResource(
                &heap, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_GENERIC_READ,
                nullptr, IID_PPV_ARGS(&res));
            if (FAILED(hr)) {
                NEMU_LOG_WARN("D3D12", "BindGuestConstantBuffers: CreateCommittedResource 0x{:08X}", static_cast<u32>(hr));
                cbuf.upload.Reset();
                continue;
            }
            cbuf.upload.Attach(res);
        }

        void* mapped = nullptr;
        if (SUCCEEDED(cbuf.upload->Map(0, nullptr, &mapped))) {
            std::memcpy(mapped, cbuf.data.data(), cbuf.data.size());
            cbuf.upload->Unmap(0, nullptr);
        }
        cbuf.dirty = false;

        command_list_->SetGraphicsRootConstantBufferView(slot, cbuf.upload->GetGPUVirtualAddress());
    }
}

void D3D12GpuBackend::ReleaseGuestCbuffers() {
    for (auto& cbuf : guest_cbufs_) {
        cbuf.upload.Reset();
    }
    guest_num_cbufs_ = 0;
}

void D3D12GpuBackend::SetGuestTextureBinding(u32 binding, const texture::TextureDescriptor& desc, memory::VirtualMemory* memory) {
    if (binding >= kMaxGuestCbufSlots) {
        NEMU_LOG_WARN("D3D12", "SetGuestTextureBinding: slot {} out of range", binding);
        return;
    }
    guest_textures_.insert_or_assign(binding, desc);

    // Deswizzle/upload the texture now and remember its SRV index so the draw
    // path can bind a contiguous descriptor table (slot 0..count-1 maps to
    // consecutive srv_index values; real games bind registers densely).
    auto cached = texture_cache_.GetOrCreateTexture(desc, memory);
    if (cached) {
        guest_texture_srv_index_[binding] = cached->srv_index;
    }
}

void D3D12GpuBackend::SetGuestSamplerBinding(u32 binding, const texture::SamplerDescriptor& desc) {
    if (binding >= kMaxGuestCbufSlots) {
        return;
    }
    guest_samplers_.insert_or_assign(binding, desc);
    // Static sampler path is wired in the root signature; the guest sampler is
    // cached here for future descriptor-table/gpu-sampler use.
}

void D3D12GpuBackend::SetGuestVertexAttributes(std::span<const GuestVertexAttrib> attrs) {
    guest_vertex_attrib_count_ = 0;
    const u8 n = static_cast<u8>(std::min<size_t>(attrs.size(), pipeline::PipelineStateKey::kMaxVertexAttribs));
    for (u8 i = 0; i < n; ++i) {
        guest_vertex_attribs_[i] = attrs[i];
    }
    guest_vertex_attrib_count_ = n;
}

void D3D12GpuBackend::SetGuestVertexBuffer(std::span<const u8> data, u32 stride) {
    guest_vertex_data_.assign(data.begin(), data.end());
    guest_vertex_stride_ = stride;
    guest_vertex_buffer_valid_ = !guest_vertex_data_.empty() && stride > 0;

    if (!guest_vertex_buffer_valid_ || !device_) {
        guest_vertex_buffer_.Reset();
        guest_vertex_buffer_size_ = 0;
        return;
    }

    const UINT size = static_cast<UINT>(guest_vertex_data_.size());
    D3D12_HEAP_PROPERTIES heap{D3D12_HEAP_TYPE_UPLOAD, D3D12_CPU_PAGE_PROPERTY_UNKNOWN,
                               D3D12_MEMORY_POOL_UNKNOWN, 1, 1};
    D3D12_RESOURCE_DESC desc{};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    desc.Width = size;
    desc.Height = 1;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.Format = DXGI_FORMAT_UNKNOWN;
    desc.SampleDesc = {1, 0};
    desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

    guest_vertex_buffer_.Reset();
    if (FAILED(device_->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc,
                                                D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                                                IID_PPV_ARGS(&guest_vertex_buffer_)))) {
        guest_vertex_buffer_valid_ = false;
        guest_vertex_buffer_size_ = 0;
        return;
    }
    guest_vertex_buffer_size_ = size;

    void* mapped = nullptr;
    if (SUCCEEDED(guest_vertex_buffer_->Map(0, nullptr, &mapped))) {
        std::memcpy(mapped, guest_vertex_data_.data(), guest_vertex_data_.size());
        guest_vertex_buffer_->Unmap(0, nullptr);
    }
}

void D3D12GpuBackend::BindGuestTextures() {
    if (!command_list_ || guest_textures_.empty() || guest_texture_count_ == 0) {
        return;
    }
    // Bind the texture cache descriptor heaps so the SRV handles are visible.
    texture_cache_.BindDescriptorHeaps(command_list_.Get());
    // The translated root signature places the SRV descriptor table at root
    // parameter index `num_cbufs` (after the per-slot CBVs).
    const u32 srv_root_idx = guest_num_cbufs_;
    // Bind a contiguous table starting at the first (slot 0) texture's SRV.
    const auto it = guest_texture_srv_index_.find(0);
    if (it != guest_texture_srv_index_.end()) {
        texture_cache_.SetGraphicsRootTexture(command_list_.Get(), srv_root_idx, it->second);
    }
}

void D3D12GpuBackend::DrawArrays(PrimitiveTopology topology, u32 first_vertex, u32 vertex_count) {
    stats_.draw_calls++;
    stats_.vertices_submitted += vertex_count;
    if (!in_frame_ || !command_list_ || !IsRenderPipelineReady() || vertices_.empty()) {
        return;
    }
    UploadGeometry();
    if (!BindTranslatedPipeline(topology)) {
        BindPipelineAndTopology(topology);
    }
    command_list_->DrawInstanced(vertex_count, 1, first_vertex, 0);
}

void D3D12GpuBackend::DrawIndexed(PrimitiveTopology topology, u32 index_count, u32 first_index, u32 base_vertex) {
    stats_.draw_calls++;
    stats_.vertices_submitted += index_count;
    if (!in_frame_ || !command_list_ || !IsRenderPipelineReady() || vertices_.empty()) {
        return;
    }
    UploadGeometry();
    if (!BindTranslatedPipeline(topology)) {
        BindPipelineAndTopology(topology);
    }
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

// FNV-1a 64-bit over a byte range (matches the intent of the pipeline key).
static u64 HashBytes64(const void* data, size_t len) noexcept {
    const auto* p = static_cast<const u8*>(data);
    u64 h = 0xcbf29ce484222325ULL;
    for (size_t i = 0; i < len; ++i) {
        h ^= p[i];
        h *= 0x100000001b3ULL;
    }
    return h;
}

bool D3D12GpuBackend::BindTranslatedPipeline(PrimitiveTopology topology) {
    // Translation path only engages when a real guest VS+PS pair is present.
    if (guest_vs_.empty() || guest_ps_.empty() || !device_ || !command_list_) {
        return false;
    }

    // Stable cache key derived from the guest shader bytecode + draw state.
    pipeline::PipelineStateKey key;
    key.vs_bytecode_hash = HashBytes64(guest_vs_.data(), guest_vs_.size());
    key.ps_bytecode_hash = HashBytes64(guest_ps_.data(), guest_ps_.size());
    key.topology = topology;
    key.cull_mode = current_rasterizer_state_.cull_face_enable ?
        (current_rasterizer_state_.cull_face == 0x0404 ? pipeline::CullMode::Front : pipeline::CullMode::Back) : pipeline::CullMode::None;
    key.depth_test_enable = current_rasterizer_state_.depth_test_enable;
    key.depth_write_enable = current_rasterizer_state_.depth_write_enable;
    switch (current_rasterizer_state_.depth_func) {
        case 0x0200: key.depth_func = pipeline::DepthFunc::Never; break;
        case 0x0201: key.depth_func = pipeline::DepthFunc::Less; break;
        case 0x0202: key.depth_func = pipeline::DepthFunc::Equal; break;
        case 0x0203: key.depth_func = pipeline::DepthFunc::LessEqual; break;
        case 0x0204: key.depth_func = pipeline::DepthFunc::Greater; break;
        case 0x0205: key.depth_func = pipeline::DepthFunc::NotEqual; break;
        case 0x0206: key.depth_func = pipeline::DepthFunc::GreaterEqual; break;
        case 0x0207: key.depth_func = pipeline::DepthFunc::Always; break;
        default: key.depth_func = pipeline::DepthFunc::Less; break;
    }
    key.blend_enable = current_rasterizer_state_.blend_enable_0;
    key.num_render_targets = std::clamp<u8>(current_rasterizer_state_.num_render_targets, 1, 8);
    for (u8 i = 0; i < 8; ++i) {
        key.rtv_formats[i] = current_rasterizer_state_.rtv_formats[i];
    }
    key.dsv_format = current_rasterizer_state_.dsv_format;
    key.num_cbufs = guest_num_cbufs_;
    key.num_textures = guest_texture_count_;
    key.vertex_attrib_count = guest_vertex_attrib_count_;
    for (u8 i = 0; i < guest_vertex_attrib_count_ && i < pipeline::PipelineStateKey::kMaxVertexAttribs; ++i) {
        auto& va = key.vertex_attribs[i];
        const auto& ga = guest_vertex_attribs_[i];
        va.attr_index = ga.attr_index;
        va.format = ga.format_id;
        va.offset = ga.offset;
        va.slot = ga.slot;
        va.stride = ga.stride;
        va.valid = ga.valid;
    }

    // Build/retrieve the translated PSO through the full chain (decode ->
    // HLSL -> validate -> D3DCompile -> PSO). This is the heart of the
    // Maxwell->Direct3D translation layer.
    auto result = pipeline::PipelineBridge::Build(guest_vs_, guest_ps_, key, &pipeline_cache_);
    if (!result.Passed()) {
        NEMU_LOG_WARN("D3D12", "Guest shader translation failed ({}); using passthrough", result.error);
        translated_key_valid_ = false;
        return false;
    }

    ID3D12PipelineState* translated_pso = pipeline_cache_.GetPipelineState(key);
    ID3D12RootSignature* translated_root = pipeline_cache_.GetRootSignature(key);
    if (!translated_pso) {
        return false;
    }

    command_list_->SetPipelineState(translated_pso);
    if (translated_root) {
        command_list_->SetGraphicsRootSignature(translated_root);
    }
    if (guest_num_cbufs_ > 0) {
        BindGuestConstantBuffers(0);
    }
    if (guest_texture_count_ > 0) {
        BindGuestTextures();
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

    // Bind geometry. When a guest vertex buffer is present (P2-4 guest path),
    // bind it with the guest stride; otherwise bind the RasterVertex geometry.
    D3D12_VERTEX_BUFFER_VIEW view{};
    if (guest_vertex_buffer_valid_ && guest_vertex_buffer_) {
        view.BufferLocation = guest_vertex_buffer_->GetGPUVirtualAddress();
        view.StrideInBytes = guest_vertex_stride_;
        view.SizeInBytes = guest_vertex_buffer_size_;
        command_list_->IASetVertexBuffers(0, 1, &view);
    } else if (vertex_buffer_) {
        const UINT stride = sizeof(D3D12Vertex);
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

    translated_key_ = key;
    translated_key_valid_ = true;
    return true;
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

D3D12GpuBackend::PipelineValidation D3D12GpuBackend::GetPipelineValidation() const noexcept {
    PipelineValidation v;
    v.device_created = device_ != nullptr;
    v.command_queue_created = command_queue_ != nullptr;
    v.command_list_created = command_list_ != nullptr;
    v.swap_chain_created = swap_chain_ != nullptr;
    v.root_signature_created = root_signature_ != nullptr;
    v.pso_created = pso_ != nullptr;
    v.geometry_upload_ok = vertex_buffer_ != nullptr || back_buffers_.empty();
    v.back_buffer_count = static_cast<u32>(back_buffers_.size());
    v.last_hr = hr_;
    return v;
}

} // namespace nemu::core::gpu
#endif // _WIN32