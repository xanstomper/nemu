#pragma once

#include "core/types.hpp"
#include <string>
#include <string_view>

namespace nemu::core::gpu::shader {

/// Result of an off-device HLSL source inspection. The real D3DCompile step
/// only runs on Windows/Xbox; this cross-platform validator catches the
/// decoder-emitter failures that would otherwise show up only at D3DCompile
/// time (truncated source, missing entry point, unbalanced braces, bad
/// struct interface). It runs headless so guest programs are rejected before
/// they reach the console.
struct HlslValidationResult {
    bool has_main{false};
    bool balanced_braces{true};
    bool has_vs_struct{false};
    bool has_ps_struct{false};
    size_t source_length{0};
    std::string error{};

    [[nodiscard]] bool Passed() const noexcept {
        return has_main && balanced_braces && error.empty() && source_length > 16;
    }
};

class HlslValidator {
public:
    /// Basic structural checks shared by VS and PS HLSL units.
    static HlslValidationResult Validate(std::string_view source);
};

} // namespace nemu::core::gpu::shader