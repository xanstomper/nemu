#include "pipeline_cache.hpp"
#include "platform/logger.hpp"
#include <cstring>
#include <vector>
#include <array>
#include <algorithm>
#include <fstream>
#include <filesystem>
#include <string_view>

namespace {

static uint64_t HashShaderSource(std::string_view source) noexcept {
    uint64_t hash = 14695981039346656037ULL;
    for (char c : source) {
        hash ^= static_cast<uint8_t>(c);
        hash *= 1099511628211ULL;
    }
    return hash;
}

static const char* kDefaultVertexShader = R"(
struct VSOut {
    float4 pos : SV_Position;
    float4 color : TEXCOORD0;
};
VSOut main(float2 pos : POSITION, float4 color : TEXCOORD0) {
    VSOut o;
    o.pos = float4(pos, 0.0f, 1.0f);
    o.color = color;
    return o;
}
)";

static const char* kDefaultPixelShader = R"(
struct PSIn {
    float4 pos : SV_Position;
    float4 color : TEXCOORD0;
};
float4 main(PSIn input) : SV_Target {
    return input.color;
}
)";

} // anonymous namespace

#ifdef _WIN32
#include <d3dcompiler.h>

namespace {

static Microsoft::WRL::ComPtr<ID3DBlob> LoadShaderFromDisk(const std::string& filepath) {
    std::ifstream file(filepath, std::ios::binary | std::ios::ate);
    if (!file.is_open()) return nullptr;
    const std::streamsize size = file.tellg();
    if (size <= 0) return nullptr;
    file.seekg(0, std::ios::beg);

    Microsoft::WRL::ComPtr<ID3DBlob> blob;
    HRESULT hr = D3DCreateBlob(static_cast<SIZE_T>(size), &blob);
    if (FAILED(hr) || !blob) return nullptr;

    if (!file.read(reinterpret_cast<char*>(blob->GetBufferPointer()), size)) {
        return nullptr;
    }
    return blob;
}

static bool SaveShaderToDisk(const std::string& filepath, ID3DBlob* blob) {
    if (!blob || blob->GetBufferSize() == 0) return false;
    std::ofstream file(filepath, std::ios::binary);
    if (!file.is_open()) return false;
    file.write(reinterpret_cast<const char*>(blob->GetBufferPointer()), static_cast<std::streamsize>(blob->GetBufferSize()));
    return file.good();
}

D3D12_CULL_MODE ConvertCullMode(nemu::core::gpu::pipeline::CullMode mode) noexcept {
    using nemu::core::gpu::pipeline::CullMode;
    switch (mode) {
        case CullMode::Front: return D3D12_CULL_MODE_FRONT;
        case CullMode::Back: return D3D12_CULL_MODE_BACK;
        case CullMode::None:
        default: return D3D12_CULL_MODE_NONE;
    }
}

D3D12_COMPARISON_FUNC ConvertComparisonFunc(nemu::core::gpu::pipeline::DepthFunc func) noexcept {
    using nemu::core::gpu::pipeline::DepthFunc;
    switch (func) {
        case DepthFunc::Never: return D3D12_COMPARISON_FUNC_NEVER;
        case DepthFunc::Less: return D3D12_COMPARISON_FUNC_LESS;
        case DepthFunc::Equal: return D3D12_COMPARISON_FUNC_EQUAL;
        case DepthFunc::LessEqual: return D3D12_COMPARISON_FUNC_LESS_EQUAL;
        case DepthFunc::Greater: return D3D12_COMPARISON_FUNC_GREATER;
        case DepthFunc::NotEqual: return D3D12_COMPARISON_FUNC_NOT_EQUAL;
        case DepthFunc::GreaterEqual: return D3D12_COMPARISON_FUNC_GREATER_EQUAL;
        case DepthFunc::Always: return D3D12_COMPARISON_FUNC_ALWAYS;
        default: return D3D12_COMPARISON_FUNC_LESS;
    }
}

D3D12_BLEND ConvertBlendFactor(nemu::core::gpu::pipeline::BlendFactor factor) noexcept {
    using nemu::core::gpu::pipeline::BlendFactor;
    switch (factor) {
        case BlendFactor::Zero: return D3D12_BLEND_ZERO;
        case BlendFactor::One: return D3D12_BLEND_ONE;
        case BlendFactor::SrcColor: return D3D12_BLEND_SRC_COLOR;
        case BlendFactor::InvSrcColor: return D3D12_BLEND_INV_SRC_COLOR;
        case BlendFactor::SrcAlpha: return D3D12_BLEND_SRC_ALPHA;
        case BlendFactor::InvSrcAlpha: return D3D12_BLEND_INV_SRC_ALPHA;
        case BlendFactor::DestAlpha: return D3D12_BLEND_DEST_ALPHA;
        case BlendFactor::InvDestAlpha: return D3D12_BLEND_INV_DEST_ALPHA;
        case BlendFactor::DestColor: return D3D12_BLEND_DEST_COLOR;
        case BlendFactor::InvDestColor: return D3D12_BLEND_INV_DEST_COLOR;
        case BlendFactor::Src1Color: return D3D12_BLEND_SRC1_COLOR;
        case BlendFactor::InvSrc1Color: return D3D12_BLEND_INV_SRC1_COLOR;
        case BlendFactor::Src1Alpha: return D3D12_BLEND_SRC1_ALPHA;
        case BlendFactor::InvSrc1Alpha: return D3D12_BLEND_INV_SRC1_ALPHA;
        default: return D3D12_BLEND_ONE;
    }
}

D3D12_BLEND_OP ConvertBlendOp(nemu::core::gpu::pipeline::BlendOp op) noexcept {
    using nemu::core::gpu::pipeline::BlendOp;
    switch (op) {
        case BlendOp::Add: return D3D12_BLEND_OP_ADD;
        case BlendOp::Subtract: return D3D12_BLEND_OP_SUBTRACT;
        case BlendOp::RevSubtract: return D3D12_BLEND_OP_REV_SUBTRACT;
        case BlendOp::Min: return D3D12_BLEND_OP_MIN;
        case BlendOp::Max: return D3D12_BLEND_OP_MAX;
        default: return D3D12_BLEND_OP_ADD;
    }
}

} // anonymous namespace
#endif

