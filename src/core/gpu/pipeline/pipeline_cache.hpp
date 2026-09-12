#pragma once

#include "core/types.hpp"
#include "core/gpu/gpu_interface.hpp"
#include <unordered_map>
#include <string>
#include <vector>
#include <mutex>
#include <memory>

#ifdef _WIN32
#include <d3d12.h>
#include <wrl/client.h>
#endif

namespace nemu::core::gpu::pipeline {

enum class BlendFactor : u8 {
    Zero = 0,
    One = 1,
    SrcColor = 2,
    InvSrcColor = 3,
    SrcAlpha = 4,
    InvSrcAlpha = 5,
    DestAlpha = 6,
    InvDestAlpha = 7,
    DestColor = 8,
    InvDestColor = 9
};

enum class BlendOp : u8 {
    Add = 0,
    Subtract = 1,
    RevSubtract = 2,
    Min = 3,
    Max = 4
};

enum class CullMode : u8 {
    None = 0,
    Front = 1,
    Back = 2
};

enum class DepthFunc : u8 {
    Never = 0,
    Less = 1,
    Equal = 2,
    LessEqual = 3,
    Greater = 4,
    NotEqual = 5,
    GreaterEqual = 6,
    Always = 7
};

/// Hardware Pipeline State Object (PSO) caching key. Uniquely identifies
/// the complete raster, blend, depth, topology, and shader program configuration.
struct PipelineStateKey {
    u64 vs_bytecode_hash{0};
    u64 ps_bytecode_hash{0};
    PrimitiveTopology topology{PrimitiveTopology::Triangles};
    CullMode cull_mode{CullMode::None};
    bool depth_test_enable{false};
    bool depth_write_enable{false};
    DepthFunc depth_func{DepthFunc::Less};
    bool blend_enable{false};
    BlendFactor src_rgb{BlendFactor::One};
    BlendFactor dst_rgb{BlendFactor::Zero};
    BlendOp op_rgb{BlendOp::Add};
    BlendFactor src_alpha{BlendFactor::One};
    BlendFactor dst_alpha{BlendFactor::Zero};
    BlendOp op_alpha{BlendOp::Add};
    u32 num_cbufs{0};
    u32 num_textures{0};

    bool operator==(const PipelineStateKey& other) const noexcept = default;
};

struct PipelineStateKeyHash {
    size_t operator()(const PipelineStateKey& k) const noexcept {
        size_t h = k.vs_bytecode_hash ^ (k.ps_bytecode_hash << 1);
        h ^= static_cast<size_t>(k.topology) << 2;
        h ^= static_cast<size_t>(k.cull_mode) << 4;
        h ^= (static_cast<size_t>(k.depth_test_enable) << 6) | (static_cast<size_t>(k.depth_write_enable) << 7);
        h ^= static_cast<size_t>(k.blend_enable) << 8;
        h ^= static_cast<size_t>(k.num_cbufs) << 12;
        h ^= static_cast<size_t>(k.num_textures) << 16;
        return h;
    }
};

/// Direct3D 12 Pipeline State Object (PSO) and Root Signature Cache.
/// Dynamically translates and compiles guest Maxwell draw pipeline state
/// into cached, native DirectX 12 hardware state for Xbox Series S/X.
class PipelineCache {
public:
#ifdef _WIN32
    explicit PipelineCache(ID3D12Device* device = nullptr);
#else
    PipelineCache();
#endif
    ~PipelineCache();

    /// Retrieve or compile a pipeline state object for the given state key.
    /// If not present, compiles the vertex and pixel HLSL sources into DXBC,
    /// constructs the RootSignature (with CBV and SRV ranges), and allocates the PSO.
    bool GetOrCreatePipeline(
        const PipelineStateKey& key,
        const std::string& vs_hlsl = "",
        const std::string& ps_hlsl = ""
    );

    [[nodiscard]] size_t GetCachedPipelineCount() const noexcept;
    [[nodiscard]] u64 GetCacheHits() const noexcept { return cache_hits_; }
    [[nodiscard]] u64 GetCacheMisses() const noexcept { return cache_misses_; }

#ifdef _WIN32
    void SetDevice(ID3D12Device* device) noexcept { device_ = device; }
    [[nodiscard]] ID3D12PipelineState* GetPipelineState(const PipelineStateKey& key) const;
    [[nodiscard]] ID3D12RootSignature* GetRootSignature(const PipelineStateKey& key) const;
#endif

    void Clear();

private:
    struct CachedPipeline {
        u64 id{0};
#ifdef _WIN32
        Microsoft::WRL::ComPtr<ID3D12PipelineState> pso;
        Microsoft::WRL::ComPtr<ID3D12RootSignature> root_signature;
#endif
        bool valid{false};
    };

    std::unordered_map<PipelineStateKey, CachedPipeline, PipelineStateKeyHash> cache_;
    mutable std::mutex mutex_;
    u64 next_id_{1};
    u64 cache_hits_{0};
    u64 cache_misses_{0};
#ifdef _WIN32
    ID3D12Device* device_{nullptr};
#endif
};

} // namespace nemu::core::gpu::pipeline
