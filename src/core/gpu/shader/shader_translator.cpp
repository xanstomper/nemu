#include "shader_translator.hpp"

namespace nemu::core::gpu::shader {

TranslatedShader ShaderTranslator::Translate(std::span<const u8> bytecode, ShaderStage stage,
                                             bool has_control_codes) {
    TranslatedShader out{};
    out.stage = stage;

    if (bytecode.empty()) {
        return out;
    }

    DecompiledProgram decomp = MaxwellShaderDecoder::DecodeAndDecompile(bytecode, stage, has_control_codes);
    if (decomp.instructions.empty()) {
        return out;
    }

    bool has_valid_inst = false;
    for (const auto& inst : decomp.instructions) {
        if (inst.opcode != MaxwellOpcode::UNKNOWN) {
            has_valid_inst = true;
            break;
        }
    }
    if (!has_valid_inst) {
        return out;
    }

    out.hlsl_source = std::move(decomp.hlsl_source);
    out.used_cbuf_banks = std::move(decomp.used_cbuf_banks);
    out.used_textures = std::move(decomp.used_textures);
    out.max_register_used = decomp.max_register_used;
    out.has_discard = decomp.has_discard;
    out.ok = !out.hlsl_source.empty();
    return out;
}

ShaderValidationReport ShaderTranslator::Validate(const ShaderProgram& program, u32 max_registers,
                                                  u32 max_textures) {
    ShaderValidationReport report{};
    report.vertex_valid = program.vertex.ok && program.vertex.stage == ShaderStage::Vertex;
    report.fragment_valid = program.fragment.ok && program.fragment.stage == ShaderStage::Fragment;

    if (!report.vertex_valid) {
        report.errors.emplace_back("vertex stage failed to decode");
        return report;
    }

    if (program.vertex.max_register_used > max_registers) {
        report.register_budget_ok = false;
        report.errors.push_back("vertex shader register budget exceeded");
    }
    if (program.vertex.used_textures.size() > max_textures) {
        report.texture_slots_ok = false;
        report.errors.push_back("vertex shader texture slot bound exceeded");
    }

    if (report.fragment_valid) {
        if (program.fragment.max_register_used > max_registers) {
            report.register_budget_ok = false;
            report.errors.push_back("fragment shader register budget exceeded");
        }
        if (program.fragment.used_textures.size() > max_textures) {
            report.texture_slots_ok = false;
            report.errors.push_back("fragment shader texture slot bound exceeded");
        }
    }

    return report;
}

} // namespace nemu::core::gpu::shader