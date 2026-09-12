#include "core/gpu/null_backend.hpp"
#include "core/gpu/maxwell_3d.hpp"
#include "core/gpu/gpu_factory.hpp"
#include "core/gpu/pipeline/pipeline_cache.hpp"
#include "core/gpu/texture/texture_cache.hpp"
#include "core/gpu/texture/texture_types.hpp"
#include "core/types.hpp"
#include <iostream>
#include <cstdlib>
#include <cstdio>

using namespace nemu;
using namespace nemu::core::gpu;
using namespace nemu::core::gpu::pipeline;

#define RP_FIRST_(a, ...) a
#define RP_ASSERT(...) \
    do { \
        if (!(RP_FIRST_(__VA_ARGS__))) { \
            std::cerr << "Assertion failed: " #__VA_ARGS__ << " at " \
                      << __FILE__ << ":" << __LINE__ << std::endl; \
            std::exit(1); \
        } \
    } while (0)

// Returns float bits helper.
static u32 F(float v) noexcept {
    u32 u = 0;
    __builtin_memcpy(&u, &v, sizeof(u));
    return u;
}

void TestPipelineCache() {
    std::cout << "[Test: D3D12 Pipeline State Object (PSO) & Root Signature Cache]" << std::endl;

    PipelineCache cache;
    RP_ASSERT(cache.GetCachedPipelineCount() == 0, "initial cache empty");

    PipelineStateKey key1{
        .vs_bytecode_hash = 0x1122334455667788ULL,
        .ps_bytecode_hash = 0x99AABBCCDDEEFF00ULL,
        .topology = PrimitiveTopology::Triangles,
        .cull_mode = CullMode::Back,
        .depth_test_enable = true,
        .depth_write_enable = true,
        .depth_func = DepthFunc::LessEqual,
        .blend_enable = true,
        .src_rgb = BlendFactor::SrcAlpha,
        .dst_rgb = BlendFactor::InvSrcAlpha,
        .op_rgb = BlendOp::Add,
        .src_alpha = BlendFactor::One,
        .dst_alpha = BlendFactor::Zero,
        .op_alpha = BlendOp::Add,
        .num_cbufs = 2,
        .num_textures = 1
    };

    // First compilation (cache miss)
    RP_ASSERT(cache.GetOrCreatePipeline(key1), "compile pipeline key1");
    RP_ASSERT(cache.GetCachedPipelineCount() == 1, "cache count 1");
    RP_ASSERT(cache.GetCacheMisses() == 1, "cache misses 1");
    RP_ASSERT(cache.GetCacheHits() == 0, "cache hits 0");

    // Second query (cache hit)
    RP_ASSERT(cache.GetOrCreatePipeline(key1), "retrieve cached pipeline key1");
    RP_ASSERT(cache.GetCachedPipelineCount() == 1, "cache count stays 1");
    RP_ASSERT(cache.GetCacheMisses() == 1, "cache misses stays 1");
    RP_ASSERT(cache.GetCacheHits() == 1, "cache hits 1");

    // Query with distinct state (cull mode Front instead of Back)
    PipelineStateKey key2 = key1;
    key2.cull_mode = CullMode::Front;
    RP_ASSERT(cache.GetOrCreatePipeline(key2), "compile pipeline key2");
    RP_ASSERT(cache.GetCachedPipelineCount() == 2, "cache count 2");
    RP_ASSERT(cache.GetCacheMisses() == 2, "cache misses 2");
    RP_ASSERT(cache.GetCacheHits() == 1, "cache hits stays 1");

    // Clear cache
    cache.Clear();
    RP_ASSERT(cache.GetCachedPipelineCount() == 0, "cache cleared");
    RP_ASSERT(cache.GetCacheHits() == 0, "hits reset");
    RP_ASSERT(cache.GetCacheMisses() == 0, "misses reset");

    std::cout << "  - D3D12 Pipeline State Object (PSO) & Root Signature Cache: PASSED" << std::endl;
}

