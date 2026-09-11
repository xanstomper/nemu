#include "interpreter.hpp"
#include "platform/logger.hpp"

namespace nemu::core::cpu {

Interpreter::Interpreter(CpuState& state, memory::IMemory& memory)
    : state_(state), memory_(memory) {}

u64 Interpreter::ApplyShift(u64 value, u8 shift_type, u8 amount, bool is_64bit) {
    const u32 max_bits = is_64bit ? 64 : 32;
    if (amount == 0) return value;
    if (amount >= max_bits) amount = static_cast<u8>(amount % max_bits);

    if (is_64bit) {
        switch (shift_type) {
            case 0: return value << amount; // LSL
            case 1: return value >> amount; // LSR
            case 2: return static_cast<u64>(static_cast<s64>(value) >> amount); // ASR
            case 3: return std::rotr(value, amount); // ROR
            default: return value;
        }
    } else {
        const u32 val32 = static_cast<u32>(value);
        switch (shift_type) {
            case 0: return static_cast<u32>(val32 << amount);
            case 1: return static_cast<u32>(val32 >> amount);
            case 2: return static_cast<u32>(static_cast<s32>(val32) >> amount);
            case 3: return std::rotr(val32, amount);
            default: return val32;
        }
    }
}

StepResult Interpreter::Step() {
    if (state_.halted) {
        return StepResult::Halted;
    }

    if (!memory_.IsValidAddress(state_.pc, 4)) {
        NEMU_LOG_ERROR("CPU", "PC 0x{:016X} is not valid memory", state_.pc);
        return StepResult::MemoryFault;
    }

    const u32 raw_inst = memory_.Read32(state_.pc);
    const DecodedInstruction inst = Decoder::Decode(raw_inst);

    if (inst.opcode == Opcode::UNDEFINED) {
        NEMU_LOG_ERROR("CPU", "Undefined instruction 0x{:08X} at PC 0x{:016X}", raw_inst, state_.pc);
        return StepResult::UndefinedInstruction;
    }

    const StepResult res = Execute(inst);
    state_.total_instructions++;
    return res;
}

StepResult Interpreter::Run(size_t instruction_count) {
    for (size_t i = 0; i < instruction_count; ++i) {
        const StepResult res = Step();
        if (res != StepResult::Ok) {
            return res;
        }
    }
    return StepResult::Ok;
}

StepResult Interpreter::Execute(const DecodedInstruction& inst) {
    const vaddr_t curr_pc = state_.pc;
    vaddr_t next_pc = curr_pc + 4;

    switch (inst.opcode) {
        case Opcode::NOP:
            break;

        case Opcode::ADD_imm: {
            if (inst.is_64bit) {
                const u64 src = state_.GetRegOrSP(inst.rn);
                state_.SetRegOrSP(inst.rd, src + inst.imm);
            } else {
                const u32 src = state_.GetWRegOrSP(inst.rn);
                state_.SetWRegOrSP(inst.rd, src + static_cast<u32>(inst.imm));
            }
            break;
        }

        case Opcode::ADDS_imm: {
            if (inst.is_64bit) {
                const u64 src = state_.GetRegOrSP(inst.rn);
                const u64 res = src + inst.imm;
                state_.SetNZCV_Add64(src, inst.imm, res);
                state_.SetX(inst.rd, res);
            } else {
                const u32 src = state_.GetWRegOrSP(inst.rn);
                const u32 imm32 = static_cast<u32>(inst.imm);
                const u32 res = src + imm32;
                state_.SetNZCV_Add32(src, imm32, res);
                state_.SetW(inst.rd, res);
            }
            break;
        }

        case Opcode::SUB_imm: {
            if (inst.is_64bit) {
                const u64 src = state_.GetRegOrSP(inst.rn);
                state_.SetRegOrSP(inst.rd, src - inst.imm);
            } else {
                const u32 src = state_.GetWRegOrSP(inst.rn);
                state_.SetWRegOrSP(inst.rd, src - static_cast<u32>(inst.imm));
            }
            break;
        }

        case Opcode::SUBS_imm: {
            if (inst.is_64bit) {
                const u64 src = state_.GetRegOrSP(inst.rn);
                const u64 res = src - inst.imm;
                state_.SetNZCV_Sub64(src, inst.imm, res);
                state_.SetX(inst.rd, res);
            } else {
                const u32 src = state_.GetWRegOrSP(inst.rn);
                const u32 imm32 = static_cast<u32>(inst.imm);
                const u32 res = src - imm32;
                state_.SetNZCV_Sub32(src, imm32, res);
                state_.SetW(inst.rd, res);
            }
            break;
        }

        case Opcode::ADD_reg: {
            const u64 rm = ApplyShift(inst.is_64bit ? state_.GetX(inst.rm) : state_.GetW(inst.rm),
                                      inst.shift_type, inst.shift_amount, inst.is_64bit);
            if (inst.is_64bit) {
                const u64 rn = state_.GetRegOrSP(inst.rn);
                state_.SetRegOrSP(inst.rd, rn + rm);
            } else {
                const u32 rn = state_.GetWRegOrSP(inst.rn);
                state_.SetWRegOrSP(inst.rd, rn + static_cast<u32>(rm));
            }
            break;
        }

        case Opcode::ADDS_reg: {
            const u64 rm = ApplyShift(inst.is_64bit ? state_.GetX(inst.rm) : state_.GetW(inst.rm),
                                      inst.shift_type, inst.shift_amount, inst.is_64bit);
            if (inst.is_64bit) {
                const u64 rn = state_.GetRegOrSP(inst.rn);
                const u64 res = rn + rm;
                state_.SetNZCV_Add64(rn, rm, res);
                state_.SetX(inst.rd, res);
            } else {
                const u32 rn = state_.GetWRegOrSP(inst.rn);
                const u32 rm32 = static_cast<u32>(rm);
                const u32 res = rn + rm32;
                state_.SetNZCV_Add32(rn, rm32, res);
                state_.SetW(inst.rd, res);
            }
            break;
        }

        case Opcode::SUB_reg: {
            const u64 rm = ApplyShift(inst.is_64bit ? state_.GetX(inst.rm) : state_.GetW(inst.rm),
                                      inst.shift_type, inst.shift_amount, inst.is_64bit);
            if (inst.is_64bit) {
                const u64 rn = state_.GetRegOrSP(inst.rn);
                state_.SetRegOrSP(inst.rd, rn - rm);
            } else {
                const u32 rn = state_.GetWRegOrSP(inst.rn);
                state_.SetWRegOrSP(inst.rd, rn - static_cast<u32>(rm));
            }
            break;
        }

        case Opcode::SUBS_reg: {
            const u64 rm = ApplyShift(inst.is_64bit ? state_.GetX(inst.rm) : state_.GetW(inst.rm),
                                      inst.shift_type, inst.shift_amount, inst.is_64bit);
            if (inst.is_64bit) {
                const u64 rn = state_.GetRegOrSP(inst.rn);
                const u64 res = rn - rm;
                state_.SetNZCV_Sub64(rn, rm, res);
                state_.SetX(inst.rd, res);
            } else {
                const u32 rn = state_.GetWRegOrSP(inst.rn);
                const u32 rm32 = static_cast<u32>(rm);
                const u32 res = rn - rm32;
                state_.SetNZCV_Sub32(rn, rm32, res);
                state_.SetW(inst.rd, res);
            }
            break;
        }

        case Opcode::MOVZ: {
            const u64 val = inst.imm << inst.shift_amount;
            if (inst.is_64bit) {
                state_.SetX(inst.rd, val);
            } else {
                state_.SetW(inst.rd, static_cast<u32>(val));
            }
            break;
        }

        case Opcode::MOVN: {
            const u64 val = ~(inst.imm << inst.shift_amount);
            if (inst.is_64bit) {
                state_.SetX(inst.rd, val);
            } else {
                state_.SetW(inst.rd, static_cast<u32>(val));
            }
            break;
        }

        case Opcode::MOVK: {
            const u64 mask = ~(0xFFFFULL << inst.shift_amount);
            if (inst.is_64bit) {
                const u64 orig = state_.GetX(inst.rd);
                state_.SetX(inst.rd, (orig & mask) | (inst.imm << inst.shift_amount));
            } else {
                const u32 orig = state_.GetW(inst.rd);
                const u32 mask32 = static_cast<u32>(mask);
                state_.SetW(inst.rd, (orig & mask32) | static_cast<u32>(inst.imm << inst.shift_amount));
            }
            break;
        }

        case Opcode::AND_reg: {
            const u64 rm = ApplyShift(inst.is_64bit ? state_.GetX(inst.rm) : state_.GetW(inst.rm),
                                      inst.shift_type, inst.shift_amount, inst.is_64bit);
            if (inst.is_64bit) {
                state_.SetX(inst.rd, state_.GetX(inst.rn) & rm);
            } else {
                state_.SetW(inst.rd, state_.GetW(inst.rn) & static_cast<u32>(rm));
            }
            break;
        }

        case Opcode::ANDS_reg: {
            const u64 rm = ApplyShift(inst.is_64bit ? state_.GetX(inst.rm) : state_.GetW(inst.rm),
                                      inst.shift_type, inst.shift_amount, inst.is_64bit);
            if (inst.is_64bit) {
                const u64 res = state_.GetX(inst.rn) & rm;
                state_.SetNZ_Logical64(res);
                state_.SetX(inst.rd, res);
            } else {
                const u32 res = state_.GetW(inst.rn) & static_cast<u32>(rm);
                state_.SetNZ_Logical32(res);
                state_.SetW(inst.rd, res);
            }
            break;
        }

        case Opcode::ORR_reg: {
            const u64 rm = ApplyShift(inst.is_64bit ? state_.GetX(inst.rm) : state_.GetW(inst.rm),
                                      inst.shift_type, inst.shift_amount, inst.is_64bit);
            if (inst.is_64bit) {
                state_.SetX(inst.rd, state_.GetX(inst.rn) | rm);
            } else {
                state_.SetW(inst.rd, state_.GetW(inst.rn) | static_cast<u32>(rm));
            }
            break;
        }

        case Opcode::ORN_reg: {
            const u64 rm = ApplyShift(inst.is_64bit ? state_.GetX(inst.rm) : state_.GetW(inst.rm),
                                      inst.shift_type, inst.shift_amount, inst.is_64bit);
            if (inst.is_64bit) {
                state_.SetX(inst.rd, state_.GetX(inst.rn) | ~rm);
            } else {
                state_.SetW(inst.rd, state_.GetW(inst.rn) | ~static_cast<u32>(rm));
            }
            break;
        }

        case Opcode::EOR_reg: {
            const u64 rm = ApplyShift(inst.is_64bit ? state_.GetX(inst.rm) : state_.GetW(inst.rm),
                                      inst.shift_type, inst.shift_amount, inst.is_64bit);
            if (inst.is_64bit) {
                state_.SetX(inst.rd, state_.GetX(inst.rn) ^ rm);
            } else {
                state_.SetW(inst.rd, state_.GetW(inst.rn) ^ static_cast<u32>(rm));
            }
            break;
        }

        case Opcode::BIC_reg: {
            const u64 rm = ApplyShift(inst.is_64bit ? state_.GetX(inst.rm) : state_.GetW(inst.rm),
                                      inst.shift_type, inst.shift_amount, inst.is_64bit);
            if (inst.is_64bit) {
                state_.SetX(inst.rd, state_.GetX(inst.rn) & ~rm);
            } else {
                state_.SetW(inst.rd, state_.GetW(inst.rn) & ~static_cast<u32>(rm));
            }
            break;
        }

        case Opcode::LSLV: {
            const u32 shift = static_cast<u32>(state_.GetX(inst.rm) & (inst.is_64bit ? 63 : 31));
            if (inst.is_64bit) {
                state_.SetX(inst.rd, state_.GetX(inst.rn) << shift);
            } else {
                state_.SetW(inst.rd, state_.GetW(inst.rn) << shift);
            }
            break;
        }

        case Opcode::LSRV: {
            const u32 shift = static_cast<u32>(state_.GetX(inst.rm) & (inst.is_64bit ? 63 : 31));
            if (inst.is_64bit) {
                state_.SetX(inst.rd, state_.GetX(inst.rn) >> shift);
            } else {
                state_.SetW(inst.rd, state_.GetW(inst.rn) >> shift);
            }
            break;
        }

        case Opcode::ASRV: {
            const u32 shift = static_cast<u32>(state_.GetX(inst.rm) & (inst.is_64bit ? 63 : 31));
            if (inst.is_64bit) {
                state_.SetX(inst.rd, static_cast<u64>(static_cast<s64>(state_.GetX(inst.rn)) >> shift));
            } else {
                state_.SetW(inst.rd, static_cast<u32>(static_cast<s32>(state_.GetW(inst.rn)) >> shift));
            }
            break;
        }

        case Opcode::MADD: {
            if (inst.is_64bit) {
                const u64 prod = state_.GetX(inst.rn) * state_.GetX(inst.rm);
                state_.SetX(inst.rd, state_.GetX(inst.ra) + prod);
            } else {
                const u32 prod = state_.GetW(inst.rn) * state_.GetW(inst.rm);
                state_.SetW(inst.rd, state_.GetW(inst.ra) + prod);
            }
            break;
        }

        case Opcode::MSUB: {
            if (inst.is_64bit) {
                const u64 prod = state_.GetX(inst.rn) * state_.GetX(inst.rm);
                state_.SetX(inst.rd, state_.GetX(inst.ra) - prod);
            } else {
                const u32 prod = state_.GetW(inst.rn) * state_.GetW(inst.rm);
                state_.SetW(inst.rd, state_.GetW(inst.ra) - prod);
            }
            break;
        }

        case Opcode::ADR: {
            state_.SetX(inst.rd, curr_pc + inst.imm);
            break;
        }

        case Opcode::ADRP: {
            const vaddr_t page_pc = curr_pc & ~0xFFFULL;
            state_.SetX(inst.rd, page_pc + (inst.imm << 12));
            break;
        }

        case Opcode::B: {
            next_pc = curr_pc + inst.imm;
            break;
        }

        case Opcode::BL: {
            state_.SetX(30, curr_pc + 4); // Save return address in LR (X30)
            next_pc = curr_pc + inst.imm;
            break;
        }

        case Opcode::B_cond: {
            if (state_.CheckCondition(inst.condition)) {
                next_pc = curr_pc + inst.imm;
            }
            break;
        }

        case Opcode::BLR: {
            const vaddr_t target = state_.GetX(inst.rn);
            state_.SetX(30, curr_pc + 4);
            next_pc = target;
            break;
        }

        case Opcode::RET: {
            next_pc = state_.GetX(inst.rn);
            break;
        }

        case Opcode::CBZ: {
            const u64 val = inst.is_64bit ? state_.GetX(inst.rd) : state_.GetW(inst.rd);
            if (val == 0) {
                next_pc = curr_pc + inst.imm;
            }
            break;
        }

        case Opcode::CBNZ: {
            const u64 val = inst.is_64bit ? state_.GetX(inst.rd) : state_.GetW(inst.rd);
            if (val != 0) {
                next_pc = curr_pc + inst.imm;
            }
            break;
        }

        case Opcode::TBZ: {
            const u64 val = state_.GetX(inst.rd);
            if ((val & (1ULL << inst.bit_pos)) == 0) {
                next_pc = curr_pc + inst.imm;
            }
            break;
        }

        case Opcode::TBNZ: {
            const u64 val = state_.GetX(inst.rd);
            if ((val & (1ULL << inst.bit_pos)) != 0) {
                next_pc = curr_pc + inst.imm;
            }
            break;
        }

        case Opcode::LDR_imm: {
            const vaddr_t addr = state_.GetRegOrSP(inst.rn) + inst.imm;
            if (inst.is_64bit) {
                state_.SetX(inst.rd, memory_.Read64(addr));
            } else {
                state_.SetW(inst.rd, memory_.Read32(addr));
            }
            break;
        }

        case Opcode::STR_imm: {
            const vaddr_t addr = state_.GetRegOrSP(inst.rn) + inst.imm;
            if (inst.is_64bit) {
                memory_.Write64(addr, state_.GetX(inst.rd));
            } else {
                memory_.Write32(addr, state_.GetW(inst.rd));
            }
            break;
        }

        case Opcode::LDRB_imm: {
            const vaddr_t addr = state_.GetRegOrSP(inst.rn) + inst.imm;
            state_.SetW(inst.rd, memory_.Read8(addr));
            break;
        }

        case Opcode::STRB_imm: {
            const vaddr_t addr = state_.GetRegOrSP(inst.rn) + inst.imm;
            memory_.Write8(addr, static_cast<u8>(state_.GetW(inst.rd)));
            break;
        }

        case Opcode::LDRH_imm: {
            const vaddr_t addr = state_.GetRegOrSP(inst.rn) + inst.imm;
            state_.SetW(inst.rd, memory_.Read16(addr));
            break;
        }

        case Opcode::STRH_imm: {
            const vaddr_t addr = state_.GetRegOrSP(inst.rn) + inst.imm;
            memory_.Write16(addr, static_cast<u16>(state_.GetW(inst.rd)));
            break;
        }

        case Opcode::LDP: {
            const vaddr_t base = state_.GetRegOrSP(inst.rn) + inst.imm;
            if (inst.is_64bit) {
                state_.SetX(inst.rd, memory_.Read64(base));
                state_.SetX(inst.rt2, memory_.Read64(base + 8));
            } else {
                state_.SetW(inst.rd, memory_.Read32(base));
                state_.SetW(inst.rt2, memory_.Read32(base + 4));
            }
            break;
        }

        case Opcode::STP: {
            const vaddr_t base = state_.GetRegOrSP(inst.rn) + inst.imm;
            if (inst.is_64bit) {
                memory_.Write64(base, state_.GetX(inst.rd));
                memory_.Write64(base + 8, state_.GetX(inst.rt2));
            } else {
                memory_.Write32(base, state_.GetW(inst.rd));
                memory_.Write32(base + 4, state_.GetW(inst.rt2));
            }
            break;
        }

        case Opcode::CSEL: {
            if (state_.CheckCondition(inst.condition)) {
                if (inst.is_64bit) {
                    state_.SetX(inst.rd, state_.GetX(inst.rn));
                } else {
                    state_.SetW(inst.rd, state_.GetW(inst.rn));
                }
            } else {
                if (inst.is_64bit) {
                    state_.SetX(inst.rd, state_.GetX(inst.rm));
                } else {
                    state_.SetW(inst.rd, state_.GetW(inst.rm));
                }
            }
            break;
        }

        case Opcode::SVC: {
            state_.pc = next_pc;
            if (svc_handler_) {
                svc_handler_(state_, static_cast<u32>(inst.imm));
            }
            return StepResult::Svc;
        }

        case Opcode::BRK:
            state_.pc = next_pc;
            return StepResult::Break;

        default:
            NEMU_LOG_ERROR("CPU", "Unhandled opcode {} at 0x{:016X}", inst.OpcodeName(), curr_pc);
            return StepResult::UndefinedInstruction;
    }

    state_.pc = next_pc;
    return StepResult::Ok;
}

} // namespace nemu::core::cpu
