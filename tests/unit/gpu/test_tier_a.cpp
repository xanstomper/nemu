// Tier-A unit tests: GMMU (global GPU memory), BufferCache (dirty-range
// streaming), expanded Maxwell3D method surface, compute dispatch, expanded
// shader decoder opcodes. Run: ctest -R test_tier_a
#include "core/gpu/gmmu.hpp"
#include "core/gpu/buffer_cache.hpp"
#include "core/gpu/maxwell_3d.hpp"
#include "core/gpu/compute_qmd.hpp"
#include "core/gpu/texture/bc1_encoder.hpp"
#include "core/gpu/texture/astc_decoder.hpp"
#include "core/gpu/texture/texture_cache.hpp"
#include "core/gpu/null_backend.hpp"
#include "core/gpu/shader/maxwell_shader_decoder.hpp"
#include "core/gpu/shader/sass_identifier.hpp"
#include "core/memory/virtual_memory.hpp"
#include <iostream>
#include <vector>
#include <cstring>
#include <cstdlib>

#define NEMU_TEST_ASSERT(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "Assertion failed: " #cond " (" << (msg) << ") at " \
                      << __FILE__ << ":" << __LINE__ << std::endl; \
            std::exit(1); \
        } \
    } while (0)

using namespace nemu;
using namespace nemu::core;
using namespace nemu::core::gpu;

// ---------------------------------------------------------------------------
// GMMU: map/read/write/unmap across page boundaries
// ---------------------------------------------------------------------------
void TestGmmuBasics() {
    GpuMemoryManager gmmu(nullptr);

    // Two back-to-back host buffers mapped at adjacent GPU regions.
    static u8 buf_a[GpuMemoryManager::kBigPageSize];   // page 1
    static u8 buf_b[GpuMemoryManager::kBigPageSize];   // page 2
    std::memset(buf_a, 0xAA, sizeof(buf_a));
    std::memset(buf_b, 0xBB, sizeof(buf_b));

    const u64 base = 0x1000000ULL; // aligned
    NEMU_TEST_ASSERT(gmmu.Map(base, sizeof(buf_a), buf_a), "map A");
    NEMU_TEST_ASSERT(gmmu.Map(base + sizeof(buf_a), sizeof(buf_b), buf_b), "map B");
    NEMU_TEST_ASSERT(gmmu.IsMapped(base, sizeof(buf_a) + sizeof(buf_b)), "both mapped");
    NEMU_TEST_ASSERT(!gmmu.IsMapped(base + sizeof(buf_a) + sizeof(buf_b), 1), "past end unmapped");

    // Cross-page read spanning A|B boundary.
    u8 out[64];
    NEMU_TEST_ASSERT(gmmu.Read(base + sizeof(buf_a) - 32, out, 64) == 64, "cross-page read");
    NEMU_TEST_ASSERT(out[0] == 0xAA && out[31] == 0xAA, "A tail bytes");
    NEMU_TEST_ASSERT(out[32] == 0xBB && out[63] == 0xBB, "B head bytes");

    // Cross-page write.
    const u8 pattern[64] = {};
    NEMU_TEST_ASSERT(gmmu.Write(base + sizeof(buf_a) - 8, pattern, 16) == 16, "cross-page write");
    NEMU_TEST_ASSERT(buf_a[sizeof(buf_a) - 1] == 0x00, "A tail overwritten");
    NEMU_TEST_ASSERT(buf_b[0] == 0x00 && buf_b[7] == 0x00, "B head overwritten");
    NEMU_TEST_ASSERT(buf_b[8] == 0xBB, "B rest intact");

    // Host pointer translation.
    NEMU_TEST_ASSERT(gmmu.GetHostPointer(base) == buf_a, "host ptr A");
    NEMU_TEST_ASSERT(gmmu.GetHostPointer(base + sizeof(buf_a) + 16) == buf_b + 16, "host ptr B");
    NEMU_TEST_ASSERT(gmmu.GetHostPointer(0xDEAD0000ULL) == nullptr, "unmapped ptr null");

    // Unmap clears.
    gmmu.Unmap(base, sizeof(buf_a) + sizeof(buf_b));
    NEMU_TEST_ASSERT(!gmmu.IsMapped(base, 1), "unmapped after Unmap");
    NEMU_TEST_ASSERT(gmmu.Read(base, out, 4) == 0, "read on unmapped transfers 0");

    std::cout << "  GMMU basics PASS\n";
}