namespace nemu::core::gpu::pipeline {

#ifdef _WIN32
PipelineCache::PipelineCache(ID3D12Device* device)
    : device_(device) {}
#else
PipelineCache::PipelineCache() = default;
#endif

PipelineCache::~PipelineCache() {
    Clear();
}

size_t PipelineCache::GetCachedPipelineCount() const noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    return cache_.size();
}

void PipelineCache::Clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    cache_.clear();
    next_id_ = 1;
    cache_hits_ = 0;
    cache_misses_ = 0;
}

bool PipelineCache::GetOrCreatePipeline(
    const PipelineStateKey& key,
    const std::string& vs_hlsl,
    const std::string& ps_hlsl
) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = cache_.find(key);
    if (it != cache_.end()) {
        cache_hits_++;
        return it->second.valid;
    }

    cache_misses_++;

#ifdef _WIN32
    if (!device_) {
        // Mock compile in headless mode
        CachedPipeline cp;
        cp.id = next_id_++;
        cp.valid = true;
        cache_.emplace(key, std::move(cp));
        return true;
    }

    // 1. Compile Shaders
    const char* vs_code = vs_hlsl.empty() ? kDefaultVertexShader : vs_hlsl.c_str();
    const char* ps_code = ps_hlsl.empty() ? kDefaultPixelShader : ps_hlsl.c_str();

    const u64 vs_hash = HashShaderSource(vs_code);
    const u64 ps_hash = HashShaderSource(ps_code);

    Microsoft::WRL::ComPtr<ID3DBlob> vs_blob, ps_blob;
    bool loaded_from_disk = false;

    if (!disk_cache_dir_.empty()) {
        const std::string vs_path = disk_cache_dir_ + "/" + std::to_string(vs_hash) + "_vs.dxbc";
        const std::string ps_path = disk_cache_dir_ + "/" + std::to_string(ps_hash) + "_ps.dxbc";
        vs_blob = LoadShaderFromDisk(vs_path);
        ps_blob = LoadShaderFromDisk(ps_path);
        if (vs_blob && ps_blob) {
            loaded_from_disk = true;
            disk_cache_hits_++;
        }
    }

    if (!loaded_from_disk) {
        Microsoft::WRL::ComPtr<ID3DBlob> vs_err, ps_err;
        HRESULT hr = D3DCompile(vs_code, std::strlen(vs_code), "MaxwellVS", nullptr, nullptr,
                                "main", "vs_5_0", 0, 0, &vs_blob, &vs_err);
        if (FAILED(hr)) {
            NEMU_LOG_WARN("D3D12", "PipelineCache: VS compile error 0x{:08X}", static_cast<u32>(hr));
            return false;
        }

        hr = D3DCompile(ps_code, std::strlen(ps_code), "MaxwellPS", nullptr, nullptr,
                        "main", "ps_5_0", 0, 0, &ps_blob, &ps_err);
        if (FAILED(hr)) {
            NEMU_LOG_WARN("D3D12", "PipelineCache: PS compile error 0x{:08X}", static_cast<u32>(hr));
            return false;
        }

        if (!disk_cache_dir_.empty()) {
            const std::string vs_path = disk_cache_dir_ + "/" + std::to_string(vs_hash) + "_vs.dxbc";
            const std::string ps_path = disk_cache_dir_ + "/" + std::to_string(ps_hash) + "_ps.dxbc";
            if (SaveShaderToDisk(vs_path, vs_blob.Get()) && SaveShaderToDisk(ps_path, ps_blob.Get())) {
                disk_cache_writes_++;
            }
        }
    }

    // 2. Build Root Signature with CBVs and SRVs
    std::vector<D3D12_ROOT_PARAMETER> root_params;
    std::vector<D3D12_DESCRIPTOR_RANGE> srv_ranges;

    // Constant buffers (cbufs)
    for (u32 i = 0; i < key.num_cbufs; ++i) {
        D3D12_ROOT_PARAMETER param{};
        param.ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
        param.Descriptor.ShaderRegister = i;
        param.Descriptor.RegisterSpace = 0;
        param.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
        root_params.push_back(param);
    }

    // Textures (SRVs)
    if (key.num_textures > 0) {
        D3D12_DESCRIPTOR_RANGE range{};
        range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        range.NumDescriptors = key.num_textures;
        range.BaseShaderRegister = 0;
        range.RegisterSpace = 0;
        range.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
        srv_ranges.push_back(range);

        D3D12_ROOT_PARAMETER param{};
        param.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        param.DescriptorTable.NumDescriptorRanges = 1;
        param.DescriptorTable.pDescriptorRanges = srv_ranges.data();
        param.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
        root_params.push_back(param);
    }

    D3D12_STATIC_SAMPLER_DESC sampler{};
    sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    sampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    sampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    sampler.ShaderRegister = 0;
    sampler.RegisterSpace = 0;
    sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    D3D12_ROOT_SIGNATURE_DESC root_desc{};
    root_desc.NumParameters = static_cast<UINT>(root_params.size());
    root_desc.pParameters = root_params.empty() ? nullptr : root_params.data();
    root_desc.NumStaticSamplers = (key.num_textures > 0) ? 1 : 0;
    root_desc.pStaticSamplers = (key.num_textures > 0) ? &sampler : nullptr;
    root_desc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

    Microsoft::WRL::ComPtr<ID3DBlob> sig_blob, sig_err;
    HRESULT hr = D3D12SerializeRootSignature(&root_desc, D3D_ROOT_SIGNATURE_VERSION_1, &sig_blob, &sig_err);
    if (FAILED(hr)) {
        NEMU_LOG_WARN("D3D12", "PipelineCache: SerializeRootSignature error 0x{:08X}", static_cast<u32>(hr));
        return false;
    }

    Microsoft::WRL::ComPtr<ID3D12RootSignature> root_sig;
    hr = device_->CreateRootSignature(0, sig_blob->GetBufferPointer(), sig_blob->GetBufferSize(),
                                      IID_PPV_ARGS(&root_sig));
    if (FAILED(hr)) {
        NEMU_LOG_WARN("D3D12", "PipelineCache: CreateRootSignature error 0x{:08X}", static_cast<u32>(hr));
        return false;
    }

    // 3. Build Graphics Pipeline State
    // The input layout must agree with the MaxwellShaderDecoder HLSL emitter.
    // When the guest supplies a vertex-attribute layout (P2-4 generalization)
    // we render it into D3D12_INPUT_ELEMENT_DESC dynamically; otherwise we fall
    // back to the default `POSITION` + `TEXCOORD0` (one RasterVertex).
    std::array<D3D12_INPUT_ELEMENT_DESC, PipelineStateKey::kMaxVertexAttribs + 1> layout{};
    size_t layout_count = 0;
    layout[layout_count++] = {"POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0,
                              D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0};
    if (key.vertex_attrib_count > 0) {
        const u8 count = std::min<u8>(key.vertex_attrib_count, PipelineStateKey::kMaxVertexAttribs);
        for (u8 i = 0; i < count; ++i) {
            const auto& a = key.vertex_attribs[i];
            if (!a.valid) {
                continue;
            }
            if (layout_count >= layout.size()) {
                break;
            }
            layout[layout_count].SemanticName = "TEXCOORD";
            layout[layout_count].SemanticIndex = a.attr_index;
            layout[layout_count].Format = static_cast<DXGI_FORMAT>(a.format);
            layout[layout_count].InputSlot = a.slot;
            layout[layout_count].AlignedByteOffset = a.offset;
            layout[layout_count].InputSlotClass = D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA;
            layout[layout_count].InstanceDataStepRate = 0;
            ++layout_count;
        }
    } else {
        // Default one-RasterVertex layout: POSITION(xy) + TEXCOORD0(RGBA color).
        layout[layout_count++] = {"TEXCOORD", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 8,
                                  D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0};
    }

    D3D12_GRAPHICS_PIPELINE_STATE_DESC pso_desc{};
    pso_desc.pRootSignature = root_sig.Get();
    pso_desc.VS = {vs_blob->GetBufferPointer(), vs_blob->GetBufferSize()};
    pso_desc.PS = {ps_blob->GetBufferPointer(), ps_blob->GetBufferSize()};
    pso_desc.InputLayout = {layout.data(), static_cast<UINT>(layout_count)};

    // Topology
    switch (key.topology) {
        case PrimitiveTopology::Points:
            pso_desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_POINT;
            break;
        case PrimitiveTopology::Lines:
        case PrimitiveTopology::LineStrip:
            pso_desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_LINE;
            break;
        case PrimitiveTopology::Triangles:
        case PrimitiveTopology::TriangleStrip:
        default:
            pso_desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
            break;
    }

    const UINT num_rts = std::clamp<UINT>(key.num_render_targets, 1, 8);
    pso_desc.NumRenderTargets = num_rts;
    for (UINT i = 0; i < num_rts; ++i) {
        pso_desc.RTVFormats[i] = (key.rtv_formats[i] != 0)
            ? static_cast<DXGI_FORMAT>(key.rtv_formats[i])
            : DXGI_FORMAT_R8G8B8A8_UNORM;
    }
    pso_desc.SampleDesc = {.Count = 1, .Quality = 0};

    // Rasterizer State
    pso_desc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    pso_desc.RasterizerState.CullMode = ConvertCullMode(key.cull_mode);
    pso_desc.RasterizerState.FrontCounterClockwise = FALSE;
    pso_desc.RasterizerState.DepthBias = 0;
    pso_desc.RasterizerState.DepthBiasClamp = 0.0f;
    pso_desc.RasterizerState.SlopeScaledDepthBias = 0.0f;
    pso_desc.RasterizerState.DepthClipEnable = TRUE;

    // Depth Stencil State
    pso_desc.DepthStencilState.DepthEnable = key.depth_test_enable ? TRUE : FALSE;
    pso_desc.DepthStencilState.DepthWriteMask = key.depth_write_enable ? D3D12_DEPTH_WRITE_MASK_ALL : D3D12_DEPTH_WRITE_MASK_ZERO;
    pso_desc.DepthStencilState.DepthFunc = ConvertComparisonFunc(key.depth_func);
    pso_desc.DepthStencilState.StencilEnable = FALSE;
    if (key.depth_test_enable || key.depth_write_enable) {
        pso_desc.DSVFormat = (key.dsv_format != 0)
            ? static_cast<DXGI_FORMAT>(key.dsv_format)
            : DXGI_FORMAT_D32_FLOAT;
    } else {
        pso_desc.DSVFormat = DXGI_FORMAT_UNKNOWN;
    }

    // Blend State
    pso_desc.BlendState.AlphaToCoverageEnable = FALSE;
    pso_desc.BlendState.IndependentBlendEnable = FALSE;
    auto& rt_blend = pso_desc.BlendState.RenderTarget[0];
    rt_blend.BlendEnable = key.blend_enable ? TRUE : FALSE;
    rt_blend.LogicOpEnable = FALSE;
    rt_blend.SrcBlend = ConvertBlendFactor(key.src_rgb);
    rt_blend.DestBlend = ConvertBlendFactor(key.dst_rgb);
    rt_blend.BlendOp = ConvertBlendOp(key.op_rgb);
    rt_blend.SrcBlendAlpha = ConvertBlendFactor(key.src_alpha);
    rt_blend.DestBlendAlpha = ConvertBlendFactor(key.dst_alpha);
    rt_blend.BlendOpAlpha = ConvertBlendOp(key.op_alpha);
    rt_blend.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;

    for (UINT i = 1; i < num_rts; ++i) {
        pso_desc.BlendState.RenderTarget[i] = rt_blend;
    }

    Microsoft::WRL::ComPtr<ID3D12PipelineState> pso;
    hr = device_->CreateGraphicsPipelineState(&pso_desc, IID_PPV_ARGS(&pso));
    if (FAILED(hr)) {
        NEMU_LOG_WARN("D3D12", "PipelineCache: CreateGraphicsPipelineState error 0x{:08X}", static_cast<u32>(hr));
        return false;
    }

    CachedPipeline cp;
    cp.id = next_id_++;
    cp.pso = std::move(pso);
    cp.root_signature = std::move(root_sig);
    cp.valid = true;
    cache_.emplace(key, std::move(cp));
    return true;
