#include "texture_cache.hpp"
#include "astc_decoder.hpp"
#include "core/gpu/deswizzle.hpp"
#include "platform/logger.hpp"
#include <cstring>
#include <algorithm>

namespace nemu::core::gpu::texture {

#ifdef _WIN32
TextureCache::TextureCache(ID3D12Device* device) : device_(device) {}
#else
TextureCache::TextureCache() = default;
#endif

TextureCache::~TextureCache() {
    Shutdown();
}

bool TextureCache::Initialize() {
    std::lock_guard<std::mutex> lock(mutex_);
#ifdef _WIN32
    if (!device_) {
        NEMU_LOG_DEBUG("gpu", "TextureCache: No D3D12 device provided (running in simulation mode)");
        return true;
    }

    // Allocate CBV/SRV/UAV descriptor heap
    D3D12_DESCRIPTOR_HEAP_DESC srv_desc{};
    srv_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    srv_desc.NumDescriptors = 2048;
    srv_desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    HRESULT hr = device_->CreateDescriptorHeap(&srv_desc, IID_PPV_ARGS(&srv_heap_));
    if (FAILED(hr)) {
        NEMU_LOG_ERROR("gpu", "Failed to create D3D12 SRV descriptor heap: 0x{:08X}", static_cast<u32>(hr));
        return false;
    }
    srv_descriptor_size_ = device_->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    srv_cpu_start_ = srv_heap_->GetCPUDescriptorHandleForHeapStart();
    srv_gpu_start_ = srv_heap_->GetGPUDescriptorHandleForHeapStart();

    // Allocate Sampler descriptor heap
    D3D12_DESCRIPTOR_HEAP_DESC samp_desc{};
    samp_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER;
    samp_desc.NumDescriptors = 64;
    samp_desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    hr = device_->CreateDescriptorHeap(&samp_desc, IID_PPV_ARGS(&sampler_heap_));
    if (FAILED(hr)) {
        NEMU_LOG_ERROR("gpu", "Failed to create D3D12 Sampler descriptor heap: 0x{:08X}", static_cast<u32>(hr));
        return false;
    }
    sampler_descriptor_size_ = device_->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER);
    sampler_cpu_start_ = sampler_heap_->GetCPUDescriptorHandleForHeapStart();
    sampler_gpu_start_ = sampler_heap_->GetGPUDescriptorHandleForHeapStart();

    NEMU_LOG_INFO("gpu", "TextureCache: D3D12 descriptor heaps initialized successfully");
#endif
    return true;
}

void TextureCache::Shutdown() {
    std::lock_guard<std::mutex> lock(mutex_);
    textures_.clear();
    samplers_.clear();
    next_srv_index_ = 0;
    next_sampler_index_ = 0;
#ifdef _WIN32
    srv_heap_.Reset();
    sampler_heap_.Reset();
#endif
}

