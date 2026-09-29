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
}

[[nodiscard]] bool MaxwellMacroEngine::HasMacro(u32 slot) const noexcept {
    if (slot >= MAX_MACRO_SLOTS) return false;
    return slots_[slot].is_valid && !slots_[slot].code.empty();
}

bool MaxwellMacroEngine::TryFastPath(u32 slot, std::span<const u32> params, const MethodCallback& on_method_emit) {
    if (params.empty() || !on_method_emit) return false;

    // Fast-path: recognize known NVN / Maxwell standard helper macro signatures
    // e.g. Macro 0: DrawArrays (method 0x0674) with count and first vertex
    if (slot == 0 && params.size() >= 2) {
        on_method_emit(0x0674, params[0]);
        return true;
    }
    // Macro 1: DrawElements (method 0x0675) with index count and start index
    if (slot == 1 && params.size() >= 2) {
        on_method_emit(0x0675, params[0]);
        return true;
    }

    return false;
}

void MaxwellMacroEngine::Execute(u32 slot, std::span<const u32> params, const MethodCallback& on_method_emit) {
    if (slot >= MAX_MACRO_SLOTS || !slots_[slot].is_valid || slots_[slot].code.empty()) {
        NEMU_LOG_DEBUG("gpu", "MaxwellMacro: Macro slot {} not bound or empty", slot);
        return;
    }

    // Try compiled fast-path first
    if (TryFastPath(slot, params, on_method_emit)) {
        slots_[slot].execution_count++;
        return;
    }

    auto& macro = slots_[slot];
    macro.execution_count++;

    // Maxwell Macro Interpreter State
    std::array<u32, MAX_REGISTERS> r{};
    u32 method_address = 0;
    size_t param_idx = 0;
    bool carry = false;

    size_t pc = 0;
    const size_t code_size = macro.code.size();

    while (pc < code_size) {
        const u32 inst = macro.code[pc++];

        // Maxwell MME instruction word encoding:
        // [2:0]   ALU Operation
        // [5:3]   Source Register A (R0..R7)
        // [8:6]   Source Register B (R0..R7)
        // [11:9]  Destination Register (R0..R7)
        // [17:12] Condition / Branch modifier
        // [18]    Emit Method Output Flag
        // [31:19] Immediate Value / Method Address Offset
        const u32 op       = inst & 0x7;
        const u32 src_a    = (inst >> 3) & 0x7;
        const u32 src_b    = (inst >> 6) & 0x7;
        const u32 dst      = (inst >> 9) & 0x7;
        const bool emit    = (inst & (1u << 18)) != 0;
        const u32 imm      = (inst >> 19);

        // Fetch operands (R1 is parameter stream fetch if selected)
        u32 a = (src_a == 1 && param_idx < params.size()) ? params[param_idx++] : r[src_a];
        u32 b = (src_b == 1 && param_idx < params.size()) ? params[param_idx++] : r[src_b];

        u32 result = 0;
        switch (op) {
            case 0: // ALU: ADD
                result = a + b + imm;
                carry = (result < a);
                break;
            case 1: // ALU: ADDC (Add with carry)
                result = a + b + (carry ? 1 : 0);
                carry = (result < a);
                break;
            case 2: // ALU: SUB
                result = a - b - imm;
                carry = (a < b);
                break;
            case 3: // ALU: SUBB (Subtract with borrow)
                result = a - b - (carry ? 1 : 0);
                carry = (a < b);
                break;
            case 4: // BITWISE: OR
                result = a | b | imm;
                break;
            case 5: // BITWISE: AND
                result = a & b & ~imm;
                break;
            case 6: // BITWISE: XOR
                result = a ^ b ^ imm;
                break;
            case 7: // SHIFT / MOV
                result = (imm != 0) ? (a << (imm & 0x1F)) : (a >> (b & 0x1F));
                break;
        }

        if (dst != 0) { // R0 is hardwired to 0
            r[dst] = result;
        }

        // Check if this instruction emits a method to the GPU pipe
        if (emit && on_method_emit) {
            if (method_address == 0) {
                method_address = imm;
            }
            on_method_emit(method_address, result);
            method_address++; // Auto-increment method address for consecutive sends
        }
    }
}

} // namespace nemu::core::gpu