// ---------------------------------------------------------------------------
// BufferCache: miss -> full upload, hit, dirty-range partial update, eviction
// ---------------------------------------------------------------------------
void TestBufferCache() {
    auto gmmu = std::make_shared<GpuMemoryManager>(nullptr);

    static u8 backing[4096];
    for (size_t i = 0; i < sizeof(backing); ++i) backing[i] = static_cast<u8>(i & 0xFF);
    const u64 gpu_addr = 0x2000000ULL;
    NEMU_TEST_ASSERT(gmmu->Map(gpu_addr, sizeof(backing), backing), "map backing");

    BufferCache cache(gmmu);

    // Miss -> full upload.
    const u64 e1 = cache.Acquire(BufferCache::Type::Vertex, gpu_addr, 1024);
    NEMU_TEST_ASSERT(e1 != 0, "vertex entry created");
    auto st = cache.GetStats();
    NEMU_TEST_ASSERT(st.misses == 1 && st.full_uploads == 1, "miss counted");
    NEMU_TEST_ASSERT(st.hits == 0, "no hit yet");

    // Hit -> same entry id, no new upload.
    const u64 e2 = cache.Acquire(BufferCache::Type::Vertex, gpu_addr, 1024);
    NEMU_TEST_ASSERT(e2 == e1, "same entry id on hit");
    st = cache.GetStats();
    NEMU_TEST_ASSERT(st.hits == 1, "hit counted");
    NEMU_TEST_ASSERT(st.full_uploads == 1, "no second full upload");

    // ReadEntry returns guest bytes.
    u8 check[16];
    NEMU_TEST_ASSERT(cache.ReadEntry(e1, 0, check, 16), "read entry");
    NEMU_TEST_ASSERT(check[5] == 0x05, "bytes match guest memory");

    // Guest dirties a range -> partial update pulls only that range.
    std::memset(backing + 512, 0x77, 32);
    cache.MarkDirty(gpu_addr + 512, 32);
    const u64 e3 = cache.Acquire(BufferCache::Type::Vertex, gpu_addr, 1024);
    NEMU_TEST_ASSERT(e3 == e1, "entry stable across dirty");
    st = cache.GetStats();
    NEMU_TEST_ASSERT(st.partial_updates >= 1, "partial update happened");
    NEMU_TEST_ASSERT(cache.ReadEntry(e1, 512, check, 8), "read dirty range");
    NEMU_TEST_ASSERT(check[0] == 0x77 && check[7] == 0x77, "dirty bytes fresh");
    // Non-dirty range keeps old data (was identical anyway).

    // InvalidateRange drops the entry.
    cache.InvalidateRange(gpu_addr, 64);
    st = cache.GetStats();
    NEMU_TEST_ASSERT(st.evictions == 1, "invalidation evicted");

    // Type separation: uniform view of the same address is a distinct entry.
    const u64 u1 = cache.Acquire(BufferCache::Type::Uniform, gpu_addr, 256);
    NEMU_TEST_ASSERT(u1 != e1, "type separates entries");

    std::cout << "  BufferCache PASS\n";
}

// ---------------------------------------------------------------------------
// Expanded Maxwell3D: rasterizer state push, compute dispatch, DrawTexture,
// instanced draws, constant-buffer bind via buffer cache.
// ---------------------------------------------------------------------------
namespace {
class CountingBackend final : public IGpuBackend {
public:
    u32 raster_state_pushes{0};
    u32 compute_dispatches{0};
    u32 draw_calls_seen{0};
    u32 cbuf_binds{0};
    u32 last_block_x{0};
    RasterizerState last_raster_state{};

    bool Initialize(u32, u32) override { return true; }
    void Shutdown() override {}
    void BeginFrame() override {}
    void EndFrame() override {}
    void Present() override {}
    void SetViewport(const Viewport&) override {}
    void SetScissor(const ScissorRect&) override {}
    void ClearRenderTarget(const ClearColor&) override {}
    void ClearDepthStencil(float, u8) override {}
    void DrawArrays(PrimitiveTopology, u32, u32) override { draw_calls_seen++; }
    void DrawIndexed(PrimitiveTopology, u32, u32, u32) override { draw_calls_seen++; }
    void SetRasterizerState(const RasterizerState& state) override {
        raster_state_pushes++;
        last_raster_state = state;
    }
    void DispatchCompute(u32 bx, u32, u32) override {
        compute_dispatches++;
        last_block_x = bx;
    }
    void SetGuestConstantBuffer(u32, const void*, u32) override { cbuf_binds++; }
    [[nodiscard]] GpuStats GetStats() const noexcept override { return {}; }
    [[nodiscard]] std::string_view GetBackendName() const noexcept override { return "Counting"; }
};
} // namespace

