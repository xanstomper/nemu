#pragma once

#include "core/gpu/gpu_interface.hpp"
#include "core/gpu/pipeline/pipeline_cache.hpp"
#include "core/gpu/texture/texture_cache.hpp"
#include "core/gpu/texture/texture_types.hpp"
#include "core/memory/virtual_memory.hpp"
#include <vector>
#include <array>
#include <cstdint>
#include <unordered_map>

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

    void SetRasterVertices(std::span<const RasterVertex> vertices) override;
    void SetRasterIndices(std::span<const u32> indices) override;

    // Feed guest Maxwell shader bytecode into the translation pipeline. When
    // present (and translatable) the backend builds a real PSO through
    // PipelineBridge/PipelineCache; otherwise it falls back to the embedded
    // color-passthrough pipeline so rendering always works.
    void SetGuestShaders(std::span<const u8> vs_bytecode, std::span<const u8> ps_bytecode) override;

    // Feed guest shader constant-buffer data. Each `slot` maps to a root CBV
    // (b/slot) declared by the translated root signature built by PipelineCache
    // (root_params[0..num_cbufs-1] are CBVs in slot order). Data is copied into a
    // private upload resource aligned to D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT
    // and bound via SetGraphicsRootConstantBufferView at draw time.
    void SetGuestConstantBuffer(u32 slot, const void* data, u32 bytes) override;

    // Guest texture bindings: cache the descriptor and create/bind an SRV
    // descriptor table into the translated root signature at draw time.
    void SetGuestTextureBinding(u32 binding, const texture::TextureDescriptor& desc, memory::VirtualMemory* memory) override;
    void SetGuestSamplerBinding(u32 binding, const texture::SamplerDescriptor& desc) override;
    void SetGuestTextureCount(u32 count) override { guest_texture_count_ = count & 0xFu; }

    // Guest vertex-attribute layout -> the dynamic PSO input layout.
    void SetGuestVertexAttributes(std::span<const GuestVertexAttrib> attrs) override;

    // Raw guest vertex-buffer bytes -> upload + bind for the translated PSO.
    void SetGuestVertexBuffer(std::span<const u8> data, u32 stride) override;

    [[nodiscard]] GpuStats GetStats() const noexcept override { return stats_; }
    [[nodiscard]] std::string_view GetBackendName() const noexcept override { return "Direct3D 12 (Xbox Series S/X & Win32)"; }

    [[nodiscard]] bool IsDeviceCreated() const noexcept { return device_ != nullptr; }

    /// True once a working swap chain + pipeline exist (real rendering path).
    [[nodiscard]] bool IsRenderPipelineReady() const noexcept { return swap_chain_ && pso_; }

    /// Structured per-stage validation of the on-console D3D12 pipeline.
    /// Lets the frontend / diagnostics confirm each stage actually came up so a
    /// game can fall back gracefully instead of black-screening.
    struct PipelineValidation {
        bool device_created{false};
        bool command_queue_created{false};
        bool command_list_created{false};
        bool swap_chain_created{false};
        bool root_signature_created{false};
        bool pso_created{false};
        bool geometry_upload_ok{false};
        u32 back_buffer_count{0};
        HRESULT last_hr{S_OK};

        [[nodiscard]] bool RenderPipelineOk() const noexcept {
            return device_created && command_queue_created && swap_chain_created &&
                   root_signature_created && pso_created;
        }
    };
    [[nodiscard]] PipelineValidation GetPipelineValidation() const noexcept;