#else
    // Headless/POSIX pipeline caching simulation
    if (!disk_cache_dir_.empty()) {
        const uint64_t vs_hash = HashShaderSource(vs_hlsl.empty() ? kDefaultVertexShader : vs_hlsl);
        const uint64_t ps_hash = HashShaderSource(ps_hlsl.empty() ? kDefaultPixelShader : ps_hlsl);
        const std::string vs_path = disk_cache_dir_ + "/" + std::to_string(vs_hash) + "_vs.dxbc";
        const std::string ps_path = disk_cache_dir_ + "/" + std::to_string(ps_hash) + "_ps.dxbc";
        std::error_code ec;
        if (std::filesystem::exists(vs_path, ec) && std::filesystem::exists(ps_path, ec)) {
            disk_cache_hits_++;
        } else {
            std::ofstream fvs(vs_path, std::ios::binary);
            if (fvs.is_open()) fvs << vs_hlsl;
            std::ofstream fps(ps_path, std::ios::binary);
            if (fps.is_open()) fps << ps_hlsl;
            disk_cache_writes_++;
        }
    }
    CachedPipeline cp;
    cp.id = next_id_++;
    cp.valid = true;
    cache_.emplace(key, std::move(cp));
    return true;
#endif
}

void PipelineCache::SetDiskCacheDirectory(std::string path) {
    std::lock_guard<std::mutex> lock(mutex_);
    disk_cache_dir_ = std::move(path);
    if (!disk_cache_dir_.empty()) {
        std::error_code ec;
        std::filesystem::create_directories(disk_cache_dir_, ec);
    }
}

#ifdef _WIN32
ID3D12PipelineState* PipelineCache::GetPipelineState(const PipelineStateKey& key) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = cache_.find(key);
    if (it != cache_.end() && it->second.valid) {
        return it->second.pso.Get();
    }
    return nullptr;
}

ID3D12RootSignature* PipelineCache::GetRootSignature(const PipelineStateKey& key) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = cache_.find(key);
    if (it != cache_.end() && it->second.valid) {
        return it->second.root_signature.Get();
    }
    return nullptr;
}
#endif

} // namespace nemu::core::gpu::pipeline