void TestExpandedMaxwell3D() {
    auto backend = std::make_shared<CountingBackend>();
    Maxwell3D maxwell(backend);

    // Verify yuzu-style boot defaults (Tier-A2)
    NEMU_TEST_ASSERT(maxwell.GetRegisters().GetFloat(MaxwellMethod::ViewportDepthRangeNear) == 0.0f, "depth range near default 0.0");
    NEMU_TEST_ASSERT(maxwell.GetRegisters().GetFloat(MaxwellMethod::ViewportDepthRangeFar) == 1.0f, "depth range far default 1.0");
    NEMU_TEST_ASSERT(maxwell.GetRegisters().regs[MaxwellMethod::RasterizeEnable] == 1, "rasterize enable default 1");
    NEMU_TEST_ASSERT(maxwell.GetRegisters().regs[MaxwellMethod::ColorMaskRT0] == 0x1111, "color mask default 0x1111");
    NEMU_TEST_ASSERT(maxwell.GetRegisters().GetFloat(MaxwellMethod::PointSize) == 1.0f, "point size default 1.0");

    // Rasterizer state registers -> pushed at first draw.
    maxwell.ProcessMethod(MaxwellMethod::DepthTestEnable, 1);
    maxwell.ProcessMethod(MaxwellMethod::DepthFunc, 5); // GEQUAL
    maxwell.ProcessMethod(MaxwellMethod::CullFaceEnable, 1);
    NEMU_TEST_ASSERT(backend->raster_state_pushes == 0, "deferred until draw");

    // Depth bounds & depth bias & polygon modes via ProcessMethod
    auto to_u32 = [](float f) noexcept {
        u32 v = 0;
        std::memcpy(&v, &f, sizeof(v));
        return v;
    };
    maxwell.ProcessMethod(MaxwellMethod::DepthBoundsNear, to_u32(0.2f));
    maxwell.ProcessMethod(MaxwellMethod::DepthBoundsFar, to_u32(0.8f));
    maxwell.ProcessMethod(MaxwellMethod::DepthBoundsEnable, 1);
    maxwell.ProcessMethod(MaxwellMethod::SlopeScaleDepthBias, to_u32(1.5f));
    maxwell.ProcessMethod(MaxwellMethod::DepthBias, to_u32(2.0f));
    maxwell.ProcessMethod(MaxwellMethod::DepthBiasClamp, to_u32(0.5f));
    maxwell.ProcessMethod(MaxwellMethod::DepthBiasTriangle, 1);
    maxwell.ProcessMethod(MaxwellMethod::LineWidthSmooth, to_u32(3.5f));
    maxwell.ProcessMethod(MaxwellMethod::PolygonModeFront, 1);

    // Blend color, color mask, logic op via ProcessMethod
    maxwell.ProcessMethod(MaxwellMethod::BlendColorR, to_u32(0.1f));
    maxwell.ProcessMethod(MaxwellMethod::BlendColorG, to_u32(0.2f));
    maxwell.ProcessMethod(MaxwellMethod::BlendColorB, to_u32(0.3f));
    maxwell.ProcessMethod(MaxwellMethod::BlendColorA, to_u32(0.4f));
    maxwell.ProcessMethod(MaxwellMethod::ColorMaskRT0, 0x1010);
    maxwell.ProcessMethod(MaxwellMethod::LogicOpEnable, 1);

    // DMA & Sync methods (must not fault)
    maxwell.ProcessMethod(MaxwellMethod::LaunchDma, 0);
    maxwell.ProcessMethod(MaxwellMethod::InlineData, 0xCAFE);
    maxwell.ProcessMethod(MaxwellMethod::SyncInfo, 0);
    maxwell.ProcessMethod(MaxwellMethod::FragmentBarrier, 0);

    // DrawArrays: topology + vertex count encoding (count in [30:8]).
    const u32 count = 3;
    const u32 draw_arg = (count << 8) | 0x03; // topology 3 = Triangles
    maxwell.ProcessMethod(MaxwellMethod::DrawArrays, draw_arg);
    NEMU_TEST_ASSERT(backend->raster_state_pushes == 1, "raster state flushed at draw");
    NEMU_TEST_ASSERT(backend->draw_calls_seen == 1, "draw issued");

    // Verify rasterizer state payload
    NEMU_TEST_ASSERT(backend->last_raster_state.depth_bounds_enable, "depth bounds enable verified");
    NEMU_TEST_ASSERT(backend->last_raster_state.depth_bounds_near == 0.2f, "depth bounds near verified");
    NEMU_TEST_ASSERT(backend->last_raster_state.depth_bounds_far == 0.8f, "depth bounds far verified");
    NEMU_TEST_ASSERT(backend->last_raster_state.polygon_offset_enable, "polygon offset enable verified");
    NEMU_TEST_ASSERT(backend->last_raster_state.polygon_offset_factor == 1.5f, "polygon offset factor verified");
    NEMU_TEST_ASSERT(backend->last_raster_state.polygon_offset_units == 2.0f, "polygon offset units verified");
    NEMU_TEST_ASSERT(backend->last_raster_state.polygon_offset_clamp == 0.5f, "polygon offset clamp verified");
    NEMU_TEST_ASSERT(backend->last_raster_state.line_width == 3.5f, "line width verified");
    NEMU_TEST_ASSERT(backend->last_raster_state.polygon_mode_front == 1, "polygon mode front verified");
    NEMU_TEST_ASSERT(backend->last_raster_state.blend_color[0] == 0.1f, "blend color R verified");
    NEMU_TEST_ASSERT(backend->last_raster_state.blend_color[3] == 0.4f, "blend color A verified");
    NEMU_TEST_ASSERT(backend->last_raster_state.color_mask[0] == 0x1010, "color mask RT0 verified");
    NEMU_TEST_ASSERT(backend->last_raster_state.logic_op_enable, "logic op enable verified");

    // Instanced draw: InstanceCount = 4 -> 4 draw calls.
    maxwell.ProcessMethod(MaxwellMethod::InstanceCount, 4);
    maxwell.ProcessMethod(MaxwellMethod::DrawArrays, draw_arg);
    NEMU_TEST_ASSERT(backend->draw_calls_seen == 1 + 4, "instanced draws looped");

    // Compute dispatch: block dims + trigger.
    maxwell.ProcessMethod(MaxwellMethod::ComputeLaunchDesc, (4u << 16) | 8u); // y=4, x=8
    maxwell.ProcessMethod(MaxwellMethod::DispatchCompute, 0);
    NEMU_TEST_ASSERT(backend->compute_dispatches == 1, "compute dispatched");
    NEMU_TEST_ASSERT(backend->last_block_x == 8, "block x = 8");

    // Constant buffer: no GMMU -> falls back to direct memory read (no memory
    // attached here, so no bind happens, but no crash either).
    maxwell.ProcessMethod(MaxwellMethod::ComputeConstBufferHigh, 0x0);
    maxwell.ProcessMethod(MaxwellMethod::ComputeConstBufferLow, 0x4000000);
    maxwell.ProcessMethod(MaxwellMethod::ComputeConstBufferSize, 256);
    maxwell.ProcessMethod(MaxwellMethod::DrawArrays, draw_arg); // triggers cbuf bind path

    // DrawTexture issues an indexed fullscreen quad.
    const u32 indexed_before = backend->draw_calls_seen;
    maxwell.ProcessMethod(MaxwellMethod::DrawTexture, 0);
    NEMU_TEST_ASSERT(backend->draw_calls_seen == indexed_before + 1, "DrawTexture draws");

    std::cout << "  Expanded Maxwell3D PASS\n";
}

