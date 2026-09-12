#include "core/types.hpp"
#include "core/gpu/shader/shader_translator.hpp"
#include "core/gpu/shader/hlsl_validator.hpp"
#include "core/gpu/pipeline/pipeline_cache.hpp"
#include "core/gpu/pipeline/pipeline_bridge.hpp"
#include <iostream>
#include <vector>
#include <cstring>
#include <cstdlib>
#include <span>

using namespace nemu;
using namespace nemu::core::gpu;

#define PB_ASSERT(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "Assertion failed: " msg " at " << __FILE__ << ":" << __LINE__ << std::endl; \
            std::exit(1); \
        } \
    } while (0)

namespace {

void EncodeInst(std::span<u8> dst, u32 opcode, u32 rd, u32 ra, u32 rb, u32 rc) {
    const u64 val = (static_cast<u64>(opcode) << 52) |
                    (static_cast<u64>(rd) & 0xFF) |
                    ((static_cast<u64>(ra) & 0xFF) << 8) |
                    (static_cast<u64>(7) << 16) |     // Predicate 7 (Always)
                    ((static_cast<u64>(rb) & 0xFF) << 20) |
                    ((static_cast<u64>(rc) & 0xFF) << 32);
    std::memcpy(dst.data(), &val, sizeof(u64));
}

std::vector<u8> MakeVertexProgram() {
    // MOV R0,R1; ST.ATTR a[0].x,R0; EXIT
    std::vector<u8> code(24, 0);
    EncodeInst(std::span<u8>(code.data() + 0, 8), 0x5C0, 0, 1, 0, 0);
    EncodeInst(std::span<u8>(code.data() + 8, 8), 0x5B1, 0, 0, 0, 0);
    EncodeInst(std::span<u8>(code.data() + 16, 8), 0x5D1, 0, 0, 0, 0);
    return code;
}

std::vector<u8> MakeFragmentProgram() {
    // MOV R0,R1; EXIT
    std::vector<u8> code(16, 0);
    EncodeInst(std::span<u8>(code.data() + 0, 8), 0x5C0, 0, 1, 0, 0);
    EncodeInst(std::span<u8>(code.data() + 8, 8), 0x5D1, 0, 0, 0, 0);
    return code;
}

} // namespace

int main() {
    std::cout << "[Test: Shader -> HLSL -> PipelineCache Bridge]" << std::endl;

    // -- HlslValidator standalone --
    {
        using namespace nemu::core::gpu::shader;
        auto ok = HlslValidator::Validate(R"(float4 main(float2 p : POSITION) : SV_Position { return float4(p,0,1); })");
        PB_ASSERT(ok.Passed(), "valid HLSL passes");
        PB_ASSERT(ok.has_main, "has main");
        PB_ASSERT(ok.balanced_braces, "braces balanced");

        auto bad = HlslValidator::Validate("float4 main() { return float4(1,0,0,1);"); // missing closing brace
        PB_ASSERT(!bad.Passed(), "truncated HLSL rejected");
        PB_ASSERT(!bad.balanced_braces, "unbalanced braces detected");
    }
    std::cout << "  - HlslValidator: PASSED" << std::endl;

    // -- ShaderTranslator -> HLSL -- 
    auto vs_prog = MakeVertexProgram();
    auto ps_prog = MakeFragmentProgram();
    auto tv = shader::ShaderTranslator::Translate(vs_prog, shader::ShaderStage::Vertex);
    PB_ASSERT(tv.ok, "vs translates");
    auto tp = shader::ShaderTranslator::Translate(ps_prog, shader::ShaderStage::Fragment);
    PB_ASSERT(tp.ok, "ps translates");
    std::cout << "  - ShaderTranslator emit: PASSED" << std::endl;

    // -- PipelineCache headless --
    {
        pipeline::PipelineCache cache;
        pipeline::PipelineStateKey key{};
        key.num_cbufs = 1;
        key.num_textures = 0;
        bool created = cache.GetOrCreatePipeline(key, tv.hlsl_source, tp.hlsl_source);
        PB_ASSERT(created, "cache creates pipeline");
        PB_ASSERT(cache.GetCachedPipelineCount() == 1, "one pipeline cached");
        // Same key -> hit.
        bool hit = cache.GetOrCreatePipeline(key, tv.hlsl_source, tp.hlsl_source);
        PB_ASSERT(hit, "cache hit returns valid");
        PB_ASSERT(cache.GetCachedPipelineCount() == 1, "no duplicate entry");
        PB_ASSERT(cache.GetCacheHits() >= 1, "cache hits counted");
    }
    std::cout << "  - PipelineCache (headless): PASSED" << std::endl;

    // -- PipelineBridge full chain --
    {
        pipeline::PipelineCache cache;
        pipeline::PipelineStateKey key{};
        key.topology = PrimitiveTopology::Triangles;
        key.num_cbufs = 0;

        // Valid VS + PS.
        auto r = pipeline::PipelineBridge::Build(vs_prog, ps_prog, key, &cache);
        PB_ASSERT(r.decoder_ok, "decoder ok");
        PB_ASSERT(r.hlsl_valid, "hlsl valid");
        PB_ASSERT(r.budget_ok, "budget ok");
        PB_ASSERT(r.cached, "cached");
        PB_ASSERT(r.Passed(), "build passed");
        PB_ASSERT(cache.GetCachedPipelineCount() == 1, "pipeline cached via bridge");

        // Garbage bytecode (< one 8-byte instruction) decodes to zero
        // instructions => clean rejection, no cache pollution.
        std::vector<u8> garbage(6, 0xFF);
        auto bad = pipeline::PipelineBridge::Build(garbage, garbage, key, &cache);
        PB_ASSERT(!bad.Passed(), "garbage bytecode rejected");
        PB_ASSERT(cache.GetCachedPipelineCount() == 1, "no cache entry for garbage");
    }
    std::cout << "  - PipelineBridge (decode->HLSL->validate->cache): PASSED" << std::endl;

    std::cout << "[Test: Shader -> HLSL -> PipelineCache Bridge PASSED]" << std::endl;
    return 0;
}