private:
    struct D3D12Vertex {
        float x, y;       // NDC
        float r, g, b, a; // color
    };

    void WaitForGpu();
    bool CreateSwapChainAndTargets();
    bool CreatePipelineAndBuffers();
    UINT CurrentBackBufferIndex() const noexcept { return static_cast<UINT>(back_buffer_index_); }
    D3D12_CPU_DESCRIPTOR_HANDLE CurrentRtv() const noexcept;
    void UploadGeometry();
    void BindPipelineAndTopology(PrimitiveTopology topology);
    bool BindTranslatedPipeline(PrimitiveTopology topology);
    void BindGuestConstantBuffers(UINT cbv_first_slot);
    void BindGuestTextures();
    void ReleaseGuestCbuffers();
    DXGI_FORMAT index_format() const noexcept { return DXGI_FORMAT_R32_UINT; }

    bool initialized_{false};
    bool in_frame_{false};
    u32 width_{1280};
    u32 height_{720};
    GpuStats stats_{};
    HRESULT hr_{S_OK};

    D3D12_VIEWPORT d3d_viewport_{};
    D3D12_RECT d3d_scissor_{};
    ClearColor clear_color_{0.0f, 0.0f, 0.0f, 1.0f};

    Microsoft::WRL::ComPtr<IDXGIFactory4> dxgi_factory_;
    Microsoft::WRL::ComPtr<ID3D12Device> device_;
    Microsoft::WRL::ComPtr<ID3D12CommandQueue> command_queue_;
    Microsoft::WRL::ComPtr<ID3D12CommandAllocator> command_allocator_;
    Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> command_list_;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> rtv_heap_;
    Microsoft::WRL::ComPtr<ID3D12Fence> fence_;
    UINT64 fence_value_{0};
    HANDLE fence_event_{nullptr};

    // Swap chain + render targets
    Microsoft::WRL::ComPtr<IDXGISwapChain3> swap_chain_;
    static constexpr UINT kBackBufferCount = 2;
    std::vector<Microsoft::WRL::ComPtr<ID3D12Resource>> back_buffers_;
    UINT back_buffer_index_{0};
    UINT rtv_descriptor_size_{0};

    // Pipeline state + geometry
    Microsoft::WRL::ComPtr<ID3D12RootSignature> root_signature_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> pso_;
    Microsoft::WRL::ComPtr<ID3D12Resource> vertex_buffer_;
    Microsoft::WRL::ComPtr<ID3D12Resource> index_buffer_;
    UINT vertex_buffer_size_{0};
    UINT index_buffer_size_{0};
    std::vector<RasterVertex> vertices_;
    std::vector<u32> indices_;

    // Translation-layer state: guest Maxwell shader programs and derived PSO.
    pipeline::PipelineCache pipeline_cache_;
    std::vector<u8> guest_vs_;
    std::vector<u8> guest_ps_;
    pipeline::PipelineStateKey translated_key_{};
    bool translated_key_valid_{false};

    // Guest constant buffers: one aligned upload resource per active slot, plus
    // the CPU copy used to refresh on the next draw.
    struct GuestCbuf {
        std::vector<u8> data;
        Microsoft::WRL::ComPtr<ID3D12Resource> upload;
        bool dirty{false};
    };
    static constexpr u32 kMaxGuestCbufSlots = 16;
    std::array<GuestCbuf, kMaxGuestCbufSlots> guest_cbufs_{};
    u32 guest_num_cbufs_{0};

    // Guest texture bindings: deswizzle/upload via TextureCache and bind an SRV
    // descriptor table (slot = shader register). Slots are sparse; binding is
    // contiguous as long as the guest binds consecutive registers starting at 0.
    texture::TextureCache texture_cache_;
    std::unordered_map<u32, texture::TextureDescriptor> guest_textures_;
    std::unordered_map<u32, texture::SamplerDescriptor> guest_samplers_;
    std::unordered_map<u32, u32> guest_texture_srv_index_;
    u32 guest_texture_count_{0};

    // Guest vertex-attribute layout for the dynamic PSO input layout.
    GuestVertexAttrib guest_vertex_attribs_[pipeline::PipelineStateKey::kMaxVertexAttribs]{};
    u8 guest_vertex_attrib_count_{0};

    // Guest vertex-buffer data uploaded for the translated PSO.
    std::vector<u8> guest_vertex_data_;
    Microsoft::WRL::ComPtr<ID3D12Resource> guest_vertex_buffer_;
    UINT guest_vertex_buffer_size_{0};
    UINT guest_vertex_stride_{0};
    bool guest_vertex_buffer_valid_{false};
};

} // namespace nemu::core::gpu
#endif // _WIN32
