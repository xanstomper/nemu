#pragma once

#include "core/types.hpp"
#include "cpu_state.hpp"
#include "instruction.hpp"
#include "decoder.hpp"
#include "core/memory/memory_interface.hpp"
#include <functional>

namespace nemu::core::cpu {

enum class StepResult {
    Ok,
    Halted,
    Svc,
    Break,
    UndefinedInstruction,
    MemoryFault
};

class Interpreter {
public:
    using SvcHandler = std::function<void(CpuState&, u32)>;

    Interpreter(CpuState& state, memory::IMemory& memory);
    Interpreter(CpuState& state, memory::IMemory* memory);
    ~Interpreter() = default;

    StepResult Step();
    StepResult Run(size_t instruction_count);
    StepResult Execute(const DecodedInstruction& inst);

    void SetSvcHandler(SvcHandler handler) { svc_handler_ = std::move(handler); }

private:
    CpuState& state_;
    memory::IMemory* memory_{nullptr};
    SvcHandler svc_handler_;

    // Helpers
    static u64 ApplyShift(u64 value, u8 shift_type, u8 amount, bool is_64bit);
    /// ARM ARM "extended register" operand (UXTB/UXTH/UXTW/UXTX/SXTB/SXTH/
    /// SXTW/SXTX then LSL #imm3) used by the ADD/SUB/AND/ORR/EOR extended
    /// register encodings.
    static u64 ApplyExtend(u64 value, u8 extend_op, u8 amount);
};

} // namespace nemu::core::cpu