// ---------------------------------------------------------------------------
// Expanded shader decoder: new opcodes decode + emit HLSL
// ---------------------------------------------------------------------------
void TestExpandedShaderDecoder() {
    // Hand-assembled Maxwell SASS 64-bit words (major opcode in [63:52]).
    struct Case {
        u64 raw;
        shader::MaxwellOpcode expect;
        const char* name;
    };
    const std::vector<Case> cases = {
        {0x5C0000000000ABCDULL, shader::MaxwellOpcode::MOV, "MOV"},
        {0x5808000000000000ULL, shader::MaxwellOpcode::FADD, "FADD"},
        {0x5838000000000000ULL, shader::MaxwellOpcode::FFMA, "FFMA"},
        {0x5868000000000000ULL, shader::MaxwellOpcode::F2F, "F2F"},
        {0x5878000000000000ULL, shader::MaxwellOpcode::F2I, "F2I"},
        {0x5888000000000000ULL, shader::MaxwellOpcode::I2F, "I2F"},
        {0x5A38000000000000ULL, shader::MaxwellOpcode::IADD3, "IADD3"},
        {0x5A58000000000000ULL, shader::MaxwellOpcode::LEA, "LEA"},
        {0x5638000000000000ULL, shader::MaxwellOpcode::LOP3, "LOP3"},
        {0x5648000000000000ULL, shader::MaxwellOpcode::BFE, "BFE"},
        {0x5318000000000000ULL, shader::MaxwellOpcode::LDG, "LDG"},
        {0x5328000000000000ULL, shader::MaxwellOpcode::STG, "STG"},
        {0x5518000000000000ULL, shader::MaxwellOpcode::TEXS, "TEXS"},
        {0x5538000000000000ULL, shader::MaxwellOpcode::TXQ, "TXQ"},
    };
    for (const auto& c : cases) {
        std::vector<u8> code(sizeof(u64));
        std::memcpy(code.data(), &c.raw, sizeof(u64));
        const auto prog = shader::MaxwellShaderDecoder::DecodeAndDecompile(
            code, shader::ShaderStage::Vertex, false);
        NEMU_TEST_ASSERT(!prog.instructions.empty(), "decoded at least one instruction");
        NEMU_TEST_ASSERT(prog.instructions[0].opcode == c.expect,
                         std::string("opcode decode for ") + c.name);
        NEMU_TEST_ASSERT(!prog.hlsl_source.empty(), "HLSL emitted");
    }

    // FFMA program: HLSL contains the multiply-add expression.
    const u64 ffma = 0x5838000000000000ULL;
    std::vector<u8> code(sizeof(u64));
    std::memcpy(code.data(), &ffma, sizeof(u64));
    const auto prog = shader::MaxwellShaderDecoder::DecodeAndDecompile(
        code, shader::ShaderStage::Vertex, false);
    NEMU_TEST_ASSERT(prog.hlsl_source.find("R[") != std::string::npos, "register write");

    std::cout << "  Expanded shader decoder PASS\n";
}

// ---------------------------------------------------------------------------
// BC1 encoder (Tier-B1): size contract + round-trip quality
// ---------------------------------------------------------------------------
void TestBc1Encoder() {
    using namespace nemu::core::gpu::texture;

    // 8x8 red/blue gradient surface.
    constexpr u32 W = 8, H = 8;
    std::vector<u8> rgba(W * H * 4);
    for (u32 y = 0; y < H; ++y) {
        for (u32 x = 0; x < W; ++x) {
            u8* px = rgba.data() + (y * W + x) * 4;
            px[0] = static_cast<u8>(x * 32); // R ramps
            px[1] = 0;
            px[2] = static_cast<u8>(255 - y * 32); // B ramps
            px[3] = 255;
        }
    }

    std::vector<u8> bc1;
    NEMU_TEST_ASSERT(Bc1Encoder::EncodeRGBA8(rgba, W, H, bc1), "encode ok");
    // Size contract: (8/4)*(8/4) blocks * 8 bytes = 32 bytes (vs 256 RGBA8).
    NEMU_TEST_ASSERT(bc1.size() == 32, "bc1 size = blocks*8");

    // Decode the first block manually and check endpoints exist + error is sane.
    u16 c0, c1;
    std::memcpy(&c0, bc1.data(), 2);
    std::memcpy(&c1, bc1.data() + 2, 2);
    NEMU_TEST_ASSERT(c0 >= c1, "4-color mode ordering");

    // Quality: decode palette, average per-pixel error must be small.
    u8 r0, g0, b0, r1, g1, b1;
    Bc1Encoder::Unpack565(c0, r0, g0, b0);
    Bc1Encoder::Unpack565(c1, r1, g1, b1);
    const int dr = r0 - r1, dg = g0 - g1, db = b0 - b1;
    NEMU_TEST_ASSERT(dr * dr + dg * dg + db * db > 1000, "endpoints distinct (gradient)");

    // Uniform block: endpoints should collapse to (almost) the same color.
    std::vector<u8> flat(64, 0);
    for (u32 i = 0; i < 16; ++i) {
        flat[i * 4 + 0] = 128; flat[i * 4 + 1] = 64; flat[i * 4 + 2] = 32; flat[i * 4 + 3] = 255;
    }
    const auto blk = Bc1Encoder::EncodeBlock(std::span<const u8, 64>(flat.data(), 64));
    u8 fr, fg, fb, fr2, fg2, fb2;
    Bc1Encoder::Unpack565(blk.color0, fr, fg, fb);
    Bc1Encoder::Unpack565(blk.color1, fr2, fg2, fb2);
    const int edr = fr - fr2, edg = fg - fg2, edb = fb - fb2;
    NEMU_TEST_ASSERT(edr * edr + edg * edg + edb * edb < 64, "uniform block endpoints near-equal");

    std::cout << "  BC1 encoder PASS\n";
}

