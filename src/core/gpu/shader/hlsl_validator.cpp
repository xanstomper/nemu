#include "hlsl_validator.hpp"

namespace nemu::core::gpu::shader {

HlslValidationResult HlslValidator::Validate(std::string_view source) {
    HlslValidationResult r{};
    r.source_length = source.size();

    if (source.empty()) {
        r.error = "empty shader source";
        r.balanced_braces = false;
        return r;
    }

    // Entry point present?
    r.has_main = source.find("main(") != std::string_view::npos;

    // Struct interface clues (matching the ShaderTranslator emitter).
    r.has_vs_struct = source.find("VSOutput") != std::string_view::npos ||
                      source.find("VSInput") != std::string_view::npos;
    r.has_ps_struct = source.find("PSOutput") != std::string_view::npos ||
                      source.find("PSInput") != std::string_view::npos;

    // Balanced brace scan (ignores string literals to stay simple but still
    // catches truncated emissions that would fail D3DCompile).
    int depth = 0;
    for (char c : source) {
        if (c == '{') ++depth;
        else if (c == '}') {
            --depth;
            if (depth < 0) {
                r.balanced_braces = false;
                r.error = "unmatched closing brace";
                return r;
            }
        }
    }
    if (depth != 0) {
        r.balanced_braces = false;
        r.error = "unbalanced braces";
        return r;
    }

    if (!r.has_main) {
        r.error = "missing 'main' entry point";
    }
    return r;
}

} // namespace nemu::core::gpu::shader