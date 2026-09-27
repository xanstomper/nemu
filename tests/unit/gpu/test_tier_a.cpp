// Tier-A unit tests: GMMU (global GPU memory), BufferCache (dirty-range
// streaming), expanded Maxwell3D method surface, compute dispatch, expanded
// shader decoder opcodes. Run: ctest -R test_tier_a
#include "core/gpu/gmmu.hpp"
#include "core/gpu/buffer_cache.hpp"
#include "core/gpu/maxwell_3d.hpp"
#include "core/gpu/null_backend.hpp"
#include "core/gpu/shader/maxwell_shader_decoder.hpp"
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
    void SetRasterizerState(const RasterizerState&) override { raster_state_pushes++; }
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

    // Rasterizer state registers -> pushed at first draw.
    maxwell.ProcessMethod(MaxwellMethod::DepthTestEnable, 1);
    maxwell.ProcessMethod(MaxwellMethod::DepthFunc, 5); // GEQUAL
    maxwell.ProcessMethod(MaxwellMethod::CullFaceEnable, 1);
    NEMU_TEST_ASSERT(backend->raster_state_pushes == 0, "deferred until draw");

    // DrawArrays: topology + vertex count encoding (count in [30:8]).
    const u32 count = 3;
    const u32 draw_arg = (count << 8) | 0x03; // topology 3 = Triangles
    maxwell.ProcessMethod(MaxwellMethod::DrawArrays, draw_arg);
    NEMU_TEST_ASSERT(backend->raster_state_pushes == 1, "raster state flushed at draw");
    NEMU_TEST_ASSERT(backend->draw_calls_seen == 1, "draw issued");

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

int main() {
    std::cout << "== NEMU Tier-A unit tests ==\n";
    TestGmmuBasics();
    TestBufferCache();
    TestExpandedMaxwell3D();
    TestExpandedShaderDecoder();
    std::cout << "ALL TIER-A TESTS PASSED\n";
    return 0;
}