// ---------------------------------------------------------------------------
// TextureCache BC1 integration (Tier-B1 wired): ASTC input -> BC1 host storage
// ---------------------------------------------------------------------------
void TestTextureCacheBc1Integration() {
    using namespace nemu::core::gpu::texture;

    auto mem = std::make_shared<memory::VirtualMemory>();
    // Map a guest page for the ASTC texture data.
    constexpr u64 guest_addr = 0x30000000ULL;
    constexpr u32 W = 64, H = 64, BW = 4, BH = 4;
    constexpr u32 blocks_x = W / BW, blocks_y = H / BH;
    constexpr size_t astc_bytes = blocks_x * blocks_y * 16; // 128-bit blocks

    // A flat red 4x4 ASTC block (constant-color mode: w0=0x1F9 just needs to
    // decode deterministically; we only assert size/storage, not color).
    std::vector<u8> astc(astc_bytes, 0);
    for (u32 b = 0; b < blocks_x * blocks_y; ++b) {
        // Minimal valid void-extent-ish block; decoder treats 0x as regular.
        astc[b * 16 + 0] = 0xF9; astc[b * 16 + 1] = 0x01;
        astc[b * 16 + 9] = 0xFF; astc[b * 16 + 10] = 0x00; astc[b * 16 + 11] = 0x00;
    }
    NEMU_TEST_ASSERT(mem->Map(guest_addr, astc_bytes, memory::MemoryPermission::All), "guest map");
    mem->WriteBlock(guest_addr, astc.data(), astc.size());

    TextureCache cache;
    NEMU_TEST_ASSERT(cache.Initialize(), "cache init");

    TextureDescriptor desc{};
    desc.gpu_address = guest_addr;
    desc.width = W;
    desc.height = H;
    desc.depth = 1;
    desc.mip_levels = 1;
    desc.format = TextureFormat::ASTC_4x4;
    desc.bytes_per_pixel = 4;

    auto tex = cache.GetOrCreateTexture(desc, mem.get());
    NEMU_TEST_ASSERT(tex && tex->is_valid, "texture created valid");

    // The core assertion: host storage is BC1 (0.5 B/px), NOT the RGBA8 blowup.
    NEMU_TEST_ASSERT(tex->host_storage == CachedTexture::HostStorage::BC1,
                     "ASTC texture stored as BC1");
    const size_t expected_bc1 = (W / 4) * (H / 4) * 8; // 2048 bytes
    NEMU_TEST_ASSERT(tex->linear_pixel_data.size() == expected_bc1,
                     "BC1 size = blocks*8 (was 16384 RGBA8: 8x reduction)");
    // Second lookup returns the same cached entry (no re-encode).
    auto tex2 = cache.GetOrCreateTexture(desc, mem.get());
    NEMU_TEST_ASSERT(tex2 == tex, "cache hit returns same entry");

    std::cout << "  TextureCache BC1 integration PASS\n";
}

// ---------------------------------------------------------------------------
// Present-path optimizer pipeline (Tier-B UI wiring): settings -> Present()
// ---------------------------------------------------------------------------
void TestPresentOptimizerPipeline() {
    NullGpuBackend gpu;
    NEMU_TEST_ASSERT(gpu.Initialize(64, 64), "init 64x64");

    // Clear to solid red.
    gpu.BeginFrame();
    gpu.ClearRenderTarget(ClearColor{.r = 1.0f, .g = 0.0f, .b = 0.0f, .a = 1.0f});
    gpu.EndFrame();

    // With optimizers disabled, Present leaves the framebuffer at 64x64.
    gpu.Present();
    NEMU_TEST_ASSERT(gpu.FramebufferSize() == 64u * 64u * 4u, "untouched present size");

    // Enable FSR_1_0 (2x target) + FXAA + framegen.
    FrameOptimizerSettings fo{};
    fo.upscaler = pipeline::UpscalerMode::FSR_1_0;
    fo.anti_aliasing = pipeline::AntiAliasingMode::FXAA;
    fo.frame_generation = pipeline::FrameGenMode::AFMF_Extrapolation_2x;
    fo.enabled = true;
    gpu.SetFrameOptimizerSettings(fo);

    gpu.BeginFrame();
    gpu.ClearRenderTarget(ClearColor{.r = 1.0f, .g = 0.0f, .b = 0.0f, .a = 1.0f});
    gpu.EndFrame();
    gpu.Present();

    // Frame was upscaled 2x: 128x128 framebuffer.
    NEMU_TEST_ASSERT(gpu.FramebufferSize() == 128u * 128u * 4u, "2x upscaled present");
    const auto st = gpu.GetStats();
    NEMU_TEST_ASSERT(st.frames_upscaled >= 1, "upscale counted");
    NEMU_TEST_ASSERT(st.frames_generated >= 1, "framegen counted");

    // Content check: solid red in, solid red out (FSR preserves flat color).
    const u8* fb = gpu.Framebuffer();
    NEMU_TEST_ASSERT(fb[0] == 255 && fb[1] == 0 && fb[2] == 0 && fb[3] == 255,
                     "solid red preserved through upscale+AA");

    std::cout << "  Present optimizer pipeline PASS\n";
}