void TestTextureCache() {
    std::cout << "[Test: Texture Cache & TIC/TSC Descriptors]" << std::endl;

    using namespace nemu::core::gpu::texture;

    // 1. TIC Parsing
    const u32 tic_words[8] = {
        0x0000000Bu | (2u << 7) | (3u << 10) | (4u << 13) | (5u << 16), // RGBA8_UNORM, R/G/B/A
        0x00200000u,                                                     // addr low
        0x00000000u,                                                     // addr high, block-linear
        (63u << 0) | (63u << 16),                                        // 64x64
        0, 0, 0, 0
    };
    auto tic = TicParser::Parse(tic_words);
    RP_ASSERT(tic.format == TextureFormat::RGBA8_UNORM, "TIC format RGBA8");
    RP_ASSERT(tic.width == 64 && tic.height == 64, "TIC 64x64");
    RP_ASSERT(tic.gpu_address == 0x00200000ULL, "TIC GPU VA");
    RP_ASSERT(tic.is_block_linear == true, "TIC block linear");

    // 2. TSC Parsing
    const u32 tsc_words[8] = {
        0x00009000u | (0u << 0) | (0u << 3) | (0u << 6), // Repeat, Linear min/mag
        0, 0, 0, 0, 0, 0, 0
    };
    auto tsc = TscParser::Parse(tsc_words);
    RP_ASSERT(tsc.wrap_u == SamplerWrapMode::Repeat, "TSC wrap repeat");
    RP_ASSERT(tsc.min_filter == SamplerFilter::Linear, "TSC linear min");
    RP_ASSERT(tsc.mag_filter == SamplerFilter::Linear, "TSC linear mag");

    // 3. Texture Cache Instance
    TextureCache tex_cache;
    RP_ASSERT(tex_cache.Initialize(), "tex_cache init");
    RP_ASSERT(tex_cache.GetTextureCount() == 0, "initial tex count 0");
    RP_ASSERT(tex_cache.GetSamplerCount() == 0, "initial samp count 0");

    auto tex1 = tex_cache.GetOrCreateTexture(tic, nullptr);
    RP_ASSERT(tex1 != nullptr, "tex1 created");
    RP_ASSERT(tex1->is_valid, "tex1 valid");
    RP_ASSERT(tex_cache.GetTextureCount() == 1, "tex count 1");

    auto tex2 = tex_cache.GetOrCreateTexture(tic, nullptr);
    RP_ASSERT(tex1 == tex2, "tex2 hits cache");
    RP_ASSERT(tex_cache.GetTextureCount() == 1, "tex count still 1");

    auto samp1 = tex_cache.GetOrCreateSampler(tsc);
    RP_ASSERT(samp1 != nullptr, "samp1 created");
    RP_ASSERT(tex_cache.GetSamplerCount() == 1, "samp count 1");

    // Invalidation
    tex_cache.InvalidateRange(0x00200000ULL, 4096);
    RP_ASSERT(tex_cache.GetTextureCount() == 0, "tex invalidated");

    tex_cache.Shutdown();
    std::cout << "  - Texture Cache & TIC/TSC Descriptors: PASSED" << std::endl;
}

int main() {
    std::cout << "[Test: Guest-Driven Render Pipeline (pushbuffer -> rasterize -> frame)]" << std::endl;

    auto backendPtr = std::make_shared<NullGpuBackend>();
    RP_ASSERT(backendPtr->Initialize(256, 256));
    Maxwell3D m3d(backendPtr);

    // Guest-style pushbuffer: clear to a dark background, set a green draw, then
    // issue DrawArrays (3 vertices, Triangles).
    const u32 pb[] = {
        (4u << 16) | MaxwellMethod::ClearColorR, F(0.05f), F(0.05f), F(0.10f), F(1.0f),
        (1u << 16) | MaxwellMethod::ClearSurface, 1u,
        (1u << 16) | MaxwellMethod::DrawArrays, (3u << 8) | 3u
    };
    m3d.SubmitPushbuffer(std::span<const u32>(pb, sizeof(pb) / sizeof(pb[0])));

    // The software rasterizer should now hold a green-filled triangle on a dark
    // background. Center pixel (128,128) is inside the triangle -> strong green.
    const u8* fb = backendPtr->Framebuffer();
    const size_t mid = ((128u * 256u) + 128u) * 4u;
    RP_ASSERT(fb[mid + 0] < 60, "center is not red");
    RP_ASSERT(fb[mid + 1] > 200, "center is strong green (rasterized triangle)");
    RP_ASSERT(fb[mid + 2] < 120, "center is not blue-dominant");

    // A corner (2,2) is outside the triangle -> dark background.
    const size_t corner = ((2u * 256u) + 2u) * 4u;
    RP_ASSERT(fb[corner + 0] < 40 && fb[corner + 1] < 40, "corner stays on background");

    // Stats reflect a real draw.
    const auto stats = backendPtr->GetStats();
    RP_ASSERT(stats.draw_calls >= 1 && stats.vertices_submitted >= 3, "draw registered");

    // Dump the guest-driven frame for visual verification.
    RP_ASSERT(backendPtr->DumpFramePPM("/tmp/nemu_guest_frame.ppm"), "frame dump");
    std::cout << "  - Guest-driven render pipeline (pushbuffer -> rasterize -> frame): PASSED" << std::endl;

    TestPipelineCache();
    TestTextureCache();

    std::cout << "[Test: Guest-Driven Render Pipeline PASSED]" << std::endl;
    return 0;
}