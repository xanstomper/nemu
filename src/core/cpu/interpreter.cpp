#include "interpreter.hpp"
#include "platform/logger.hpp"
#include <cmath>
#include <cstring>
#include <atomic>
#include <cstdlib>

namespace nemu::core::cpu {

Interpreter::Interpreter(CpuState& state, memory::IMemory& memory)
    : state_(state), memory_(&memory) {}

Interpreter::Interpreter(CpuState& state, memory::IMemory* memory)
    : state_(state), memory_(memory) {}

u64 Interpreter::ApplyExtend(u64 value, u8 extend_op, u8 amount) {
    // ARM ARM "extended register" operand: narrow/sign-extend the source
    // register, then shift left by the 3-bit imm3. This is what powers the
    // extremely common `add x, x, w, uxtw #N` / `uxtb` / `uxth` /
    // `sxtw` / bare `lsl #N` idioms that compilers emit everywhere.
    u64 ext = 0;
    switch (extend_op & 0x7) {
        case 0: ext = static_cast<u64>(static_cast<u8>(value));  break;  // UXTB
        case 1: ext = static_cast<u64>(static_cast<u16>(value)); break;  // UXTH
        case 2: ext = static_cast<u64>(static_cast<u32>(value)); break;  // UXTW
        case 3: ext = value;                                        break;  // UXTX
        case 4: ext = static_cast<u64>(static_cast<s64>(static_cast<s8>(value)));   break; // SXTB
        case 5: ext = static_cast<u64>(static_cast<s64>(static_cast<s16>(value)));  break; // SXTH
        case 6: ext = static_cast<u64>(static_cast<s64>(static_cast<s32>(value)));  break; // SXTW
        default: ext = value;                                       break;  // SXTX
    }
    return amount >= 64 ? 0 : (ext << amount);
}

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

    // Optional first-instructions trace for boot debugging:
    //   NEMU_TRACE_N=200000 ./build/bin/Nemu --run game.xci
    // logs "TRACE pc=... raw=..." for the first N steps of every thread
    // (cheap atomic counter, disabled when unset).
    static const long long trace_limit = [] {
        const char* e = std::getenv("NEMU_TRACE_N");
        return e ? std::atoll(e) : 0LL;
    }();
    static std::atomic<long long> trace_count{0};
    if (trace_limit > 0 && trace_count.fetch_add(1, std::memory_order_relaxed) < trace_limit) {
        const u32 raw = memory_ ? memory_->Read32(state_.pc) : 0;
        NEMU_LOG_INFO("TRACE", "pc=0x{:016X} raw=0x{:08X} sp=0x{:016X} x0=0x{:016X} x8=0x{:016X} x16=0x{:016X} x30=0x{:016X}",
                      state_.pc, raw, state_.sp, state_.GetX(0), state_.GetX(8), state_.GetX(16), state_.GetX(30));
    }

    if (!memory_ || !memory_->IsValidAddress(state_.pc, 4)) {
        NEMU_LOG_ERROR("CPU", "PC 0x{:016X} is not valid memory", state_.pc);
        return StepResult::MemoryFault;
    }

    const u32 raw_inst = memory_->Read32(state_.pc);
    const DecodedInstruction inst = Decoder::Decode(raw_inst);

    if (inst.opcode == Opcode::UNDEFINED) {
        // Throttle identically to memory faults: a guest re-executing one
        // undecodable word (a zeroed page, an unresolved jump target) produced
        // millions of identical log lines per probe run.
        constexpr u64 kUndefLogHeadroom = 8;
        constexpr u64 kUndefLogInterval = 100000;
        static std::atomic<u64> undef_count{0};
        const u64 total = undef_count.fetch_add(1, std::memory_order_relaxed) + 1;
        if (total <= kUndefLogHeadroom) {
            NEMU_LOG_ERROR("CPU", "Undefined instruction 0x{:08X} at PC 0x{:016X} (#{})",
                           raw_inst, state_.pc, total);
        } else if (total == kUndefLogHeadroom + 1) {
            NEMU_LOG_ERROR("CPU",
                           "Guest repeatedly executes undefined opcodes (throttling logs, now "
                           "every {}th). Same instruction was 0x{:08X} at PC 0x{:016X}.",
                           kUndefLogInterval, raw_inst, state_.pc);
        } else if (total % kUndefLogInterval == 0) {
            NEMU_LOG_ERROR("CPU", "Undefined instruction #{} at PC 0x{:016X}", total, state_.pc);
        }
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
            const u64 rm_raw = inst.is_64bit ? state_.GetX(inst.rm) : state_.GetW(inst.rm);
            const u64 rm = (inst.extend_op != 0xFF)
                ? ApplyExtend(rm_raw, inst.extend_op, inst.shift_amount)
                : ApplyShift(rm_raw, inst.shift_type, inst.shift_amount, inst.is_64bit);
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
            const u64 rm_raw = inst.is_64bit ? state_.GetX(inst.rm) : state_.GetW(inst.rm);
            const u64 rm = (inst.extend_op != 0xFF)
                ? ApplyExtend(rm_raw, inst.extend_op, inst.shift_amount)
                : ApplyShift(rm_raw, inst.shift_type, inst.shift_amount, inst.is_64bit);
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
            const u64 rm_raw = inst.is_64bit ? state_.GetX(inst.rm) : state_.GetW(inst.rm);
            const u64 rm = (inst.extend_op != 0xFF)
                ? ApplyExtend(rm_raw, inst.extend_op, inst.shift_amount)
                : ApplyShift(rm_raw, inst.shift_type, inst.shift_amount, inst.is_64bit);
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
            const u64 rm_raw = inst.is_64bit ? state_.GetX(inst.rm) : state_.GetW(inst.rm);
            const u64 rm = (inst.extend_op != 0xFF)
                ? ApplyExtend(rm_raw, inst.extend_op, inst.shift_amount)
                : ApplyShift(rm_raw, inst.shift_type, inst.shift_amount, inst.is_64bit);
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
            const u64 rm_raw = inst.is_64bit ? state_.GetX(inst.rm) : state_.GetW(inst.rm);
            const u64 rm = (inst.extend_op != 0xFF)
                ? ApplyExtend(rm_raw, inst.extend_op, inst.shift_amount)
                : ApplyShift(rm_raw, inst.shift_type, inst.shift_amount, inst.is_64bit);
            if (inst.is_64bit) {
                state_.SetX(inst.rd, state_.GetX(inst.rn) & rm);
            } else {
                state_.SetW(inst.rd, state_.GetW(inst.rn) & static_cast<u32>(rm));
            }
            break;
        }