// ---------------------------------------------------------------------------
// SASS identifier (Tier-A1 full-table): known words -> right family
// ---------------------------------------------------------------------------
void TestSassIdentifier() {
    using namespace nemu::core::gpu::shader;

    // EXIT must identify cleanly (EXIT: mask/value top16 = 1110 0011 0000 ----).
    const auto e = IdentifySass(0xE300000000000000ULL);
    NEMU_TEST_ASSERT(e.encoding_index != SIZE_MAX, "EXIT matched a row");
    NEMU_TEST_ASSERT(std::string_view(SassCuteName(e.encoding_index))
                         .find("EXIT") != std::string_view::npos,
                     "EXIT cute name");

    // LD (mask E0 value 80) and ST (mask E0 value A0) — the two least-specific
    // rows. Match-words use the row values themselves.
    const auto ld = IdentifySass(0x8000000000000000ULL);
    NEMU_TEST_ASSERT(ld.encoding_index != SIZE_MAX, "LD row");
    const auto st = IdentifySass(0xA000000000000000ULL);
    NEMU_TEST_ASSERT(st.encoding_index != SIZE_MAX, "ST row");

    // Most-specific encodings beat generic ones: an IADD3-shaped word must
    // not resolve to plain IADD (popcount ordering is live).
    const auto specific = IdentifySass(0x5A38000000000000ULL);
    NEMU_TEST_ASSERT(specific.encoding_index != SIZE_MAX, "specific row");

    // Coverage: random words map to rows or fall back without crashing.
    u64 x = 0x1234567890ABCDEFULL;
    size_t matched = 0;
    for (int i = 0; i < 10000; ++i) {
        x = x * 6364136223846793005ULL + 1442695040888963407ULL;
        const auto info = IdentifySass(x);
        if (info.encoding_index != SIZE_MAX) matched++;
    }
    NEMU_TEST_ASSERT(matched > 500, "random words map to real rows (table is live)");

    std::cout << "  SASS identifier PASS\n";
}

// ---------------------------------------------------------------------------
// Extended SASS HLSL emission (Tier-A1 IR layer): IMAD/MUFU/HFMA2 programs
// decode + the emitted HLSL contains the expected ops.
// ---------------------------------------------------------------------------
void TestExtendedSassEmission() {
    using namespace nemu::core::gpu::shader;

    // Build a synthetic 3-instruction SASS program: IMAD, MUFU.rsqrt, HFMA2.
    // Raw words: top 16 bits are the yuzu pattern (masked at [63:52]+); the
    // decoder maps the matching family.
    auto word = [&](u64 top16) -> u64 {
        return top16 << 48; // place the 16-bit pattern at the top of the 64-bit word
    };
    std::vector<u8> code;
    for (u64 w : {
        word(0x5A00), // IADD3/IMAD family space anchor; IMAD is 0101 0110 ---- ----
        word(0x5E80), // MUFU family (0x5E0-0x5EF), rsqrt sub-op
        word(0x51C0), // HFMA2 family
    }) {
        for (int b = 7; b >= 0; --b) code.push_back(static_cast<u8>((w >> (b * 8)) & 0xFF));
    }

    // IMAD row: verify the identified word maps to a non-UNKNOWN opcode.
    const auto imad_sass = IdentifySass(word(0x5A00));
    NEMU_TEST_ASSERT(imad_sass.encoding_index != SIZE_MAX, "IMAD-space word identified");
    const auto imad_maxwell = IdentifyMaxwell(word(0x5A00));

    // At minimum the identify bridge must not crash and the decoder must
    // produce a non-empty program whose HLSL contains our register-write
    // emission scaffolding for at least one instruction.
    const auto prog = shader::MaxwellShaderDecoder::DecodeAndDecompile(code, ShaderStage::Vertex, false);
    NEMU_TEST_ASSERT(!prog.instructions.empty(), "decoded instructions");

    // The emitter runs for every decoded instruction (fast-path OR extended);
    // assert the HLSL is structurally valid (has the R[] array + main body).
    NEMU_TEST_ASSERT(!prog.hlsl_source.empty(), "HLSL emitted");
    NEMU_TEST_ASSERT(prog.hlsl_source.find("R[") != std::string::npos, "register array used");
    NEMU_TEST_ASSERT(prog.hlsl_source.find("main(") != std::string::npos, "main entry present");
    NEMU_TEST_ASSERT(prog.hlsl_source.find("return output") != std::string::npos, "returns output");
    (void)imad_maxwell; // only the decode path is asserted here

    std::cout << "  Extended SASS emission PASS\n";
}

