#pragma once

#include "core/types.hpp"
#include "maxwell_shader_decoder.hpp"
#include <optional>
#include <string>
#include <vector>
#include <memory>
#include <span>

namespace nemu::core::gpu::shader {

/// A Maxwell shader program translated to a compilable HLSL source unit and
/// the resource usage it implies for the D3D12 pipeline.
struct TranslatedShader {
    ShaderStage stage{ShaderStage::Vertex};
    std::string hlsl_source{};
    std::vector<u32> used_cbuf_banks{};
    std::vector<u32> used_textures{};
    u32 max_register_used{0};
    bool has_discard{false};
    bool ok{false};
};

/// A complete shader stage pair for a draw submission (fragment is optional).
struct ShaderProgram {
    TranslatedShader vertex{};
    TranslatedShader fragment{};
};

/// Validation report for a translated shader program.
struct ShaderValidationReport {
    bool vertex_valid{false};
    bool fragment_valid{false};
    bool register_budget_ok{true};
    bool texture_slots_ok{true};
    std::vector<std::string> errors{};

    [[nodiscard]] bool Passed() const noexcept {
        return errors.empty() && vertex_valid && register_budget_ok && texture_slots_ok;
    }
};

/// Front-line of Guest graphics translation: decode Maxwell binary microcode to
/// a validated HLSL source ready for D3D12 / HLSL compile on Xbox.
///
/// This is platform-agnostic (no D3D12 dependency) so it can be unit tested on
/// a headless Linux host and reused verbatim by the Xbox D3D12 backend.
class ShaderTranslator {
public:
    /// Decode + decompile a Maxwell vertex or fragment program to HLSL.
    static TranslatedShader Translate(std::span<const u8> bytecode, ShaderStage stage,
                                      bool has_control_codes = false);

    /// Validate a decoded vertex+fragment program: register budget, texture
    /// slot bound, and at least a valid vertex stage. Fragment is optional
    /// (a program may rely on the fixed-function passthrough).
    static ShaderValidationReport Validate(const ShaderProgram& program, u32 max_registers = 256,
                                           u32 max_textures = 16);
};

} // namespace nemu::core::gpu::shader