        case Opcode::ANDS_reg: {
            const u64 rm_raw = inst.is_64bit ? state_.GetX(inst.rm) : state_.GetW(inst.rm);
            const u64 rm = (inst.extend_op != 0xFF)
                ? ApplyExtend(rm_raw, inst.extend_op, inst.shift_amount)
                : ApplyShift(rm_raw, inst.shift_type, inst.shift_amount, inst.is_64bit);
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
            const u64 rm_raw = inst.is_64bit ? state_.GetX(inst.rm) : state_.GetW(inst.rm);
            const u64 rm = (inst.extend_op != 0xFF)
                ? ApplyExtend(rm_raw, inst.extend_op, inst.shift_amount)
                : ApplyShift(rm_raw, inst.shift_type, inst.shift_amount, inst.is_64bit);
            if (inst.is_64bit) {
                state_.SetX(inst.rd, state_.GetX(inst.rn) | rm);
            } else {
                state_.SetW(inst.rd, state_.GetW(inst.rn) | static_cast<u32>(rm));
            }
            break;
        }

        case Opcode::ORN_reg: {
            const u64 rm_raw = inst.is_64bit ? state_.GetX(inst.rm) : state_.GetW(inst.rm);
            const u64 rm = (inst.extend_op != 0xFF)
                ? ApplyExtend(rm_raw, inst.extend_op, inst.shift_amount)
                : ApplyShift(rm_raw, inst.shift_type, inst.shift_amount, inst.is_64bit);
            if (inst.is_64bit) {
                state_.SetX(inst.rd, state_.GetX(inst.rn) | ~rm);
            } else {
                state_.SetW(inst.rd, state_.GetW(inst.rn) | ~static_cast<u32>(rm));
            }
            break;
        }

        case Opcode::EOR_reg: {
            const u64 rm_raw = inst.is_64bit ? state_.GetX(inst.rm) : state_.GetW(inst.rm);
            const u64 rm = (inst.extend_op != 0xFF)
                ? ApplyExtend(rm_raw, inst.extend_op, inst.shift_amount)
                : ApplyShift(rm_raw, inst.shift_type, inst.shift_amount, inst.is_64bit);
            if (inst.is_64bit) {
                state_.SetX(inst.rd, state_.GetX(inst.rn) ^ rm);
            } else {
                state_.SetW(inst.rd, state_.GetW(inst.rn) ^ static_cast<u32>(rm));
            }
            break;
        }

        // Logical (immediate): AND / ORR / EOR / ANDS with a decoded mask in inst.imm.
        case Opcode::AND_imm: {
            if (inst.is_64bit) {
                state_.SetX(inst.rd, state_.GetX(inst.rn) & inst.imm);
            } else {
                state_.SetW(inst.rd, state_.GetW(inst.rn) & static_cast<u32>(inst.imm));
            }
            break;
        }
        case Opcode::ORR_imm: {
            if (inst.is_64bit) {
                state_.SetX(inst.rd, state_.GetX(inst.rn) | inst.imm);
            } else {
                state_.SetW(inst.rd, state_.GetW(inst.rn) | static_cast<u32>(inst.imm));
            }
            break;
        }
        case Opcode::EOR_imm: {
            if (inst.is_64bit) {
                state_.SetX(inst.rd, state_.GetX(inst.rn) ^ inst.imm);
            } else {
                state_.SetW(inst.rd, state_.GetW(inst.rn) ^ static_cast<u32>(inst.imm));
            }
            break;
        }
        case Opcode::ANDS_imm: {
            if (inst.is_64bit) {
                const u64 res = state_.GetX(inst.rn) & inst.imm;
                state_.SetNZ_Logical64(res);
                state_.SetX(inst.rd, res);
            } else {
                const u32 res = state_.GetW(inst.rn) & static_cast<u32>(inst.imm);
                state_.SetNZ_Logical32(res);
                state_.SetW(inst.rd, res);
            }
            break;
        }