std::shared_ptr<CachedTexture> TextureCache::GetOrCreateTexture(
    const TextureDescriptor& desc,
    memory::VirtualMemory* memory
) {
    std::lock_guard<std::mutex> lock(mutex_);

    TextureKey key{
        .gpu_address = desc.gpu_address,
        .width = desc.width,
        .height = desc.height,
        .format = desc.format,
        .is_block_linear = desc.is_block_linear,
    };

    auto it = textures_.find(key);
    if (it != textures_.end()) {
        return it->second;
    }

    auto cached = std::make_shared<CachedTexture>();
    cached->desc = desc;
    cached->srv_index = next_srv_index_++;
    cached->is_valid = false;

    // Allocate buffer for deswizzled linear data
    const size_t linear_size = desc.CalculateLinearSize();
    cached->linear_pixel_data.resize(linear_size > 0 ? linear_size : 16);

    // Read and convert texture data from guest memory if available
    if (memory && desc.gpu_address != 0) {
        if (desc.IsAstc()) {
            // ASTC compressed texture
            u32 bw = 4, bh = 4;
            switch (desc.format) {
                case TextureFormat::ASTC_4x4:   bw = 4; bh = 4; break;
                case TextureFormat::ASTC_5x4:   bw = 5; bh = 4; break;
                case TextureFormat::ASTC_5x5:   bw = 5; bh = 5; break;
                case TextureFormat::ASTC_6x5:   bw = 6; bh = 5; break;
                case TextureFormat::ASTC_6x6:   bw = 6; bh = 6; break;
                case TextureFormat::ASTC_8x5:   bw = 8; bh = 5; break;
                case TextureFormat::ASTC_8x6:   bw = 8; bh = 6; break;
                case TextureFormat::ASTC_8x8:   bw = 8; bh = 8; break;
                case TextureFormat::ASTC_10x5:  bw = 10; bh = 5; break;
                case TextureFormat::ASTC_10x6:  bw = 10; bh = 6; break;
                case TextureFormat::ASTC_10x8:  bw = 10; bh = 8; break;
                case TextureFormat::ASTC_10x10: bw = 10; bh = 10; break;
                case TextureFormat::ASTC_12x10: bw = 12; bh = 10; break;
                case TextureFormat::ASTC_12x12: bw = 12; bh = 12; break;
                default: break;
            }

            const u32 blocks_x = (desc.width + bw - 1) / bw;
            const u32 blocks_y = (desc.height + bh - 1) / bh;
            const size_t astc_bytes = static_cast<size_t>(blocks_x) * blocks_y * AstcDecoder::BLOCK_SIZE_BYTES;

            std::vector<u8> raw_astc(astc_bytes);
            if (memory->ReadBlock(desc.gpu_address, raw_astc.data(), astc_bytes)) {
                std::vector<u32> decoded_rgba(desc.width * desc.height);
                AstcDecoder::DecompressSurface(raw_astc, desc.width, desc.height, bw, bh, decoded_rgba);
                std::memcpy(cached->linear_pixel_data.data(), decoded_rgba.data(), decoded_rgba.size() * sizeof(u32));
                cached->is_valid = true;
            }
        } else if (desc.is_block_linear) {
            // Tegra GM20B block-linear format
            std::vector<u8> raw_swizzled(linear_size);
            if (memory->ReadBlock(desc.gpu_address, raw_swizzled.data(), linear_size)) {
                TextureSwizzler::DeswizzleBlockLinear(
                    raw_swizzled,
                    cached->linear_pixel_data,
                    desc.width,
                    desc.height,
                    desc.bytes_per_pixel,
                    desc.block_height_gobs
                );
                cached->is_valid = true;
            }
        } else {
            // Standard linear pitch texture
            if (memory->ReadBlock(desc.gpu_address, cached->linear_pixel_data.data(), linear_size)) {
                cached->is_valid = true;
            }
        }
    } else {
        // Fallback procedural checkerboard pattern for test/debug
        cached->is_valid = true;
        for (u32 y = 0; y < desc.height; ++y) {
            for (u32 x = 0; x < desc.width; ++x) {
                const size_t offset = (static_cast<size_t>(y) * desc.width + x) * 4;
                if (offset + 4 <= cached->linear_pixel_data.size()) {
                    const u8 c = ((x ^ y) & 8) ? 255 : 0;
                    cached->linear_pixel_data[offset + 0] = c;
                    cached->linear_pixel_data[offset + 1] = c;
                    cached->linear_pixel_data[offset + 2] = 255;
                    cached->linear_pixel_data[offset + 3] = 255;
                }
            }
        }
    }

#ifdef _WIN32
    if (device_ && srv_heap_) {
        DXGI_FORMAT dxgi_fmt = DXGI_FORMAT_R8G8B8A8_UNORM;
        switch (desc.format) {
            case TextureFormat::RGBA8_SRGB: dxgi_fmt = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB; break;
            case TextureFormat::BGRA8_UNORM: dxgi_fmt = DXGI_FORMAT_B8G8R8A8_UNORM; break;
            case TextureFormat::BGRA8_SRGB: dxgi_fmt = DXGI_FORMAT_B8G8R8A8_UNORM_SRGB; break;
            case TextureFormat::BC1_UNORM: dxgi_fmt = DXGI_FORMAT_BC1_UNORM; break;
            case TextureFormat::BC1_SRGB: dxgi_fmt = DXGI_FORMAT_BC1_UNORM_SRGB; break;
            case TextureFormat::BC2_UNORM: dxgi_fmt = DXGI_FORMAT_BC2_UNORM; break;
            case TextureFormat::BC3_UNORM: dxgi_fmt = DXGI_FORMAT_BC3_UNORM; break;
            case TextureFormat::BC7_UNORM: dxgi_fmt = DXGI_FORMAT_BC7_UNORM; break;
            case TextureFormat::BC7_SRGB: dxgi_fmt = DXGI_FORMAT_BC7_UNORM_SRGB; break;
            case TextureFormat::R8_UNORM: dxgi_fmt = DXGI_FORMAT_R8_UNORM; break;
            case TextureFormat::R16_FLOAT: dxgi_fmt = DXGI_FORMAT_R16_FLOAT; break;
            case TextureFormat::R32_FLOAT: dxgi_fmt = DXGI_FORMAT_R32_FLOAT; break;
            case TextureFormat::RGBA16_FLOAT: dxgi_fmt = DXGI_FORMAT_R16G16B16A16_FLOAT; break;
            case TextureFormat::RGBA32_FLOAT: dxgi_fmt = DXGI_FORMAT_R32G32B32A32_FLOAT; break;
            default: dxgi_fmt = DXGI_FORMAT_R8G8B8A8_UNORM; break;
        }

        D3D12_RESOURCE_DESC res_desc{};
        res_desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        res_desc.Width = std::max(1u, desc.width);
        res_desc.Height = std::max(1u, desc.height);
        res_desc.DepthOrArraySize = 1;
        res_desc.MipLevels = 1;
        res_desc.Format = dxgi_fmt;
        res_desc.SampleDesc.Count = 1;
        res_desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
        res_desc.Flags = D3D12_RESOURCE_FLAG_NONE;

        D3D12_HEAP_PROPERTIES heap_props{};
        heap_props.Type = D3D12_HEAP_TYPE_DEFAULT;

        HRESULT hr = device_->CreateCommittedResource(
            &heap_props,
            D3D12_HEAP_FLAG_NONE,
            &res_desc,
            D3D12_RESOURCE_STATE_COPY_DEST,
            nullptr,
            IID_PPV_ARGS(&cached->resource)
        );

        if (SUCCEEDED(hr)) {
            cached->cpu_srv_handle.ptr = srv_cpu_start_.ptr + static_cast<SIZE_T>(cached->srv_index * srv_descriptor_size_);
            cached->gpu_srv_handle.ptr = srv_gpu_start_.ptr + static_cast<UINT64>(cached->srv_index * srv_descriptor_size_);

            D3D12_SHADER_RESOURCE_VIEW_DESC srv_view{};
            srv_view.Format = dxgi_fmt;
            srv_view.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
            srv_view.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
            srv_view.Texture2D.MipLevels = 1;

            device_->CreateShaderResourceView(cached->resource.Get(), &srv_view, cached->cpu_srv_handle);
        }
    }
#endif

    textures_[key] = cached;
    return cached;
}

