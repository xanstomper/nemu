#pragma once

#include "core/types.hpp"
#include <span>
#include <vector>
#include <array>
#include <functional>
#include <string_view>

namespace nemu::core::gpu {

/// Represents a Maxwell 3D GPU Macro instruction (MME).
/// Games compile microcode into the GPU pushbuffer to automate multi-register
/// setups (draw calls, index binding, scissor arrays) without CPU overhead.
class MaxwellMacroEngine {
public:
    static constexpr size_t MAX_MACRO_SLOTS = 128;
    static constexpr size_t MAX_REGISTERS = 8;

    using MethodCallback = std::function<void(u32 method, u32 argument)>;

    MaxwellMacroEngine();
    ~MaxwellMacroEngine() = default;

    /// Reset all uploaded macro programs and execution registers
    void Reset();

    /// Set current active macro slot for streaming microcode uploads (method 0x38)
    void BindMacro(u32 slot);

    /// Upload a 32-bit microcode word into the currently bound macro program (method 0x39)
    void LoadMacroCode(u32 code_word);

    /// Upload a complete microcode binary directly into a macro slot
    void SetMacroProgram(u32 slot, std::span<const u32> code);

    /// Check if a macro slot contains a valid executable program
    [[nodiscard]] bool HasMacro(u32 slot) const noexcept;

    /// Execute a macro program at the given slot with input parameters
    /// @param slot Macro index (0..127) corresponding to methods 0xE00..0xE7F
    /// @param params Pushbuffer parameter stream provided to the macro call
    /// @param on_method_emit Callback invoked whenever the macro sends a method write to Maxwell 3D
    void Execute(u32 slot, std::span<const u32> params, const MethodCallback& on_method_emit);

    /// JIT / Fast-path detection for known NVN macro routines (e.g. DrawArraysIndirect)
    [[nodiscard]] bool TryFastPath(u32 slot, std::span<const u32> params, const MethodCallback& on_method_emit);

private:
    struct MacroSlot {
        std::vector<u32> code;
        bool is_valid{false};
        u32 execution_count{0};
    };

    std::array<MacroSlot, MAX_MACRO_SLOTS> slots_{};
    u32 current_upload_slot_{0};
    std::array<u32, MAX_REGISTERS> regs_{};
};

} // namespace nemu::core::gpu
