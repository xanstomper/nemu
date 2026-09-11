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
    ~Interpreter() = default;

    StepResult Step();
    StepResult Run(size_t instruction_count);

    void SetSvcHandler(SvcHandler handler) { svc_handler_ = std::move(handler); }

private:
    CpuState& state_;
    memory::IMemory& memory_;
    SvcHandler svc_handler_;

    // Execution handlers
    StepResult Execute(const DecodedInstruction& inst);

    // Helpers
    static u64 ApplyShift(u64 value, u8 shift_type, u8 amount, bool is_64bit);
};

} // namespace nemu::core::cpu