std::shared_ptr<CachedSampler> TextureCache::GetOrCreateSampler(
    const SamplerDescriptor& desc
) {
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = samplers_.find(desc);
    if (it != samplers_.end()) {
        return it->second;
    }

    auto cached = std::make_shared<CachedSampler>();
    cached->desc = desc;
    cached->sampler_index = next_sampler_index_++;
    cached->is_valid = true;

#ifdef _WIN32
    if (device_ && sampler_heap_) {
        cached->cpu_handle.ptr = sampler_cpu_start_.ptr + static_cast<SIZE_T>(cached->sampler_index * sampler_descriptor_size_);
        cached->gpu_handle.ptr = sampler_gpu_start_.ptr + static_cast<UINT64>(cached->sampler_index * sampler_descriptor_size_);

        auto map_address_mode = [](SamplerWrapMode mode) noexcept -> D3D12_TEXTURE_ADDRESS_MODE {
            switch (mode) {
                case SamplerWrapMode::Repeat: return D3D12_TEXTURE_ADDRESS_MODE_WRAP;
                case SamplerWrapMode::MirroredRepeat: return D3D12_TEXTURE_ADDRESS_MODE_MIRROR;
                case SamplerWrapMode::ClampToEdge: return D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
                case SamplerWrapMode::ClampToBorder: return D3D12_TEXTURE_ADDRESS_MODE_BORDER;
                case SamplerWrapMode::MirrorClampToEdge: return D3D12_TEXTURE_ADDRESS_MODE_MIRROR_ONCE;
                default: return D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
            }
        };

        D3D12_SAMPLER_DESC samp{};
        samp.AddressU = map_address_mode(desc.wrap_u);
        samp.AddressV = map_address_mode(desc.wrap_v);
        samp.AddressW = map_address_mode(desc.wrap_w);

        if (desc.min_filter == SamplerFilter::Linear && desc.mag_filter == SamplerFilter::Linear) {
            samp.Filter = (desc.mip_filter == MipFilter::Linear) ?
                          D3D12_FILTER_MIN_MAG_MIP_LINEAR : D3D12_FILTER_MIN_MAG_LINEAR_MIP_POINT;
        } else if (desc.min_filter == SamplerFilter::Linear && desc.mag_filter == SamplerFilter::Nearest) {
            samp.Filter = D3D12_FILTER_MIN_LINEAR_MAG_MIP_POINT;
        } else if (desc.min_filter == SamplerFilter::Nearest && desc.mag_filter == SamplerFilter::Linear) {
            samp.Filter = D3D12_FILTER_MIN_POINT_MAG_LINEAR_MIP_POINT;
        } else {
            samp.Filter = D3D12_FILTER_MIN_MAG_MIP_POINT;
        }

        samp.MinLOD = desc.min_lod;
        samp.MaxLOD = desc.max_lod;
        samp.MipLODBias = desc.lod_bias;
        samp.MaxAnisotropy = 1;
        samp.ComparisonFunc = D3D12_COMPARISON_FUNC_ALWAYS;
        for (size_t i = 0; i < 4; ++i) {
            samp.BorderColor[i] = desc.border_color[i];
        }

        device_->CreateSampler(&samp, cached->cpu_handle);
    }
#endif

    samplers_[desc] = cached;
    return cached;
}

