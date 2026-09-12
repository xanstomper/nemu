#pragma once

#include "core/types.hpp"
#include "texture_types.hpp"
#include "core/memory/virtual_memory.hpp"
#include <unordered_map>
#include <vector>
#include <memory>
#include <mutex>
#include <span>
#include <optional>

#ifdef _WIN32
#include <d3d12.h>
#include <wrl/client.h>
#endif

namespace nemu::core::gpu::texture {

struct CachedTexture {
    TextureDescriptor desc{};
    u32 srv_index{0};
    bool is_valid{false};
    std::vector<u8> linear_pixel_data{}; // Deswizzled/decompressed pixel buffer

#ifdef _WIN32
    Microsoft::WRL::ComPtr<ID3D12Resource> resource{};
    D3D12_CPU_DESCRIPTOR_HANDLE cpu_srv_handle{};
    D3D12_GPU_DESCRIPTOR_HANDLE gpu_srv_handle{};
#endif
};

struct CachedSampler {
    SamplerDescriptor desc{};
    u32 sampler_index{0};
    bool is_valid{false};

#ifdef _WIN32
    D3D12_CPU_DESCRIPTOR_HANDLE cpu_handle{};
    D3D12_GPU_DESCRIPTOR_HANDLE gpu_handle{};
#endif
};

class TextureCache {
public:
#ifdef _WIN32
    explicit TextureCache(ID3D12Device* device = nullptr);
#else
    TextureCache();
#endif
    ~TextureCache();

    TextureCache(const TextureCache&) = delete;
    TextureCache& operator=(const TextureCache&) = delete;

    /// Initialize GPU descriptor heaps (CBV/SRV and Sampler)
    bool Initialize();

    /// Release resources
    void Shutdown();

    /// Find an existing cached texture or fetch/deswizzle/upload a new one from guest memory
    std::shared_ptr<CachedTexture> GetOrCreateTexture(
        const TextureDescriptor& desc,
        memory::VirtualMemory* memory = nullptr
    );

    /// Find or allocate a texture sampler
    std::shared_ptr<CachedSampler> GetOrCreateSampler(
        const SamplerDescriptor& desc
    );

    /// Invalidate cached textures when guest memory is overwritten
    void InvalidateRange(u64 gpu_address, size_t size);

    /// Total number of active textures in cache
    [[nodiscard]] size_t GetTextureCount() const noexcept;

    /// Total number of active samplers in cache
    [[nodiscard]] size_t GetSamplerCount() const noexcept;

#ifdef _WIN32
    /// Bind descriptor heaps to the active command list
    void BindDescriptorHeaps(ID3D12GraphicsCommandList* cmd_list);

    /// Set graphics root descriptor table for texture SRV
    void SetGraphicsRootTexture(ID3D12GraphicsCommandList* cmd_list, UINT root_param_idx, u32 srv_index);

    /// Set graphics root descriptor table for Sampler
    void SetGraphicsRootSampler(ID3D12GraphicsCommandList* cmd_list, UINT root_param_idx, u32 sampler_index);

    [[nodiscard]] ID3D12DescriptorHeap* GetSrvHeap() const noexcept { return srv_heap_.Get(); }
    [[nodiscard]] ID3D12DescriptorHeap* GetSamplerHeap() const noexcept { return sampler_heap_.Get(); }
#endif

private:
    struct TextureKey {
        u64 gpu_address{0};
        u32 width{0};
        u32 height{0};
        TextureFormat format{TextureFormat::Unknown};
        bool is_block_linear{false};

        bool operator==(const TextureKey& o) const noexcept = default;
    };

    struct TextureKeyHash {
        size_t operator()(const TextureKey& k) const noexcept {
            size_t h = std::hash<u64>{}(k.gpu_address);
            h ^= std::hash<u32>{}(k.width) + 0x9e3779b9 + (h << 6) + (h >> 2);
            h ^= std::hash<u32>{}(k.height) + 0x9e3779b9 + (h << 6) + (h >> 2);
            h ^= std::hash<u32>{}(static_cast<u32>(k.format)) + 0x9e3779b9 + (h << 6) + (h >> 2);
            h ^= std::hash<bool>{}(k.is_block_linear) + 0x9e3779b9 + (h << 6) + (h >> 2);
            return h;
        }
    };

    struct SamplerKeyHash {
        size_t operator()(const SamplerDescriptor& s) const noexcept {
            size_t h = static_cast<size_t>(s.wrap_u);
            h ^= (static_cast<size_t>(s.wrap_v) << 3);
            h ^= (static_cast<size_t>(s.wrap_w) << 6);
            h ^= (static_cast<size_t>(s.min_filter) << 9);
            h ^= (static_cast<size_t>(s.mag_filter) << 10);
            h ^= (static_cast<size_t>(s.mip_filter) << 11);
            return h;
        }
    };

    struct SamplerEqual {
        bool operator()(const SamplerDescriptor& a, const SamplerDescriptor& b) const noexcept {
            return a.wrap_u == b.wrap_u &&
                   a.wrap_v == b.wrap_v &&
                   a.wrap_w == b.wrap_w &&
                   a.min_filter == b.min_filter &&
                   a.mag_filter == b.mag_filter &&
                   a.mip_filter == b.mip_filter &&
                   a.min_lod == b.min_lod &&
                   a.max_lod == b.max_lod;
        }
    };

    mutable std::mutex mutex_;
    std::unordered_map<TextureKey, std::shared_ptr<CachedTexture>, TextureKeyHash> textures_;
    std::unordered_map<SamplerDescriptor, std::shared_ptr<CachedSampler>, SamplerKeyHash, SamplerEqual> samplers_;

    u32 next_srv_index_{0};
    u32 next_sampler_index_{0};

#ifdef _WIN32
    ID3D12Device* device_{nullptr};
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> srv_heap_;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> sampler_heap_;
    UINT srv_descriptor_size_{0};
    UINT sampler_descriptor_size_{0};
    D3D12_CPU_DESCRIPTOR_HANDLE srv_cpu_start_{};
    D3D12_GPU_DESCRIPTOR_HANDLE srv_gpu_start_{};
    D3D12_CPU_DESCRIPTOR_HANDLE sampler_cpu_start_{};
    D3D12_GPU_DESCRIPTOR_HANDLE sampler_gpu_start_{};
#endif
};

} // namespace nemu::core::gpu::texture