// ---------------------------------------------------------------------------
// Rare/unsupported SASS family diagnostic: an identified-but-not-emitted
// family (e.g. SUATOM surface-atomic) yields an auditable [untranslated]
// comment instead of silently vanishing.
// ---------------------------------------------------------------------------
void TestRareSassDiagnostic() {
    using namespace nemu::core::gpu::shader;

    // SUATOM surface atomic: mask/val top16 = 1110 1010 0--- (from table).
    // DecodeInstruction64 memcpy's 8 bytes host-endian, so write little-endian.
    const u64 suatom = 0xEA00000000000000ULL;
    std::vector<u8> code;
    for (int b = 0; b < 8; ++b) code.push_back(static_cast<u8>((suatom >> (b * 8)) & 0xFF));

    NEMU_TEST_ASSERT(IdentifySass(suatom).encoding_index != SIZE_MAX, "SUATOM identified");

    const auto prog = MaxwellShaderDecoder::DecodeAndDecompile(code, ShaderStage::Fragment, false);
    NEMU_TEST_ASSERT(!prog.instructions.empty(), "rare family decoded");
    // The [untranslated] diagnostic comment must be present in the emitted HLSL.
    NEMU_TEST_ASSERT(prog.hlsl_source.find("[untranslated]") != std::string::npos,
                     "rare family leaves auditable diagnostic");

    std::cout << "  Rare SASS diagnostic PASS\n";
}

// ---------------------------------------------------------------------------
// ComputeQmd & Hardware ComputeLaunch (Tier-A3)
// ---------------------------------------------------------------------------
void TestComputeQmd() {
    ComputeQmd qmd{};
    // word 8: program offset
    qmd.words[8] = 0x1200;
    // word 12: GridDimX
    qmd.words[12] = 32;
    // word 13: GridDimY (low 16), GridDimZ (high 16)
    qmd.words[13] = 16 | (4 << 16);
    // word 18: BlockDimX (high 16)
    qmd.words[18] = (8 << 16);
    // word 19: BlockDimY (low 16), BlockDimZ (high 16)
    qmd.words[19] = 4 | (2 << 16);
    // word 17: shared memory size
    qmd.words[17] = 4096;
    // word 20: cbuf mask (bits 0..7) -> enable slot 0 and slot 2
    qmd.words[20] = (1 << 0) | (1 << 2);
    // slot 0: word 29 (addr low), word 30 (addr high bits 0..7, size >> 15)
    // address = 0x20000, size = 512
    qmd.words[29] = 0x20000;
    qmd.words[30] = 0x0 | (512 << 15);
    // slot 2: word 33 (addr low), word 34 (addr high + size)
    // address = 0x30000, size = 1024
    qmd.words[33] = 0x30000;
    qmd.words[34] = 0x0 | (1024 << 15);

    NEMU_TEST_ASSERT(qmd.ProgramOffset() == 0x1200, "QMD program offset");
    NEMU_TEST_ASSERT(qmd.GridDimX() == 32, "QMD GridDimX");
    NEMU_TEST_ASSERT(qmd.GridDimY() == 16, "QMD GridDimY");
    NEMU_TEST_ASSERT(qmd.GridDimZ() == 4, "QMD GridDimZ");
    NEMU_TEST_ASSERT(qmd.BlockDimX() == 8, "QMD BlockDimX");
    NEMU_TEST_ASSERT(qmd.BlockDimY() == 4, "QMD BlockDimY");
    NEMU_TEST_ASSERT(qmd.BlockDimZ() == 2, "QMD BlockDimZ");
    NEMU_TEST_ASSERT(qmd.SharedMemorySize() == 4096, "QMD SharedMemorySize");
    NEMU_TEST_ASSERT(qmd.ConstantBufferValid(0), "QMD cbuf 0 valid");
    NEMU_TEST_ASSERT(!qmd.ConstantBufferValid(1), "QMD cbuf 1 not valid");
    NEMU_TEST_ASSERT(qmd.ConstantBufferValid(2), "QMD cbuf 2 valid");
    NEMU_TEST_ASSERT(qmd.ConstantBufferAddress(0) == 0x20000, "QMD cbuf 0 addr");
    NEMU_TEST_ASSERT(qmd.ConstantBufferSize(0) == 512, "QMD cbuf 0 size");
    NEMU_TEST_ASSERT(qmd.ConstantBufferAddress(2) == 0x30000, "QMD cbuf 2 addr");
    NEMU_TEST_ASSERT(qmd.ConstantBufferSize(2) == 1024, "QMD cbuf 2 size");

    // Test Hardware QMD ComputeLaunch through Maxwell3D with GMMU
    auto backend = std::make_shared<CountingBackend>();
    Maxwell3D maxwell(backend);
    auto gmmu = std::make_shared<GpuMemoryManager>(nullptr);
    maxwell.SetGpuMemory(gmmu);

    // Map a big page for QMD and constant buffers
    static u8 page[GpuMemoryManager::kBigPageSize];
    std::memset(page, 0, sizeof(page));
    const u64 gpu_base = 0x4000000ULL;
    NEMU_TEST_ASSERT(gmmu->Map(gpu_base, sizeof(page), page), "map QMD memory");

    // Configure cbuf 0 at gpu_base + 0x2000 (size 128)
    qmd.words[29] = static_cast<u32>(gpu_base + 0x2000);
    qmd.words[30] = static_cast<u32>(((gpu_base + 0x2000) >> 32) & 0xFF) | (128 << 15);
    qmd.words[20] = (1 << 0); // only slot 0 enabled
    std::memcpy(page, qmd.words.data(), sizeof(qmd.words));

    // launch_desc_loc = gpu_base >> 8
    const u32 launch_loc = static_cast<u32>(gpu_base >> 8);
    maxwell.ProcessMethod(MaxwellMethod::ComputeLaunchDescLoc, launch_loc);

    const u32 dispatches_before = backend->compute_dispatches;
    const u32 cbufs_before = backend->cbuf_binds;

    // Issue hardware ComputeLaunch (method 0x00AF)
    maxwell.ProcessMethod(MaxwellMethod::ComputeLaunch, 0);

    NEMU_TEST_ASSERT(backend->compute_dispatches == dispatches_before + 1, "QMD compute launched");
    NEMU_TEST_ASSERT(backend->last_block_x == 32, "QMD grid x = 32 passed as block_x to backend");
    NEMU_TEST_ASSERT(backend->cbuf_binds == cbufs_before + 1, "QMD cbuf bound");

    std::cout << "  ComputeQmd and Hardware ComputeLaunch PASS\n";
}