void TextureCache::InvalidateRange(u64 gpu_address, size_t size) {
    std::lock_guard<std::mutex> lock(mutex_);
    const u64 end = gpu_address + size;

    for (auto it = textures_.begin(); it != textures_.end();) {
        const u64 tex_start = it->first.gpu_address;
        const u64 tex_end = tex_start + it->second->desc.CalculateLinearSize();
        if (tex_start < end && tex_end > gpu_address) {
            it = textures_.erase(it);
        } else {
            ++it;
        }
    }
}

size_t TextureCache::GetTextureCount() const noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    return textures_.size();
}

size_t TextureCache::GetSamplerCount() const noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    return samplers_.size();
}

#ifdef _WIN32
void TextureCache::BindDescriptorHeaps(ID3D12GraphicsCommandList* cmd_list) {
    if (!cmd_list || !srv_heap_ || !sampler_heap_) return;
    ID3D12DescriptorHeap* heaps[] = { srv_heap_.Get(), sampler_heap_.Get() };
    cmd_list->SetDescriptorHeaps(2, heaps);
}

void TextureCache::SetGraphicsRootTexture(ID3D12GraphicsCommandList* cmd_list, UINT root_param_idx, u32 srv_index) {
    if (!cmd_list || !srv_heap_) return;
    D3D12_GPU_DESCRIPTOR_HANDLE gpu_handle{};
    gpu_handle.ptr = srv_gpu_start_.ptr + static_cast<UINT64>(srv_index * srv_descriptor_size_);
    cmd_list->SetGraphicsRootDescriptorTable(root_param_idx, gpu_handle);
}

void TextureCache::SetGraphicsRootSampler(ID3D12GraphicsCommandList* cmd_list, UINT root_param_idx, u32 sampler_index) {
    if (!cmd_list || !sampler_heap_) return;
    D3D12_GPU_DESCRIPTOR_HANDLE gpu_handle{};
    gpu_handle.ptr = sampler_gpu_start_.ptr + static_cast<UINT64>(sampler_index * sampler_descriptor_size_);
    cmd_list->SetGraphicsRootDescriptorTable(root_param_idx, gpu_handle);
}
#endif

} // namespace nemu::core::gpu::texture
