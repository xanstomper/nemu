#pragma once

#include "core/types.hpp"
#include "astc_decoder.hpp"
#include <span>
#include <vector>
#include <memory>
#include <string>
#include <string_view>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <d3d12.h>
#include <wrl/client.h>
#endif

namespace nemu::core::gpu::texture {

/// Configuration for an ASTC DirectCompute decompression pass
struct AstcComputeDispatchInfo {
    u32 width{0};
    u32 height{0};
    u32 depth{1};
    AstcBlockDimension block_dim{AstcBlockDimension::Block4x4};
    bool is_srgb{false};
    bool output_bc7{true}; // If true, decompress into BC7 format; else RGBA8
};

/// Direct3D 12 DirectCompute ASTC decompressor.
/// Emulates the Switch Tegra X1 hardware ASTC decoder on host AMD/Nvidia GPUs
/// using compute shaders, avoiding CPU decompression stalls.
class AstcComputePipeline {
public:
#ifdef _WIN32
    explicit AstcComputePipeline(ID3D12Device* device);
#else
    AstcComputePipeline();
#endif
    ~AstcComputePipeline();

    /// Initialize D3D12 compute pipeline states, root signatures, and shader resources
    bool Initialize();

    /// Shutdown and release D3D12 resources
    void Shutdown();

    /// Check if DirectCompute hardware decompression is available on the current device
    [[nodiscard]] bool IsHardwareAccelerated() const noexcept { return is_initialized_; }

    /// Decompress an ASTC surface directly in GPU memory using DirectCompute
    /// @param cmd_list D3D12 graphics/compute command list to record dispatch commands
    /// @param src_astc_buffer GPU resource containing raw ASTC block stream
    /// @param dst_texture GPU resource texture destination (BC7 or RGBA8)
    /// @param info Surface dimensions and block footprint
    bool DecompressGpu(
#ifdef _WIN32
        ID3D12GraphicsCommandList* cmd_list,
        ID3D12Resource* src_astc_buffer,
        ID3D12Resource* dst_texture,
#else
        void* cmd_list,
        void* src_astc_buffer,
        void* dst_texture,
#endif
        const AstcComputeDispatchInfo& info
    );

    /// Decompresses a 3D volumetric ASTC texture slice by slice
    bool DecompressVolume(
        std::span<const u8> astc_data,
        u32 width,
        u32 height,
        u32 depth,
        u32 block_width,
        u32 block_height,
        std::vector<u32>& out_rgba8,
        bool is_srgb = false
    );

    /// Returns HLSL compute shader source for ASTC block decoding
    [[nodiscard]] static std::string_view GetComputeShaderSource(AstcBlockDimension dim) noexcept;

private:
    bool is_initialized_{false};

#ifdef _WIN32
    ID3D12Device* device_{nullptr};
    Microsoft::WRL::ComPtr<ID3D12RootSignature> root_signature_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> pso_rgba8_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> pso_bc7_;
#endif
};

} // namespace nemu::core::gpu::texture
