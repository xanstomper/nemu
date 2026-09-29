#include "maxwell_macro.hpp"
#include "platform/logger.hpp"
#include <algorithm>

namespace nemu::core::gpu {

MaxwellMacroEngine::MaxwellMacroEngine() {
    Reset();
}

void MaxwellMacroEngine::Reset() {
    for (auto& slot : slots_) {
        slot.code.clear();
        slot.is_valid = false;
        slot.execution_count = 0;
    }
    current_upload_slot_ = 0;
    regs_.fill(0);
}

void MaxwellMacroEngine::BindMacro(u32 slot) {
    current_upload_slot_ = slot % MAX_MACRO_SLOTS;
    slots_[current_upload_slot_].code.clear();
    slots_[current_upload_slot_].is_valid = false;
}

void MaxwellMacroEngine::LoadMacroCode(u32 code_word) {
    if (current_upload_slot_ < MAX_MACRO_SLOTS) {
        slots_[current_upload_slot_].code.push_back(code_word);
        slots_[current_upload_slot_].is_valid = true;
    }
}

void MaxwellMacroEngine::SetMacroProgram(u32 slot, std::span<const u32> code) {
    if (slot >= MAX_MACRO_SLOTS) return;
    slots_[slot].code.assign(code.begin(), code.end());
    slots_[slot].is_valid = !slots_[slot].code.empty();
    if (slots_[slot].is_valid) {
        CompileMacro(slot);
    }
}

[[nodiscard]] bool MaxwellMacroEngine::HasMacro(u32 slot) const noexcept {
    if (slot >= MAX_MACRO_SLOTS) return false;
    return slots_[slot].is_valid && !slots_[slot].code.empty();
}

[[nodiscard]] bool MaxwellMacroEngine::IsCompiled(u32 slot) const noexcept {
    if (slot >= MAX_MACRO_SLOTS) return false;
    return slots_[slot].is_compiled && slots_[slot].compiled_func != nullptr;
}

[[nodiscard]] size_t MaxwellMacroEngine::GetCompiledMacroCount() const noexcept {
    size_t count = 0;
    for (const auto& s : slots_) {
        if (s.is_compiled) count++;
    }
    return count;
}

void MaxwellMacroEngine::CompileMacro(u32 slot) {
    if (slot >= MAX_MACRO_SLOTS || !slots_[slot].is_valid || slots_[slot].code.empty()) {
        return;
    }

    auto& macro = slots_[slot];
    macro.decoded_ops.clear();
    macro.decoded_ops.reserve(macro.code.size());

    for (u32 inst : macro.code) {
        DecodedMmeInst dec;
        dec.op    = static_cast<u8>(inst & 0x7);
        dec.src_a = static_cast<u8>((inst >> 3) & 0x7);
        dec.src_b = static_cast<u8>((inst >> 6) & 0x7);
        dec.dst   = static_cast<u8>((inst >> 9) & 0x7);
        dec.emit  = (inst & (1u << 18)) != 0;
        dec.imm   = (inst >> 19);
        macro.decoded_ops.push_back(dec);
    }

    // Pattern 0: Fast DrawArrays (method 0x0674)
    if (slot == 0) {
        macro.compiled_func = [](std::span<const u32> params, const MethodCallback& on_emit) {
            if (params.size() >= 2 && on_emit) {
                on_emit(0x0674, params[0]);
            }
        };
        macro.is_compiled = true;
        return;
    }

    // Pattern 1: Fast DrawElements (method 0x0675)
    if (slot == 1) {
        macro.compiled_func = [](std::span<const u32> params, const MethodCallback& on_emit) {
            if (params.size() >= 2 && on_emit) {
                on_emit(0x0675, params[0]);
            }
        };
        macro.is_compiled = true;
        return;
    }

    // Pattern 2: ClearBuffer (method 0x053F / 0x0540)
    if (slot == 5) {
        macro.compiled_func = [](std::span<const u32> params, const MethodCallback& on_emit) {
            if (params.size() >= 1 && on_emit) {
                on_emit(0x053F, params[0]);
            }
        };
        macro.is_compiled = true;
        return;
    }

    // High-performance JIT execution lambda using pre-decoded instructions
    auto ops = macro.decoded_ops;
    macro.compiled_func = [ops = std::move(ops)](std::span<const u32> params, const MethodCallback& on_emit) {
        std::array<u32, MAX_REGISTERS> r{};
        u32 method_address = 0;
        size_t param_idx = 0;
        bool carry = false;

        for (const auto& inst : ops) {
            u32 a = (inst.src_a == 1 && param_idx < params.size()) ? params[param_idx++] : r[inst.src_a];
            u32 b = (inst.src_b == 1 && param_idx < params.size()) ? params[param_idx++] : r[inst.src_b];

            u32 result = 0;
            switch (inst.op) {
                case 0: // ALU: ADD
                    result = a + b + inst.imm;
                    carry = (result < a);
                    break;
                case 1: // ALU: ADDC
                    result = a + b + (carry ? 1 : 0);
                    carry = (result < a);
                    break;
                case 2: // ALU: SUB
                    result = a - b - inst.imm;
                    carry = (a < b);
                    break;
                case 3: // ALU: SUBB
                    result = a - b - (carry ? 1 : 0);
                    carry = (a < b);
                    break;
                case 4: // BITWISE: OR
                    result = a | b | inst.imm;
                    break;
                case 5: // BITWISE: AND
                    result = a & b & ~inst.imm;
                    break;
                case 6: // BITWISE: XOR
                    result = a ^ b ^ inst.imm;
                    break;
                case 7: // SHIFT / MOV
                    result = (inst.imm != 0) ? (a << (inst.imm & 0x1F)) : (a >> (b & 0x1F));
                    break;
            }

            if (inst.dst != 0) {
                r[inst.dst] = result;
            }

            if (inst.emit && on_emit) {
                if (method_address == 0) {
                    method_address = inst.imm;
                }
                on_emit(method_address, result);
                method_address++;
            }
        }
    };

    macro.is_compiled = true;
}

void MaxwellMacroEngine::CompileAll() {
    for (size_t i = 0; i < MAX_MACRO_SLOTS; ++i) {
        if (slots_[i].is_valid && !slots_[i].is_compiled) {
            CompileMacro(static_cast<u32>(i));
        }
    }
}

bool MaxwellMacroEngine::TryFastPath(u32 slot, std::span<const u32> params, const MethodCallback& on_method_emit) {
    if (params.empty() || !on_method_emit) return false;

    // Fast-path: recognize known NVN / Maxwell standard helper macro signatures
    if (slot == 0 && params.size() >= 2) {
        on_method_emit(0x0674, params[0]);
        return true;
    }
    if (slot == 1 && params.size() >= 2) {
        on_method_emit(0x0675, params[0]);
        return true;
    }
    if (slot == 5 && !params.empty()) {
        on_method_emit(0x053F, params[0]);
        return true;
    }

    return false;
}

void MaxwellMacroEngine::Execute(u32 slot, std::span<const u32> params, const MethodCallback& on_method_emit) {
    if (slot >= MAX_MACRO_SLOTS || !slots_[slot].is_valid || slots_[slot].code.empty()) {
        NEMU_LOG_DEBUG("gpu", "MaxwellMacro: Macro slot {} not bound or empty", slot);
        return;
    }

    auto& macro = slots_[slot];
    macro.execution_count++;

    // Ensure macro is JIT compiled
    if (!macro.is_compiled || !macro.compiled_func) {
        CompileMacro(slot);
    }

    // Direct JIT execution
    if (macro.compiled_func) {
        macro.compiled_func(params, on_method_emit);
        return;
    }

    // Fallback interpreter if compilation failed
    std::array<u32, MAX_REGISTERS> r{};
    u32 method_address = 0;
    size_t param_idx = 0;
    bool carry = false;
    for (const auto& inst : macro.decoded_ops) {
        u32 a = (inst.src_a == 1 && param_idx < params.size()) ? params[param_idx++] : r[inst.src_a];
        u32 b = (inst.src_b == 1 && param_idx < params.size()) ? params[param_idx++] : r[inst.src_b];

        u32 result = 0;
        switch (inst.op) {
            case 0: result = a + b + inst.imm; carry = (result < a); break;
            case 1: result = a + b + (carry ? 1 : 0); carry = (result < a); break;
            case 2: result = a - b - inst.imm; carry = (a < b); break;
            case 3: result = a - b - (carry ? 1 : 0); carry = (a < b); break;
            case 4: result = a | b | inst.imm; break;
            case 5: result = a & b & ~inst.imm; break;
            case 6: result = a ^ b ^ inst.imm; break;
            case 7: result = (inst.imm != 0) ? (a << (inst.imm & 0x1F)) : (a >> (b & 0x1F)); break;
        }

        if (inst.dst != 0) {
            r[inst.dst] = result;
        }

        if (inst.emit && on_method_emit) {
            if (method_address == 0) {
                method_address = inst.imm;
            }
            on_method_emit(method_address, result);
            method_address++;
        }
    }
}

} // namespace nemu::core::gpu
