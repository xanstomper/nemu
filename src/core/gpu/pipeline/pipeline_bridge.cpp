#include "pipeline_bridge.hpp"

namespace nemu::core::gpu::pipeline {

using namespace nemu::core::gpu::shader;

PipelineBuildResult PipelineBridge::Build(
    std::span<const u8> vs_bytecode,
    std::span<const u8> ps_bytecode,
    const PipelineStateKey& key,
    PipelineCache* cache
) {
    PipelineBuildResult res{};

    // 1. Translate both stages (decode Maxwell -> HLSL).
    ShaderProgram program;
    bool need_vs = !vs_bytecode.empty();
    bool need_ps = !ps_bytecode.empty();
    if (need_vs) {
        program.vertex = ShaderTranslator::Translate(vs_bytecode, ShaderStage::Vertex);
        res.vs_hlsl = program.vertex.hlsl_source;
    } else {
        // Default color-passthrough vertex stage is implied; still mark decode ok.
        res.decoder_ok = true;
    }
    if (need_ps) {
        program.fragment = ShaderTranslator::Translate(ps_bytecode, ShaderStage::Fragment);
        res.ps_hlsl = program.fragment.hlsl_source;
    }

    if (need_vs && !program.vertex.ok) {
        res.error = "vertex stage failed to decode";
        return res;
    }
    if (need_ps && !program.fragment.ok) {
        res.error = "fragment stage failed to decode";
        return res;
    }
    res.decoder_ok = true;

    // 2. Cross-stage budget validation.
    auto report = ShaderTranslator::Validate(program);
    res.budget_ok = report.register_budget_ok && report.texture_slots_ok && report.errors.empty();
    if (!res.budget_ok) {
        res.error = report.errors.empty() ? "shader budget exceeded" : report.errors[0];
        return res;
    }

    // 3. HLSL source validation (compile-readiness, headless).
    if (need_vs) {
        auto v = HlslValidator::Validate(program.vertex.hlsl_source);
        res.hlsl_valid = v.Passed();
        if (!v.Passed()) {
            res.error = "VS HLSL invalid: " + v.error;
            return res;
        }
    }
    if (need_ps) {
        auto p = HlslValidator::Validate(program.fragment.hlsl_source);
        res.hlsl_valid = p.Passed();
        if (!p.Passed()) {
            res.error = "PS HLSL invalid: " + p.error;
            return res;
        }
    }
    res.hlsl_valid = true;

    // 4. Push into the pipeline cache (real D3DCompile on Windows/Xbox).
    if (cache) {
        res.cached = cache->GetOrCreatePipeline(key, program.vertex.hlsl_source,
                                                program.fragment.hlsl_source);
    } else {
        res.cached = true;
    }

    return res;
}

} // namespace nemu::core::gpu::pipeline