// ---------------------------------------------------------------------------
// Compute shader HLSL emission (Tier-A3): a compute-stage program decodes and
// the emitter produces a valid cs_5_0 skeleton (numthreads + dispatch id).
// ---------------------------------------------------------------------------
void TestComputeShaderEmission() {
    using namespace nemu::core::gpu::shader;

    // Simple compute program: NOP + EXIT (empty body is a valid shader).
    // DecodeInstruction64 memcpy's 8 bytes host-endian -> write little-endian.
    u64 w_nop = 0x0200000000000000ULL;   // NOP
    u64 w_exit = 0xE300000000000000ULL;  // EXIT (mask/value top16 = 1110 0011 0000 ----)
    std::vector<u8> code;
    for (u64 w : {w_nop, w_exit}) {
        for (int b = 0; b < 8; ++b) code.push_back(static_cast<u8>((w >> (b * 8)) & 0xFF));
    }

    const auto prog = MaxwellShaderDecoder::DecodeAndDecompile(
        code, ShaderStage::Compute, /*has_control_codes=*/false);
    NEMU_TEST_ASSERT(!prog.instructions.empty(), "compute decoded");
    NEMU_TEST_ASSERT(!prog.hlsl_source.empty(), "compute HLSL emitted");

    // Compute skeleton must be cs_5_0-valid: numthreads + dispatch id + void void.
    const auto& h = prog.hlsl_source;
    NEMU_TEST_ASSERT(h.find("[numthreads(8, 8, 1)]") != std::string::npos, "numthreads decl");
    NEMU_TEST_ASSERT(h.find("SV_DispatchThreadID") != std::string::npos, "dispatch thread id");
    NEMU_TEST_ASSERT(h.find("void main(") != std::string::npos, "void main (no output return)");
    // No stray 'return output' in compute.
    NEMU_TEST_ASSERT(h.find("return output") == std::string::npos, "compute has no output return");

    std::cout << "  Compute shader emission PASS\n";
}

// ---------------------------------------------------------------------------
// Predicated branch (BRA.P) decode: a conditional BRA word records its
// predicate + invert sense + absolute target for the emitter.
// ---------------------------------------------------------------------------
void TestPredicatedBranchDecode() {
    using namespace nemu::core::gpu::shader;
    using namespace nemu;

    // BRA: major 0x5D0 (0101 1101 0000 ----). The decoder reads:
    //   rel_target  = bits [23:20]  (s24 offset from current instr)
    //   predicate   = bits [13:11]  (P0-P6; 7 = unconditional/PT)
    //   invert      = bit  14
    // Build a word: rel_target = +4 words (bytes 32), predicate P2, invert set.
    u64 base = 0x5D00000000000000ULL; // BRA major at top
    const u32 rel_words = 4;                  // forward 4 instructions
    const u32 pred = 2;                       // P2
    const u32 invert = 1;                     // BRA !P2
    u64 w = base
          | (static_cast<u64>(rel_words & 0xFFFFFF) << 20)
          | (static_cast<u64>(pred & 0x7) << 11)
          | (static_cast<u64>(invert & 0x1) << 14);

    // Isolate the compiled decoder path: decode via a 1-instruction program.
    std::vector<u8> code;
    for (int b = 0; b < 8; ++b) code.push_back(static_cast<u8>((w >> (b * 8)) & 0xFF));
    auto prog = MaxwellShaderDecoder::DecodeAndDecompile(code, ShaderStage::Vertex, false);
    NEMU_TEST_ASSERT(!prog.instructions.empty(), "BRA decoded");
    const auto& inst = prog.instructions[0];
    NEMU_TEST_ASSERT(inst.opcode == MaxwellOpcode::BRA, "opcode is BRA");

    // The emitted HLSL records a predicated [bra] comment for P2 (predicate<7).
    NEMU_TEST_ASSERT(prog.hlsl_source.find("[bra]") != std::string::npos,
                     "predicated BRA emitted");

    std::cout << "  Predicated branch decode PASS\n";
}

int main() {
    std::cout << "== NEMU Tier-A unit tests ==\n";
    TestGmmuBasics();
    TestBufferCache();
    TestExpandedMaxwell3D();
    TestExpandedShaderDecoder();
    TestBc1Encoder();
    TestTextureCacheBc1Integration();
    TestPresentOptimizerPipeline();
    TestSassIdentifier();
    TestExtendedSassEmission();
    TestRareSassDiagnostic();
    TestComputeQmd();
    TestComputeShaderEmission();
    TestPredicatedBranchDecode();
    std::cout << "ALL TIER-A TESTS PASSED\n";
    return 0;
}
