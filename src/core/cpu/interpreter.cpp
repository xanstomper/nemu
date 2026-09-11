#include "interpreter.hpp"
#include "platform/logger.hpp"
#include <cmath>
#include <cstring>

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

        // Scalar Floating-Point Arithmetic
        case Opcode::FADD_scalar: {
            if (inst.is_fp_double) {
                state_.SetDouble(inst.rd, state_.GetDouble(inst.rn) + state_.GetDouble(inst.rm));
            } else {
                state_.SetSingle(inst.rd, state_.GetSingle(inst.rn) + state_.GetSingle(inst.rm));
            }
            break;
        }

        case Opcode::FSUB_scalar: {
            if (inst.is_fp_double) {
                state_.SetDouble(inst.rd, state_.GetDouble(inst.rn) - state_.GetDouble(inst.rm));
            } else {
                state_.SetSingle(inst.rd, state_.GetSingle(inst.rn) - state_.GetSingle(inst.rm));
            }
            break;
        }

        case Opcode::FMUL_scalar: {
            if (inst.is_fp_double) {
                state_.SetDouble(inst.rd, state_.GetDouble(inst.rn) * state_.GetDouble(inst.rm));
            } else {
                state_.SetSingle(inst.rd, state_.GetSingle(inst.rn) * state_.GetSingle(inst.rm));
            }
            break;
        }

        case Opcode::FDIV_scalar: {
            if (inst.is_fp_double) {
                state_.SetDouble(inst.rd, state_.GetDouble(inst.rn) / state_.GetDouble(inst.rm));
            } else {
                state_.SetSingle(inst.rd, state_.GetSingle(inst.rn) / state_.GetSingle(inst.rm));
            }
            break;
        }

        case Opcode::FMAX_scalar: {
            if (inst.is_fp_double) {
                state_.SetDouble(inst.rd, std::fmax(state_.GetDouble(inst.rn), state_.GetDouble(inst.rm)));
            } else {
                state_.SetSingle(inst.rd, std::fmax(state_.GetSingle(inst.rn), state_.GetSingle(inst.rm)));
            }
            break;
        }

        case Opcode::FMIN_scalar: {
            if (inst.is_fp_double) {
                state_.SetDouble(inst.rd, std::fmin(state_.GetDouble(inst.rn), state_.GetDouble(inst.rm)));
            } else {
                state_.SetSingle(inst.rd, std::fmin(state_.GetSingle(inst.rn), state_.GetSingle(inst.rm)));
            }
            break;
        }

        case Opcode::FABS_scalar: {
            if (inst.is_fp_double) {
                state_.SetDouble(inst.rd, std::fabs(state_.GetDouble(inst.rn)));
            } else {
                state_.SetSingle(inst.rd, std::fabs(state_.GetSingle(inst.rn)));
            }
            break;
        }

        case Opcode::FNEG_scalar: {
            if (inst.is_fp_double) {
                state_.SetDouble(inst.rd, -state_.GetDouble(inst.rn));
            } else {
                state_.SetSingle(inst.rd, -state_.GetSingle(inst.rn));
            }
            break;
        }

        case Opcode::FSQRT_scalar: {
            if (inst.is_fp_double) {
                state_.SetDouble(inst.rd, std::sqrt(state_.GetDouble(inst.rn)));
            } else {
                state_.SetSingle(inst.rd, std::sqrt(state_.GetSingle(inst.rn)));
            }
            break;
        }

        case Opcode::FCMP_scalar: {
            const double val_a = inst.is_fp_double ? state_.GetDouble(inst.rn) : static_cast<double>(state_.GetSingle(inst.rn));
            const double val_b = (inst.rm == 31) ? 0.0 : (inst.is_fp_double ? state_.GetDouble(inst.rm) : static_cast<double>(state_.GetSingle(inst.rm)));
            state_.SetNZCV_FPCmp(val_a, val_b);
            break;
        }

        case Opcode::FCSEL_scalar: {
            if (state_.CheckCondition(inst.condition)) {
                if (inst.is_fp_double) state_.SetDouble(inst.rd, state_.GetDouble(inst.rn));
                else state_.SetSingle(inst.rd, state_.GetSingle(inst.rn));
            } else {
                if (inst.is_fp_double) state_.SetDouble(inst.rd, state_.GetDouble(inst.rm));
                else state_.SetSingle(inst.rd, state_.GetSingle(inst.rm));
            }
            break;
        }

        case Opcode::FMOV_reg: {
            if (inst.is_fp_double) state_.SetDouble(inst.rd, state_.GetDouble(inst.rn));
            else state_.SetSingle(inst.rd, state_.GetSingle(inst.rn));
            break;
        }

        case Opcode::FMOV_imm: {
            if (inst.is_fp_double) state_.SetDouble(inst.rd, inst.fp_imm);
            else state_.SetSingle(inst.rd, static_cast<float>(inst.fp_imm));
            break;
        }

        case Opcode::FMOV_to_gp: {
            if (inst.is_64bit) {
                u64 u = 0;
                const double d = state_.GetDouble(inst.rn);
                std::memcpy(&u, &d, sizeof(u));
                state_.SetX(inst.rd, u);
            } else {
                u32 u = 0;
                const float f = state_.GetSingle(inst.rn);
                std::memcpy(&u, &f, sizeof(u));
                state_.SetW(inst.rd, u);
            }
            break;
        }

        case Opcode::FMOV_from_gp: {
            if (inst.is_64bit) {
                double d = 0.0;
                const u64 u = state_.GetX(inst.rn);
                std::memcpy(&d, &u, sizeof(d));
                state_.SetDouble(inst.rd, d);
            } else {
                float f = 0.0f;
                const u32 u = state_.GetW(inst.rn);
                std::memcpy(&f, &u, sizeof(f));
                state_.SetSingle(inst.rd, f);
            }
            break;
        }

        case Opcode::SCVTF: {
            if (inst.is_fp_double) {
                const double d = inst.is_64bit ? static_cast<double>(static_cast<s64>(state_.GetX(inst.rn)))
                                               : static_cast<double>(static_cast<s32>(state_.GetW(inst.rn)));
                state_.SetDouble(inst.rd, d);
            } else {
                const float f = inst.is_64bit ? static_cast<float>(static_cast<s64>(state_.GetX(inst.rn)))
                                              : static_cast<float>(static_cast<s32>(state_.GetW(inst.rn)));
                state_.SetSingle(inst.rd, f);
            }
            break;
        }

        case Opcode::UCVTF: {
            if (inst.is_fp_double) {
                const double d = inst.is_64bit ? static_cast<double>(state_.GetX(inst.rn))
                                               : static_cast<double>(state_.GetW(inst.rn));
                state_.SetDouble(inst.rd, d);
            } else {
                const float f = inst.is_64bit ? static_cast<float>(state_.GetX(inst.rn))
                                              : static_cast<float>(state_.GetW(inst.rn));
                state_.SetSingle(inst.rd, f);
            }
            break;
        }

        case Opcode::FCVTZS: {
            if (inst.is_fp_double) {
                const double d = state_.GetDouble(inst.rn);
                if (inst.is_64bit) state_.SetX(inst.rd, static_cast<u64>(static_cast<s64>(d)));
                else state_.SetW(inst.rd, static_cast<u32>(static_cast<s32>(d)));
            } else {
                const float f = state_.GetSingle(inst.rn);
                if (inst.is_64bit) state_.SetX(inst.rd, static_cast<u64>(static_cast<s64>(f)));
                else state_.SetW(inst.rd, static_cast<u32>(static_cast<s32>(f)));
            }
            break;
        }

        case Opcode::FCVTZU: {
            if (inst.is_fp_double) {
                const double d = state_.GetDouble(inst.rn);
                if (inst.is_64bit) state_.SetX(inst.rd, static_cast<u64>(d));
                else state_.SetW(inst.rd, static_cast<u32>(d));
            } else {
                const float f = state_.GetSingle(inst.rn);
                if (inst.is_64bit) state_.SetX(inst.rd, static_cast<u64>(f));
                else state_.SetW(inst.rd, static_cast<u32>(f));
            }
            break;
        }

        case Opcode::FCVT: {
            if (inst.is_fp_double) {
                state_.SetDouble(inst.rd, static_cast<double>(state_.GetSingle(inst.rn)));
            } else {
                state_.SetSingle(inst.rd, static_cast<float>(state_.GetDouble(inst.rn)));
            }
            break;
        }

        // Loads and Stores FP
        case Opcode::LDR_fp_imm: {
            const vaddr_t addr = state_.GetRegOrSP(inst.rn) + inst.imm;
            if (inst.is_fp_double) {
                state_.v[inst.rd].low = memory_.Read64(addr);
                state_.v[inst.rd].high = 0;
            } else {
                state_.v[inst.rd].low = memory_.Read32(addr);
                state_.v[inst.rd].high = 0;
            }
            break;
        }

        case Opcode::STR_fp_imm: {
            const vaddr_t addr = state_.GetRegOrSP(inst.rn) + inst.imm;
            if (inst.is_fp_double) {
                memory_.Write64(addr, state_.v[inst.rd].low);
            } else {
                memory_.Write32(addr, static_cast<u32>(state_.v[inst.rd].low));
            }
            break;
        }

        case Opcode::LDP_fp: {
            const vaddr_t base = state_.GetRegOrSP(inst.rn) + inst.imm;
            if (inst.is_fp_double) {
                state_.v[inst.rd].low = memory_.Read64(base);
                state_.v[inst.rd].high = 0;
                state_.v[inst.rt2].low = memory_.Read64(base + 8);
                state_.v[inst.rt2].high = 0;
            } else {
                state_.v[inst.rd].low = memory_.Read32(base);
                state_.v[inst.rd].high = 0;
                state_.v[inst.rt2].low = memory_.Read32(base + 4);
                state_.v[inst.rt2].high = 0;
            }
            break;
        }

        case Opcode::STP_fp: {
            const vaddr_t base = state_.GetRegOrSP(inst.rn) + inst.imm;
            if (inst.is_fp_double) {
                memory_.Write64(base, state_.v[inst.rd].low);
                memory_.Write64(base + 8, state_.v[inst.rt2].low);
            } else {
                memory_.Write32(base, static_cast<u32>(state_.v[inst.rd].low));
                memory_.Write32(base + 4, static_cast<u32>(state_.v[inst.rt2].low));
            }
            break;
        }

        // Vector / NEON SIMD
        case Opcode::ADD_vec: {
            if (inst.vec_size == 2) {
                for (size_t l = 0; l < 4; ++l) {
                    state_.SetVectorLane32(inst.rd, l, state_.GetVectorLane32(inst.rn, l) + state_.GetVectorLane32(inst.rm, l));
                }
            } else {
                for (size_t l = 0; l < 2; ++l) {
                    state_.SetVectorLane64(inst.rd, l, state_.GetVectorLane64(inst.rn, l) + state_.GetVectorLane64(inst.rm, l));
                }
            }
            break;
        }

        case Opcode::SUB_vec: {
            if (inst.vec_size == 2) {
                for (size_t l = 0; l < 4; ++l) {
                    state_.SetVectorLane32(inst.rd, l, state_.GetVectorLane32(inst.rn, l) - state_.GetVectorLane32(inst.rm, l));
                }
            } else {
                for (size_t l = 0; l < 2; ++l) {
                    state_.SetVectorLane64(inst.rd, l, state_.GetVectorLane64(inst.rn, l) - state_.GetVectorLane64(inst.rm, l));
                }
            }
            break;
        }

        case Opcode::FADD_vec: {
            if (inst.is_fp_double) {
                for (size_t l = 0; l < 2; ++l) {
                    double a = 0.0, b = 0.0;
                    const u64 ua = state_.GetVectorLane64(inst.rn, l);
                    const u64 ub = state_.GetVectorLane64(inst.rm, l);
                    std::memcpy(&a, &ua, 8);
                    std::memcpy(&b, &ub, 8);
                    double res = a + b;
                    u64 ures = 0;
                    std::memcpy(&ures, &res, 8);
                    state_.SetVectorLane64(inst.rd, l, ures);
                }
            } else {
                for (size_t l = 0; l < 4; ++l) {
                    float a = 0.0f, b = 0.0f;
                    const u32 ua = state_.GetVectorLane32(inst.rn, l);
                    const u32 ub = state_.GetVectorLane32(inst.rm, l);
                    std::memcpy(&a, &ua, 4);
                    std::memcpy(&b, &ub, 4);
                    float res = a + b;
                    u32 ures = 0;
                    std::memcpy(&ures, &res, 4);
                    state_.SetVectorLane32(inst.rd, l, ures);
                }
            }
            break;
        }

        case Opcode::FSUB_vec: {
            if (inst.is_fp_double) {
                for (size_t l = 0; l < 2; ++l) {
                    double a = 0.0, b = 0.0;
                    const u64 ua = state_.GetVectorLane64(inst.rn, l);
                    const u64 ub = state_.GetVectorLane64(inst.rm, l);
                    std::memcpy(&a, &ua, 8);
                    std::memcpy(&b, &ub, 8);
                    double res = a - b;
                    u64 ures = 0;
                    std::memcpy(&ures, &res, 8);
                    state_.SetVectorLane64(inst.rd, l, ures);
                }
            } else {
                for (size_t l = 0; l < 4; ++l) {
                    float a = 0.0f, b = 0.0f;
                    const u32 ua = state_.GetVectorLane32(inst.rn, l);
                    const u32 ub = state_.GetVectorLane32(inst.rm, l);
                    std::memcpy(&a, &ua, 4);
                    std::memcpy(&b, &ub, 4);
                    float res = a - b;
                    u32 ures = 0;
                    std::memcpy(&ures, &res, 4);
                    state_.SetVectorLane32(inst.rd, l, ures);
                }
            }
            break;
        }

        case Opcode::FMUL_vec: {
            if (inst.is_fp_double) {
                for (size_t l = 0; l < 2; ++l) {
                    double a = 0.0, b = 0.0;
                    const u64 ua = state_.GetVectorLane64(inst.rn, l);
                    const u64 ub = state_.GetVectorLane64(inst.rm, l);
                    std::memcpy(&a, &ua, 8);
                    std::memcpy(&b, &ub, 8);
                    double res = a * b;
                    u64 ures = 0;
                    std::memcpy(&ures, &res, 8);
                    state_.SetVectorLane64(inst.rd, l, ures);
                }
            } else {
                for (size_t l = 0; l < 4; ++l) {
                    float a = 0.0f, b = 0.0f;
                    const u32 ua = state_.GetVectorLane32(inst.rn, l);
                    const u32 ub = state_.GetVectorLane32(inst.rm, l);
                    std::memcpy(&a, &ua, 4);
                    std::memcpy(&b, &ub, 4);
                    float res = a * b;
                    u32 ures = 0;
                    std::memcpy(&ures, &res, 4);
                    state_.SetVectorLane32(inst.rd, l, ures);
                }
            }
            break;
        }

        case Opcode::AND_vec: {
            state_.v[inst.rd].low = state_.v[inst.rn].low & state_.v[inst.rm].low;
            state_.v[inst.rd].high = state_.v[inst.rn].high & state_.v[inst.rm].high;
            break;
        }

        case Opcode::ORR_vec: {
            state_.v[inst.rd].low = state_.v[inst.rn].low | state_.v[inst.rm].low;
            state_.v[inst.rd].high = state_.v[inst.rn].high | state_.v[inst.rm].high;
            break;
        }

        case Opcode::EOR_vec: {
            state_.v[inst.rd].low = state_.v[inst.rn].low ^ state_.v[inst.rm].low;
            state_.v[inst.rd].high = state_.v[inst.rn].high ^ state_.v[inst.rm].high;
            break;
        }

        case Opcode::DUP_gen: {
            if (inst.vec_size == 3) {
                const u64 val = state_.GetX(inst.rn);
                state_.SetVectorLane64(inst.rd, 0, val);
                state_.SetVectorLane64(inst.rd, 1, val);
            } else if (inst.vec_size == 2) {
                const u32 val = state_.GetW(inst.rn);
                for (size_t l = 0; l < 4; ++l) state_.SetVectorLane32(inst.rd, l, val);
            }
            break;
        }

        case Opcode::INS_gen: {
            if (inst.vec_size == 3) {
                state_.SetVectorLane64(inst.rd, inst.vec_index, state_.GetX(inst.rn));
            } else if (inst.vec_size == 2) {
                state_.SetVectorLane32(inst.rd, inst.vec_index, state_.GetW(inst.rn));
            }
            break;
        }

        case Opcode::UMOV: {
            if (inst.vec_size == 3) {
                state_.SetX(inst.rd, state_.GetVectorLane64(inst.rn, inst.vec_index));
            } else if (inst.vec_size == 2) {
                state_.SetW(inst.rd, state_.GetVectorLane32(inst.rn, inst.vec_index));
            }
            break;
        }

        case Opcode::SMOV: {
            // SMOV sign-extends the selected element up to 64 bits and writes Xd.
            if (inst.vec_size == 3) {
                const s64 v = static_cast<s64>(state_.GetVectorLane64(inst.rn, inst.vec_index));
                state_.SetX(inst.rd, static_cast<u64>(v));
            } else {
                u64 v = 0;
                unsigned bits = 0;
                if (inst.vec_size == 0) { v = state_.GetVectorLane8(inst.rn, inst.vec_index); bits = 8; }
                else if (inst.vec_size == 1) { v = state_.GetVectorLane16(inst.rn, inst.vec_index); bits = 16; }
                else { v = static_cast<u64>(state_.GetVectorLane32(inst.rn, inst.vec_index)); bits = 32; }
                const u64 sign = (v >> (bits - 1)) & 1;
                const u64 ext = sign ? (~u64{0} << bits) : 0;
                state_.SetX(inst.rd, v | ext);
            }
            break;
        }

        // Atomics & Exclusives
        case Opcode::LDXR: {
            const vaddr_t addr = state_.GetRegOrSP(inst.rn);
            state_.exclusive_addr = addr;
            state_.exclusive_active = true;
            if (inst.is_64bit) {
                state_.SetX(inst.rd, memory_.Read64(addr));
            } else {
                state_.SetW(inst.rd, memory_.Read32(addr));
            }
            break;
        }

        case Opcode::STXR: {
            const vaddr_t addr = state_.GetRegOrSP(inst.rn);
            if (state_.exclusive_active && state_.exclusive_addr == addr) {
                if (inst.is_64bit) {
                    memory_.Write64(addr, state_.GetX(inst.rd));
                } else {
                    memory_.Write32(addr, state_.GetW(inst.rd));
                }
                state_.SetW(inst.rs, 0); // 0 = Success
                state_.exclusive_active = false;
            } else {
                state_.SetW(inst.rs, 1); // 1 = Failure
            }
            break;
        }

        case Opcode::LDADD: {
            const vaddr_t addr = state_.GetRegOrSP(inst.rn);
            if (inst.is_64bit) {
                const u64 old_val = memory_.Read64(addr);
                const u64 add_val = state_.GetX(inst.rs);
                memory_.Write64(addr, old_val + add_val);
                state_.SetX(inst.rd, old_val);
            } else {
                const u32 old_val = memory_.Read32(addr);
                const u32 add_val = state_.GetW(inst.rs);
                memory_.Write32(addr, old_val + add_val);
                state_.SetW(inst.rd, old_val);
            }
            break;
        }

        case Opcode::CAS: {
            const vaddr_t addr = state_.GetRegOrSP(inst.rn);
            if (inst.is_64bit) {
                const u64 cur = memory_.Read64(addr);
                const u64 cmp = state_.GetX(inst.rs);
                if (cur == cmp) {
                    memory_.Write64(addr, state_.GetX(inst.rd));
                }
                state_.SetX(inst.rs, cur);
            } else {
                const u32 cur = memory_.Read32(addr);
                const u32 cmp = state_.GetW(inst.rs);
                if (cur == cmp) {
                    memory_.Write32(addr, state_.GetW(inst.rd));
                }
                state_.SetW(inst.rs, cur);
            }
            break;
        }

        case Opcode::SWP: {
            const vaddr_t addr = state_.GetRegOrSP(inst.rn);
            if (inst.is_64bit) {
                const u64 cur = memory_.Read64(addr);
                memory_.Write64(addr, state_.GetX(inst.rs));
                state_.SetX(inst.rd, cur);
            } else {
                const u32 cur = memory_.Read32(addr);
                memory_.Write32(addr, state_.GetW(inst.rs));
                state_.SetW(inst.rd, cur);
            }
            break;
        }

        case Opcode::CLREX: {
            state_.exclusive_active = false;
            state_.exclusive_addr = 0;
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