        case Opcode::BIC_reg: {
            const u64 rm_raw = inst.is_64bit ? state_.GetX(inst.rm) : state_.GetW(inst.rm);
            const u64 rm = (inst.extend_op != 0xFF)
                ? ApplyExtend(rm_raw, inst.extend_op, inst.shift_amount)
                : ApplyShift(rm_raw, inst.shift_type, inst.shift_amount, inst.is_64bit);
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

        case Opcode::SMULL:
        case Opcode::UMULL: {
            // 32-bit multiply long: Rd = sign/zero-extend(Rn) * Rm (64-bit result).
            if (inst.opcode == Opcode::SMULL) {
                const s64 a = static_cast<s32>(state_.GetW(inst.rn));
                const s64 b = static_cast<s32>(state_.GetW(inst.rm));
                state_.SetX(inst.rd, static_cast<u64>(a * b));
            } else {
                const u64 a = state_.GetW(inst.rn);
                const u64 b = state_.GetW(inst.rm);
                state_.SetX(inst.rd, a * b);
            }
            break;
        }

        case Opcode::UDIV:
        case Opcode::SDIV: {
            // A64: division by zero yields 0 (no trap).
            if (inst.opcode == Opcode::SDIV) {
                const s64 a = inst.is_64bit ? static_cast<s64>(state_.GetX(inst.rn))
                                            : static_cast<s32>(state_.GetW(inst.rn));
                const s64 b = inst.is_64bit ? static_cast<s64>(state_.GetX(inst.rm))
                                            : static_cast<s32>(state_.GetW(inst.rm));
                const s64 q = (b == 0) ? 0 : (a / b);
                if (inst.is_64bit) state_.SetX(inst.rd, static_cast<u64>(q));
                else state_.SetW(inst.rd, static_cast<u32>(q));
            } else {
                const u64 a = inst.is_64bit ? state_.GetX(inst.rn) : state_.GetW(inst.rn);
                const u64 b = inst.is_64bit ? state_.GetX(inst.rm) : state_.GetW(inst.rm);
                const u64 q = (b == 0) ? 0 : (a / b);
                if (inst.is_64bit) state_.SetX(inst.rd, q);
                else state_.SetW(inst.rd, static_cast<u32>(q));
            }
            break;
        }

        case Opcode::SBFM:
        case Opcode::UBFM: {
            // A64 unified bitfield op (verified encodings via GNU as):
            //   ubfm w1,w2,#13,#0 -> 0x530D0041 => immr=bits[15:10], imms=bits[5:0]
            // Rd = f(Rn ROR immr) with mask from DecodeBitMasks; the game-relevant
            // forms (UBFX/BFI/SXTx/LSL-imm/LSR-imm/ASR-imm) all produce a
            // contiguous field, so the mask is the imms..immr span.
            const u32 immr = inst.shift_amount;
            const u32 imms = static_cast<u32>(inst.imm);
            const u32 ds = inst.is_64bit ? 64 : 32;
            const u64 srcval = inst.is_64bit ? state_.GetX(inst.rn) : state_.GetW(inst.rn);

            const u64 ones = ~0ULL;
            const u64 ror = (immr == 0) ? srcval
                          : ((srcval >> immr) | (srcval << (ds - immr)));
            const u64 ror32 = (ds == 32) ? (ror & 0xFFFFFFFFULL) : ror;

            const u32 nbits = (imms >= immr) ? (imms - immr + 1)
                                             : (ds - immr + imms + 1);
            const u64 mask = (nbits >= 64) ? ones
                          : (((1ULL << nbits) - 1) & ((ds == 32) ? 0xFFFFFFFFULL : ones));

            u64 result;
            if (inst.opcode == Opcode::UBFM) {
                result = ror32 & mask;
            } else { // SBFM: extract field; sign-extend from its top bit
                const u64 field = ror32 & mask;
                const bool neg = nbits > 0 && ((field >> (nbits - 1)) & 1ULL);
                const u64 sext = (ds == 64) ? ones : 0xFFFFFFFFULL;
                result = neg ? (field | (sext & ~mask)) : field;
            }
            if (inst.is_64bit) state_.SetX(inst.rd, result);
            else state_.SetW(inst.rd, static_cast<u32>(result));
            break;
        }

        case Opcode::BFM: {
            // BFM / BFI / BFXIL: dst = (dst & ~mask) | (ROR(src, immr) & mask)
            // Same rotate+mask as SBFM/UBFM, but preserves dst bits outside the field.
            const u32 immr = inst.shift_amount;
            const u32 imms = static_cast<u32>(inst.imm);
            const u32 ds = inst.is_64bit ? 64 : 32;
            const u64 srcval = inst.is_64bit ? state_.GetX(inst.rn) : state_.GetW(inst.rn);
            const u64 dstval = inst.is_64bit ? state_.GetX(inst.rd) : state_.GetW(inst.rd);

            const u64 ror = (immr == 0) ? srcval
                          : ((srcval >> immr) | (srcval << (ds - immr)));
            const u64 ror32 = (ds == 32) ? (ror & 0xFFFFFFFFULL) : ror;
            const u32 nbits = (imms >= immr) ? (imms - immr + 1)
                                             : (ds - immr + imms + 1);
            const u64 mask = (nbits >= 64) ? ~0ULL
                          : (((1ULL << nbits) - 1) & ((ds == 32) ? 0xFFFFFFFFULL : ~0ULL));
            const u64 result = (dstval & ~mask) | (ror32 & mask);
            if (inst.is_64bit) state_.SetX(inst.rd, result);
            else state_.SetW(inst.rd, static_cast<u32>(result));
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

        case Opcode::BR: {
            const vaddr_t target = state_.GetX(inst.rn);
            next_pc = target;
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
            const u64 base = state_.GetRegOrSP(inst.rn);
            // PostIndexed reads from base, then writes back base+imm.
            const vaddr_t addr = (inst.addr_mode == AddressingMode::PostIndexed)
                ? base : base + inst.imm;
            if (inst.is_64bit) {
                state_.SetX(inst.rd, memory_->Read64(addr));
            } else {
                state_.SetW(inst.rd, memory_->Read32(addr));
            }
            // Pre/post-index write back the base register (unless it aliases Rd).
            if (inst.addr_mode == AddressingMode::PreIndexed ||
                inst.addr_mode == AddressingMode::PostIndexed) {
                if (inst.rn != inst.rd) state_.SetRegOrSP(inst.rn, base + inst.imm);
            }
            break;
        }

        case Opcode::STR_imm: {
            const u64 base = state_.GetRegOrSP(inst.rn);
            const vaddr_t addr = (inst.addr_mode == AddressingMode::PostIndexed)
                ? base : base + inst.imm;
            if (inst.is_64bit) {
                memory_->Write64(addr, state_.GetX(inst.rd));
            } else {
                memory_->Write32(addr, state_.GetW(inst.rd));
            }
            if (inst.addr_mode == AddressingMode::PreIndexed ||
                inst.addr_mode == AddressingMode::PostIndexed) {
                state_.SetRegOrSP(inst.rn, base + inst.imm);
            }
            break;
        }

        case Opcode::LDRB_imm: {
            const vaddr_t addr = state_.GetRegOrSP(inst.rn) + inst.imm;
            state_.SetW(inst.rd, memory_->Read8(addr));
            break;
        }

        case Opcode::STRB_imm: {
            const vaddr_t addr = state_.GetRegOrSP(inst.rn) + inst.imm;
            memory_->Write8(addr, static_cast<u8>(state_.GetW(inst.rd)));
            break;
        }

        case Opcode::LDRH_imm: {
            const vaddr_t addr = state_.GetRegOrSP(inst.rn) + inst.imm;
            state_.SetW(inst.rd, memory_->Read16(addr));
            break;
        }

        case Opcode::STRH_imm: {
            const vaddr_t addr = state_.GetRegOrSP(inst.rn) + inst.imm;
            memory_->Write16(addr, static_cast<u16>(state_.GetW(inst.rd)));
            break;
        }

        // ---- SIMD structure load/store multiple (LD1/ST1/LD2/ST2/LD3/ST3/LD4/ST4)
        // Contiguous forms move whole registers byte-exact; interleaved (LD2-4)
        // de-interleave nregs-wide element groups across registers.
        // Decode contract: vec_size=nregs, vec_index=Q (8B/16B per reg).
        case Opcode::LD1_vec:
        case Opcode::ST1_vec:
        case Opcode::LD1x4_vec:
        case Opcode::ST1x4_vec:
        case Opcode::LD2_vec:
        case Opcode::ST2_vec:
        case Opcode::LD3_vec:
        case Opcode::ST3_vec:
        case Opcode::LD4_vec:
        case Opcode::ST4_vec: {
            const u32 nregs = inst.vec_size;
            const u32 reg_bytes = (inst.vec_index != 0) ? 16u : 8u; // Q
            const u32 total = nregs * reg_bytes;
            const vaddr_t addr = state_.GetRegOrSP(inst.rn);
            const bool is_load = inst.opcode == Opcode::LD1_vec || inst.opcode == Opcode::LD1x4_vec
                              || inst.opcode == Opcode::LD2_vec || inst.opcode == Opcode::LD3_vec
                              || inst.opcode == Opcode::LD4_vec;
            const bool contiguous = inst.opcode == Opcode::LD1_vec
                || inst.opcode == Opcode::ST1_vec
                || inst.opcode == Opcode::LD1x4_vec || inst.opcode == Opcode::ST1x4_vec;
            const bool post = inst.addr_mode == AddressingMode::PostIndexed;

            if (contiguous) {
                // Whole-register contiguous move (LD1/ST1) — byte exact.
                if (is_load) {
                    for (u32 r = 0; r < nregs; ++r) {
                        u128 val{};
                        memory_->ReadBlock(addr + r * reg_bytes, &val, reg_bytes);
                        state_.SetVector(static_cast<u32>((inst.rd + r) & 31), val);
                    }
                } else {
                    for (u32 r = 0; r < nregs; ++r) {
                        const u128 val = state_.GetVector(static_cast<u32>((inst.rd + r) & 31));
                        memory_->WriteBlock(addr + r * reg_bytes, &val, reg_bytes);
                    }
                }
            } else {
                // Interleaved (LD2/ST2/LD3/LD4): elements alternate across the n
                // registers. esz = 1 << size_field (decoded into bit_pos).
                // Ground truth: LD4 {v0.4s}: reg_bytes=16, esz=4 → 4 elems/reg;
                //               LD2 {v0.8h}: reg_bytes=16, esz=2 → 8 elems/reg.
                const u32 esz = 1u << inst.bit_pos;
                const u32 elems = reg_bytes / esz;
                std::vector<u8> buf(total);
                if (is_load) {
                    memory_->ReadBlock(addr, buf.data(), buf.size());
                    for (u32 e = 0; e < elems; ++e) {
                        for (u32 r = 0; r < nregs; ++r) {
                            const size_t off = (e * nregs + r) * esz;
                            u128 val = state_.GetVector(static_cast<u32>((inst.rd + r) & 31));
                            // element e lives at byte offset e*esz within the register
                            std::memcpy(reinterpret_cast<u8*>(&val) + e * esz, buf.data() + off, esz);
                            state_.SetVector(static_cast<u32>((inst.rd + r) & 31), val);
                        }
                    }
                } else {
                    for (u32 e = 0; e < elems; ++e) {
                        for (u32 r = 0; r < nregs; ++r) {
                            const u128 val = state_.GetVector(static_cast<u32>((inst.rd + r) & 31));
                            const size_t off = (e * nregs + r) * esz;
                            std::memcpy(buf.data() + off,
                                        reinterpret_cast<const u8*>(&val) + e * esz, esz);
                        }
                    }
                    memory_->WriteBlock(addr, buf.data(), buf.size());
                }
            }

            if (post) {
                const vaddr_t base = state_.GetRegOrSP(inst.rn) + total;
                state_.SetX(inst.rn, base);
            }
            break;
        }

        case Opcode::LDP: {
            // Pre-index: write back the base BEFORE the access, then access at the
            // updated base. Post-index: access at the original base, then write
            // back. Plain offset: no writeback.
            const u64 orig = state_.GetRegOrSP(inst.rn);
            if (inst.addr_mode == AddressingMode::PreIndexed &&
                inst.rn != inst.rd && inst.rn != inst.rt2) {
                state_.SetRegOrSP(inst.rn, orig + inst.imm);
            }
            const vaddr_t base = (inst.addr_mode == AddressingMode::PostIndexed)
                ? orig : orig + inst.imm;
            if (inst.is_64bit) {
                state_.SetX(inst.rd, memory_->Read64(base));
                state_.SetX(inst.rt2, memory_->Read64(base + 8));
            } else {
                state_.SetW(inst.rd, memory_->Read32(base));
                state_.SetW(inst.rt2, memory_->Read32(base + 4));
            }
            if (inst.addr_mode == AddressingMode::PostIndexed) {
                state_.SetRegOrSP(inst.rn, orig + inst.imm);
            }
            break;
        }

        case Opcode::STP: {
            const u64 orig = state_.GetRegOrSP(inst.rn);
            if (inst.addr_mode == AddressingMode::PreIndexed) {
                state_.SetRegOrSP(inst.rn, orig + inst.imm);
            }
            const vaddr_t base = (inst.addr_mode == AddressingMode::PostIndexed)
                ? orig : orig + inst.imm;
            if (inst.is_64bit) {
                memory_->Write64(base, state_.GetX(inst.rd));
                memory_->Write64(base + 8, state_.GetX(inst.rt2));
            } else {
                memory_->Write32(base, state_.GetW(inst.rd));
                memory_->Write32(base + 4, state_.GetW(inst.rt2));
            }
            if (inst.addr_mode == AddressingMode::PostIndexed) {
                state_.SetRegOrSP(inst.rn, orig + inst.imm);
            }
            break;
        }

        // Load/store with a register offset: [Xn + Xm (LSL shift)].
        // Terraria's main uses the 64/32-bit forms ~66,000 times and the
        // sign-extending 8/16-bit forms ~16,000 times; all of them used to fall
        // through to "Unhandled opcode".
        case Opcode::LDR_reg: {
            const u64 addr = state_.GetRegOrSP(inst.rn) + state_.GetX(inst.rm);
            if (inst.is_64bit) {
                state_.SetX(inst.rd, memory_->Read64(addr));
            } else {
                state_.SetW(inst.rd, memory_->Read32(addr));
            }
            break;
        }

        case Opcode::STR_reg: {
            const u64 addr = state_.GetRegOrSP(inst.rn) + state_.GetX(inst.rm);
            if (inst.is_64bit) {
                memory_->Write64(addr, state_.GetX(inst.rd));
            } else {
                memory_->Write32(addr, state_.GetW(inst.rd));
            }
            break;
        }

        case Opcode::LDRSB_imm: {
            const vaddr_t addr = state_.GetRegOrSP(inst.rn) + inst.imm;
            state_.SetW(inst.rd, static_cast<u32>(
                static_cast<s64>(static_cast<s8>(memory_->Read8(addr)))));
            break;
        }

        case Opcode::LDRSH_imm: {
            const vaddr_t addr = state_.GetRegOrSP(inst.rn) + inst.imm;
            state_.SetW(inst.rd, static_cast<u32>(
                static_cast<s64>(static_cast<s16>(memory_->Read16(addr)))));
            break;
        }

        case Opcode::LDRSW_imm: {
            const vaddr_t addr = state_.GetRegOrSP(inst.rn) + inst.imm;
            state_.SetX(inst.rd, static_cast<u64>(
                static_cast<s64>(static_cast<s32>(memory_->Read32(addr)))));
            break;
        }

        case Opcode::RORV: {
            const u64 amount = state_.GetX(inst.rm) & (inst.is_64bit ? 63 : 31);
            if (inst.is_64bit) {
                state_.SetX(inst.rd, std::rotr(state_.GetX(inst.rn), amount));
            } else {
                state_.SetW(inst.rd, std::rotr(state_.GetW(inst.rn),
                                                static_cast<u32>(amount)));
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

        // Conditional select increment/invert/negate.
        // CSINC: Rd = cond ? Rn : Rm+1 ; CSINV: Rd = cond ? Rn : ~Rm ;
        // CSNEG: Rd = cond ? Rn : -Rm
        case Opcode::CSINC:
        case Opcode::CSINV:
        case Opcode::CSNEG: {
            u64 sel = 0;
            if (inst.is_64bit) {
                sel = state_.GetX(inst.rm);
            } else {
                sel = static_cast<u32>(state_.GetW(inst.rm));
            }
            if (inst.opcode == Opcode::CSINC) ++sel;
            else if (inst.opcode == Opcode::CSINV) sel = ~sel;
            else if (inst.opcode == Opcode::CSNEG) sel = ~sel + 1;

            if (state_.CheckCondition(inst.condition)) {
                if (inst.is_64bit) state_.SetX(inst.rd, state_.GetX(inst.rn));
                else state_.SetW(inst.rd, state_.GetW(inst.rn));
            } else {
                if (inst.is_64bit) state_.SetX(inst.rd, sel);
                else state_.SetW(inst.rd, static_cast<u32>(sel));
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

        case Opcode::FMADD_scalar:
        case Opcode::FMSUB_scalar: {
            // Rd = Ra ± (Rn * Rm) — fused; the workhorse of game matrix/physics math.
            if (inst.is_fp_double) {
                const double prod = state_.GetDouble(inst.rn) * state_.GetDouble(inst.rm);
                if (inst.opcode == Opcode::FMADD_scalar) {
                    state_.SetDouble(inst.rd, state_.GetDouble(inst.ra) + prod);
                } else {
                    state_.SetDouble(inst.rd, state_.GetDouble(inst.ra) - prod);
                }
            } else {
                const float prod = state_.GetSingle(inst.rn) * state_.GetSingle(inst.rm);
                if (inst.opcode == Opcode::FMADD_scalar) {
                    state_.SetSingle(inst.rd, state_.GetSingle(inst.ra) + prod);
                } else {
                    state_.SetSingle(inst.rd, state_.GetSingle(inst.ra) - prod);
                }
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
                state_.v[inst.rd].low = memory_->Read64(addr);
                state_.v[inst.rd].high = 0;
            } else {
                state_.v[inst.rd].low = memory_->Read32(addr);
                state_.v[inst.rd].high = 0;
            }
            break;
        }

        case Opcode::STR_fp_imm: {
            const vaddr_t addr = state_.GetRegOrSP(inst.rn) + inst.imm;
            if (inst.is_fp_double) {
                memory_->Write64(addr, state_.v[inst.rd].low);
            } else {
                memory_->Write32(addr, static_cast<u32>(state_.v[inst.rd].low));
            }
            break;
        }

        case Opcode::LDP_fp: {
            const u64 orig = state_.GetRegOrSP(inst.rn);
            if (inst.addr_mode == AddressingMode::PreIndexed) {
                state_.SetRegOrSP(inst.rn, orig + inst.imm);
            }
            const vaddr_t base = (inst.addr_mode == AddressingMode::PostIndexed)
                ? orig : orig + inst.imm;
            if (inst.is_fp_double) {
                state_.v[inst.rd].low = memory_->Read64(base);
                state_.v[inst.rd].high = 0;
                state_.v[inst.rt2].low = memory_->Read64(base + 8);
                state_.v[inst.rt2].high = 0;
            } else {
                state_.v[inst.rd].low = memory_->Read32(base);
                state_.v[inst.rd].high = 0;
                state_.v[inst.rt2].low = memory_->Read32(base + 4);
                state_.v[inst.rt2].high = 0;
            }
            if (inst.addr_mode == AddressingMode::PostIndexed) {
                state_.SetRegOrSP(inst.rn, orig + inst.imm);
            }
            break;
        }

        case Opcode::STP_fp: {
            const u64 orig = state_.GetRegOrSP(inst.rn);
            if (inst.addr_mode == AddressingMode::PreIndexed) {
                state_.SetRegOrSP(inst.rn, orig + inst.imm);
            }
            const vaddr_t base = (inst.addr_mode == AddressingMode::PostIndexed)
                ? orig : orig + inst.imm;
            if (inst.is_fp_double) {
                memory_->Write64(base, state_.v[inst.rd].low);
                memory_->Write64(base + 8, state_.v[inst.rt2].low);
            } else {
                memory_->Write32(base, static_cast<u32>(state_.v[inst.rd].low));
                memory_->Write32(base + 4, static_cast<u32>(state_.v[inst.rt2].low));
            }
            if (inst.addr_mode == AddressingMode::PostIndexed) {
                state_.SetRegOrSP(inst.rn, orig + inst.imm);
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

        case Opcode::FCVTZS_vec:
        case Opcode::FCVTZU_vec:
        case Opcode::SCVTF_vec:
        case Opcode::UCVTF_vec: {
            for (size_t l = 0; l < 4; ++l) {
                u32 res;
                switch (inst.opcode) {
                case Opcode::FCVTZS_vec: {
                    u32 u = state_.GetVectorLane32(inst.rn, l); float f; std::memcpy(&f, &u, 4);
                    res = static_cast<u32>(static_cast<s32>(f)); // trunc toward zero
                    break;
                }
                case Opcode::FCVTZU_vec: {
                    u32 u = state_.GetVectorLane32(inst.rn, l); float f; std::memcpy(&f, &u, 4);
                    res = (f <= 0.0f) ? 0u : static_cast<u32>(f);
                    break;
                }
                case Opcode::SCVTF_vec: {
                    s32 v = static_cast<s32>(state_.GetVectorLane32(inst.rn, l));
                    float f = static_cast<float>(v); res = 0; std::memcpy(&res, &f, 4);
                    break;
                }
                default: { // UCVTF
                    u32 v = state_.GetVectorLane32(inst.rn, l);
                    float f = static_cast<float>(v); res = 0; std::memcpy(&res, &f, 4);
                    break;
                }
                }
                state_.SetVectorLane32(inst.rd, l, res);
            }
            break;
        }

        case Opcode::FMLA_vec:
        case Opcode::FMLS_vec:
        case Opcode::FABD_vec:
        case Opcode::FDIV_vec:
        case Opcode::FMAX_vec:
        case Opcode::FMIN_vec: {
            // FP vector ops, 32-bit lanes (the form games use for vertex/particle math).
            for (size_t l = 0; l < 4; ++l) {
                float a = 0.0f, b = 0.0f;
                const u32 ua = state_.GetVectorLane32(inst.rn, l);
                const u32 ub = state_.GetVectorLane32(inst.rm, l);
                std::memcpy(&a, &ua, 4);
                std::memcpy(&b, &ub, 4);
                float res;
                switch (inst.opcode) {
                case Opcode::FMLA_vec: { // accumulate into rd
                    float acc = 0.0f;
                    const u32 uacc = state_.GetVectorLane32(inst.rd, l);
                    std::memcpy(&acc, &uacc, 4);
                    res = std::fma(a, b, acc);
                    break;
                }
                case Opcode::FMLS_vec: {
                    float acc = 0.0f;
                    const u32 uacc = state_.GetVectorLane32(inst.rd, l);
                    std::memcpy(&acc, &uacc, 4);
                    res = std::fma(a, -b, acc);
                    break;
                }
                case Opcode::FABD_vec:  res = std::fabs(a - b); break;
                case Opcode::FDIV_vec:  res = a / b; break;
                case Opcode::FMAX_vec:  res = std::max(a, b); break;
                default:                res = std::min(a, b); break; // FMIN
                }
                u32 ures = 0;
                std::memcpy(&ures, &res, 4);
                state_.SetVectorLane32(inst.rd, l, ures);
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

        // ---- 3-same comparisons / min-max / multiply (32-bit lanes) ----
        case Opcode::CMGT_vec: {  // signed >
            for (u32 l = 0; l < 4; ++l) {
                const s32 a = static_cast<s32>(state_.GetVectorLane32(inst.rn, l));
                const s32 b = static_cast<s32>(state_.GetVectorLane32(inst.rm, l));
                state_.SetVectorLane32(inst.rd, l, (a > b) ? 0xFFFFFFFFu : 0u);
            }
            break;
        }
        case Opcode::CMHI_vec: {  // unsigned >
            for (u32 l = 0; l < 4; ++l) {
                state_.SetVectorLane32(inst.rd, l,
                    state_.GetVectorLane32(inst.rn, l) > state_.GetVectorLane32(inst.rm, l)
                        ? 0xFFFFFFFFu : 0u);
            }
            break;
        }
        case Opcode::CMEQ_vec: {
            for (u32 l = 0; l < 4; ++l) {
                state_.SetVectorLane32(inst.rd, l,
                    state_.GetVectorLane32(inst.rn, l) == state_.GetVectorLane32(inst.rm, l)
                        ? 0xFFFFFFFFu : 0u);
            }
            break;
        }
        case Opcode::SMAX_vec:
        case Opcode::UMAX_vec: {
            for (u32 l = 0; l < 4; ++l) {
                u32 r;
                if (inst.opcode == Opcode::SMAX_vec) {
                    r = static_cast<u32>(std::max(static_cast<s32>(state_.GetVectorLane32(inst.rn, l)),
                                                  static_cast<s32>(state_.GetVectorLane32(inst.rm, l))));
                } else {
                    r = std::max(state_.GetVectorLane32(inst.rn, l), state_.GetVectorLane32(inst.rm, l));
                }
                state_.SetVectorLane32(inst.rd, l, r);
            }
            break;
        }
        case Opcode::SMIN_vec:
        case Opcode::UMIN_vec: {
            for (u32 l = 0; l < 4; ++l) {
                u32 r;
                if (inst.opcode == Opcode::SMIN_vec) {
                    r = static_cast<u32>(std::min(static_cast<s32>(state_.GetVectorLane32(inst.rn, l)),
                                                  static_cast<s32>(state_.GetVectorLane32(inst.rm, l))));
                } else {
                    r = std::min(state_.GetVectorLane32(inst.rn, l), state_.GetVectorLane32(inst.rm, l));
                }
                state_.SetVectorLane32(inst.rd, l, r);
            }
            break;
        }
        case Opcode::MUL_vec: {
            for (u32 l = 0; l < 4; ++l) {
                state_.SetVectorLane32(inst.rd, l,
                    state_.GetVectorLane32(inst.rn, l) * state_.GetVectorLane32(inst.rm, l));
            }
            break;
        }
        case Opcode::MLA_vec: {  // accumulate into rd
            for (u32 l = 0; l < 4; ++l) {
                state_.SetVectorLane32(inst.rd, l,
                    state_.GetVectorLane32(inst.rd, l) +
                    state_.GetVectorLane32(inst.rn, l) * state_.GetVectorLane32(inst.rm, l));
            }
            break;
        }
        case Opcode::SQADD_vec: {  // saturating add (32-bit lanes)
            for (u32 l = 0; l < 4; ++l) {
                const s64 a = static_cast<s32>(state_.GetVectorLane32(inst.rn, l));
                const s64 b = static_cast<s32>(state_.GetVectorLane32(inst.rm, l));
                s64 r = a + b;
                if (r > 0x7FFFFFFF) r = 0x7FFFFFFF;
                if (r < -0x80000000LL) r = -0x80000000LL;
                state_.SetVectorLane32(inst.rd, l, static_cast<u32>(static_cast<s32>(r)));
            }
            break;
        }
        case Opcode::SHL_vec:
        case Opcode::SSHR_vec:
        case Opcode::USHR_vec: {
            // shift amount = (16 << size) - immh:imb; stored in shift_amount (immh only
            // when emulating 32-bit lanes: amount = 32 - immr-encoding shift)
            const u32 esz = (inst.vec_size == 2) ? 32u : 64u;
            u32 amount = esz - inst.shift_amount;
            if (inst.opcode == Opcode::SHL_vec) {
                const u32 amt = inst.shift_amount; // SHL shifts left by immh:imb directly
                for (u32 l = 0; l < 4; ++l) {
                    state_.SetVectorLane32(inst.rd, l,
                        state_.GetVectorLane32(inst.rn, l) << amt);
                }
            } else if (inst.opcode == Opcode::USHR_vec) {
                for (u32 l = 0; l < 4; ++l) {
                    state_.SetVectorLane32(inst.rd, l,
                        state_.GetVectorLane32(inst.rn, l) >> amount);
                }
            } else { // SSHR arithmetic
                for (u32 l = 0; l < 4; ++l) {
                    state_.SetVectorLane32(inst.rd, l,
                        static_cast<u32>(static_cast<s32>(state_.GetVectorLane32(inst.rn, l)) >> amount));
                }
            }
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
                state_.SetX(inst.rd, memory_->Read64(addr));
            } else {
                state_.SetW(inst.rd, memory_->Read32(addr));
            }
            break;
        }

        case Opcode::STXR: {
            const vaddr_t addr = state_.GetRegOrSP(inst.rn);
            if (state_.exclusive_active && state_.exclusive_addr == addr) {
                if (inst.is_64bit) {
                    memory_->Write64(addr, state_.GetX(inst.rd));
                } else {
                    memory_->Write32(addr, state_.GetW(inst.rd));
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
                u8* ptr = memory_->GetPointer(addr);
                if (ptr && (reinterpret_cast<uintptr_t>(ptr) % 8 == 0)) {
                    auto* aptr = reinterpret_cast<std::atomic<u64>*>(ptr);
                    const u64 old_val = aptr->fetch_add(state_.GetX(inst.rs));
                    state_.SetX(inst.rd, old_val);
                } else {
                    const u64 old_val = memory_->Read64(addr);
                    memory_->Write64(addr, old_val + state_.GetX(inst.rs));
                    state_.SetX(inst.rd, old_val);
                }
            } else {
                u8* ptr = memory_->GetPointer(addr);
                if (ptr && (reinterpret_cast<uintptr_t>(ptr) % 4 == 0)) {
                    auto* aptr = reinterpret_cast<std::atomic<u32>*>(ptr);
                    const u32 old_val = aptr->fetch_add(state_.GetW(inst.rs));
                    state_.SetW(inst.rd, old_val);
                } else {
                    const u32 old_val = memory_->Read32(addr);
                    memory_->Write32(addr, old_val + state_.GetW(inst.rs));
                    state_.SetW(inst.rd, old_val);
                }
            }
            break;
        }

        case Opcode::LDCLR: {
            const vaddr_t addr = state_.GetRegOrSP(inst.rn);
            if (inst.is_64bit) {
                u8* ptr = memory_->GetPointer(addr);
                if (ptr && (reinterpret_cast<uintptr_t>(ptr) % 8 == 0)) {
                    auto* aptr = reinterpret_cast<std::atomic<u64>*>(ptr);
                    const u64 old_val = aptr->fetch_and(~state_.GetX(inst.rs));
                    state_.SetX(inst.rd, old_val);
                } else {
                    const u64 old_val = memory_->Read64(addr);
                    memory_->Write64(addr, old_val & ~state_.GetX(inst.rs));
                    state_.SetX(inst.rd, old_val);
                }
            } else {
                u8* ptr = memory_->GetPointer(addr);
                if (ptr && (reinterpret_cast<uintptr_t>(ptr) % 4 == 0)) {
                    auto* aptr = reinterpret_cast<std::atomic<u32>*>(ptr);
                    const u32 old_val = aptr->fetch_and(~state_.GetW(inst.rs));
                    state_.SetW(inst.rd, old_val);
                } else {
                    const u32 old_val = memory_->Read32(addr);
                    memory_->Write32(addr, old_val & ~state_.GetW(inst.rs));
                    state_.SetW(inst.rd, old_val);
                }
            }
            break;
        }

        case Opcode::LDSET: {
            const vaddr_t addr = state_.GetRegOrSP(inst.rn);
            if (inst.is_64bit) {
                u8* ptr = memory_->GetPointer(addr);
                if (ptr && (reinterpret_cast<uintptr_t>(ptr) % 8 == 0)) {
                    auto* aptr = reinterpret_cast<std::atomic<u64>*>(ptr);
                    const u64 old_val = aptr->fetch_or(state_.GetX(inst.rs));
                    state_.SetX(inst.rd, old_val);
                } else {
                    const u64 old_val = memory_->Read64(addr);
                    memory_->Write64(addr, old_val | state_.GetX(inst.rs));
                    state_.SetX(inst.rd, old_val);
                }
            } else {
                u8* ptr = memory_->GetPointer(addr);
                if (ptr && (reinterpret_cast<uintptr_t>(ptr) % 4 == 0)) {
                    auto* aptr = reinterpret_cast<std::atomic<u32>*>(ptr);
                    const u32 old_val = aptr->fetch_or(state_.GetW(inst.rs));
                    state_.SetW(inst.rd, old_val);
                } else {
                    const u32 old_val = memory_->Read32(addr);
                    memory_->Write32(addr, old_val | state_.GetW(inst.rs));
                    state_.SetW(inst.rd, old_val);
                }
            }
            break;
        }

        case Opcode::LDEOR: {
            const vaddr_t addr = state_.GetRegOrSP(inst.rn);
            if (inst.is_64bit) {
                u8* ptr = memory_->GetPointer(addr);
                if (ptr && (reinterpret_cast<uintptr_t>(ptr) % 8 == 0)) {
                    auto* aptr = reinterpret_cast<std::atomic<u64>*>(ptr);
                    const u64 old_val = aptr->fetch_xor(state_.GetX(inst.rs));
                    state_.SetX(inst.rd, old_val);
                } else {
                    const u64 old_val = memory_->Read64(addr);
                    memory_->Write64(addr, old_val ^ state_.GetX(inst.rs));
                    state_.SetX(inst.rd, old_val);
                }
            } else {
                u8* ptr = memory_->GetPointer(addr);
                if (ptr && (reinterpret_cast<uintptr_t>(ptr) % 4 == 0)) {
                    auto* aptr = reinterpret_cast<std::atomic<u32>*>(ptr);
                    const u32 old_val = aptr->fetch_xor(state_.GetW(inst.rs));
                    state_.SetW(inst.rd, old_val);
                } else {
                    const u32 old_val = memory_->Read32(addr);
                    memory_->Write32(addr, old_val ^ state_.GetW(inst.rs));
                    state_.SetW(inst.rd, old_val);
                }
            }
            break;
        }

        case Opcode::CAS: {
            const vaddr_t addr = state_.GetRegOrSP(inst.rn);
            if (inst.is_64bit) {
                u8* ptr = memory_->GetPointer(addr);
                if (ptr && (reinterpret_cast<uintptr_t>(ptr) % 8 == 0)) {
                    auto* aptr = reinterpret_cast<std::atomic<u64>*>(ptr);
                    u64 expected = state_.GetX(inst.rs);
                    const u64 desired = state_.GetX(inst.rd);
                    aptr->compare_exchange_strong(expected, desired);
                    state_.SetX(inst.rs, expected);
                } else {
                    const u64 cur = memory_->Read64(addr);
                    const u64 cmp = state_.GetX(inst.rs);
                    if (cur == cmp) {
                        memory_->Write64(addr, state_.GetX(inst.rd));
                    }
                    state_.SetX(inst.rs, cur);
                }
            } else {
                u8* ptr = memory_->GetPointer(addr);
                if (ptr && (reinterpret_cast<uintptr_t>(ptr) % 4 == 0)) {
                    auto* aptr = reinterpret_cast<std::atomic<u32>*>(ptr);
                    u32 expected = state_.GetW(inst.rs);
                    const u32 desired = state_.GetW(inst.rd);
                    aptr->compare_exchange_strong(expected, desired);
                    state_.SetW(inst.rs, expected);
                } else {
                    const u32 cur = memory_->Read32(addr);
                    const u32 cmp = state_.GetW(inst.rs);
                    if (cur == cmp) {
                        memory_->Write32(addr, state_.GetW(inst.rd));
                    }
                    state_.SetW(inst.rs, cur);
                }
            }
            break;
        }

        case Opcode::CASP: {
            const vaddr_t addr = state_.GetRegOrSP(inst.rn);
            const u64 cur_lo = memory_->Read64(addr);
            const u64 cur_hi = memory_->Read64(addr + 8);
            const u64 cmp_lo = state_.GetX(inst.rs);
            const u64 cmp_hi = state_.GetX(inst.rs + 1);
            if (cur_lo == cmp_lo && cur_hi == cmp_hi) {
                memory_->Write64(addr, state_.GetX(inst.rd));
                memory_->Write64(addr + 8, state_.GetX(inst.rd + 1));
            }
            state_.SetX(inst.rs, cur_lo);
            state_.SetX(inst.rs + 1, cur_hi);
            break;
        }

        case Opcode::SWP: {
            const vaddr_t addr = state_.GetRegOrSP(inst.rn);
            if (inst.is_64bit) {
                u8* ptr = memory_->GetPointer(addr);
                if (ptr && (reinterpret_cast<uintptr_t>(ptr) % 8 == 0)) {
                    auto* aptr = reinterpret_cast<std::atomic<u64>*>(ptr);
                    const u64 old_val = aptr->exchange(state_.GetX(inst.rs));
                    state_.SetX(inst.rd, old_val);
                } else {
                    const u64 cur = memory_->Read64(addr);
                    memory_->Write64(addr, state_.GetX(inst.rs));
                    state_.SetX(inst.rd, cur);
                }
            } else {
                u8* ptr = memory_->GetPointer(addr);
                if (ptr && (reinterpret_cast<uintptr_t>(ptr) % 4 == 0)) {
                    auto* aptr = reinterpret_cast<std::atomic<u32>*>(ptr);
                    const u32 old_val = aptr->exchange(state_.GetW(inst.rs));
                    state_.SetW(inst.rd, old_val);
                } else {
                    const u32 cur = memory_->Read32(addr);
                    memory_->Write32(addr, state_.GetW(inst.rs));
                    state_.SetW(inst.rd, cur);
                }
            }
            break;
        }

        case Opcode::CLREX: {
            state_.exclusive_active = false;
            state_.exclusive_addr = 0;
            break;
        }

        case Opcode::ISB:
            std::atomic_thread_fence(std::memory_order_seq_cst);
            break;

        case Opcode::DSB:
            std::atomic_thread_fence(std::memory_order_seq_cst);
            break;

        case Opcode::DMB:
            std::atomic_thread_fence(std::memory_order_acq_rel);
            break;

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

        case Opcode::MRS: {
            const u32 sys_reg = static_cast<u32>(inst.imm);
            u64 val = 0;
            switch (sys_reg) {
                case 0x5E83: // TPIDRRO_EL0 (TLS base register)
                    val = state_.tpidrro_el0;
                    break;
                case 0x5E82: // TPIDR_EL0 (read/write thread ID)
                    val = state_.tpidr_el0;
                    break;
                case 0x5F00: // CNTFRQ_EL0 (timer frequency, 19.2 MHz)
                    val = state_.cntfrq_el0;
                    break;
                case 0x5F01: // CNTPCT_EL0 (physical counter)
                case 0x5F02: // CNTVCT_EL0 (virtual counter)
                    val = ++state_.cntpct_el0;
                    break;
                case 0x5DA0: // FPCR (Floating-point Control Register)
                    val = state_.fpcr;
                    break;
                case 0x5DA1: // FPSR (Floating-point Status Register)
                    val = state_.fpsr;
                    break;
                case 0x5D40: // NZCV (Condition Flags)
                    val = state_.pstate.Pack();
                    break;
                default:
                    val = 0;
                    break;
            }
            state_.SetX(inst.rd, val);
            break;
        }

        case Opcode::MSR: {
            const u32 sys_reg = static_cast<u32>(inst.imm);
            const u64 val = state_.GetX(inst.rn);
            switch (sys_reg) {
                case 0x5E83: // TPIDRRO_EL0
                    state_.tpidrro_el0 = val;
                    break;
                case 0x5E82: // TPIDR_EL0
                    state_.tpidr_el0 = val;
                    break;
                case 0x5F00: // CNTFRQ_EL0
                    state_.cntfrq_el0 = val;
                    break;
                case 0x5F01: // CNTPCT_EL0
                case 0x5F02: // CNTVCT_EL0
                    state_.cntpct_el0 = val;
                    break;
                case 0x5DA0: // FPCR
                    state_.SetFPCR(static_cast<u32>(val));
                    break;
                case 0x5DA1: // FPSR
                    state_.SetFPSR(static_cast<u32>(val));
                    break;
                case 0x5D40: // NZCV
                    state_.pstate.Unpack(static_cast<u32>(val));
                    break;
                default:
                    break;
            }
            break;
        }

        default:
            NEMU_LOG_ERROR("CPU", "Unhandled opcode {} at 0x{:016X}", inst.OpcodeName(), curr_pc);
            return StepResult::UndefinedInstruction;
    }

    // DIAGNOSTIC: detect a branch to a low/unmapped address (the PC-0 crash).
    if (next_pc < 0x2000ULL) {
        static bool s_logged_low_branch = false;
        if (!s_logged_low_branch) {
            s_logged_low_branch = true;
            NEMU_LOG_ERROR("CPU",
                           "Low-branch: PC 0x{:016X} -> next 0x{:016X} (inst {:08X} {}, rn={} imm={:#x}) "
                           "X30(LR)=0x{:016X} SP=0x{:016X}",
                           curr_pc, next_pc, inst.raw, inst.OpcodeName(), inst.rn, inst.imm,
                           state_.GetX(30), state_.sp);
        }
    }

    state_.pc = next_pc;
    return StepResult::Ok;
}

} // namespace nemu::core::cpu
