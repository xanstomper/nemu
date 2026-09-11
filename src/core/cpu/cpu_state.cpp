#include "cpu_state.hpp"
#include <format>
#include <sstream>

namespace nemu::core::cpu {

void CpuState::Reset() noexcept {
    x.fill(0);
    sp = 0;
    pc = 0;
    pstate = PState{};
    v.fill(u128{});
    fpcr = 0;
    fpsr = 0;
    tpidrro_el0 = 0;
    tpidr_el0 = 0;
    cntfrq_el0 = 19200000;
    cntpct_el0 = 0;
    halted = false;
    total_instructions = 0;
}

u64 CpuState::GetX(u32 reg) const noexcept {
    if (reg >= 31) return 0; // XZR
    return x[reg];
}

void CpuState::SetX(u32 reg, u64 val) noexcept {
    if (reg < 31) {
        x[reg] = val;
    }
    // reg == 31 is XZR (writes discarded)
}

u32 CpuState::GetW(u32 reg) const noexcept {
    return static_cast<u32>(GetX(reg));
}

void CpuState::SetW(u32 reg, u32 val) noexcept {
    // In ARM64, 32-bit operations write zero-extended 64-bit result to X register
    if (reg < 31) {
        x[reg] = static_cast<u64>(val);
    }
}

u64 CpuState::GetRegOrSP(u32 reg) const noexcept {
    if (reg == 31) return sp;
    return x[reg];
}

void CpuState::SetRegOrSP(u32 reg, u64 val) noexcept {
    if (reg == 31) {
        sp = val;
    } else {
        x[reg] = val;
    }
}

u32 CpuState::GetWRegOrSP(u32 reg) const noexcept {
    return static_cast<u32>(GetRegOrSP(reg));
}

void CpuState::SetWRegOrSP(u32 reg, u32 val) noexcept {
    if (reg == 31) {
        sp = static_cast<u64>(val);
    } else {
        x[reg] = static_cast<u64>(val);
    }
}

bool CpuState::CheckCondition(Condition cond) const noexcept {
    switch (cond) {
        case Condition::EQ: return pstate.z;
        case Condition::NE: return !pstate.z;
        case Condition::CS: return pstate.c;
        case Condition::CC: return !pstate.c;
        case Condition::MI: return pstate.n;
        case Condition::PL: return !pstate.n;
        case Condition::VS: return pstate.v;
        case Condition::VC: return !pstate.v;
        case Condition::HI: return pstate.c && !pstate.z;
        case Condition::LS: return !pstate.c || pstate.z;
        case Condition::GE: return pstate.n == pstate.v;
        case Condition::LT: return pstate.n != pstate.v;
        case Condition::GT: return !pstate.z && (pstate.n == pstate.v);
        case Condition::LE: return pstate.z || (pstate.n != pstate.v);
        case Condition::AL:
        case Condition::NV: return true;
        default: return true;
    }
}

void CpuState::SetNZCV_Add32(u32 a, u32 b, u32 result) noexcept {
    pstate.n = (result >> 31) & 1;
    pstate.z = (result == 0);
    // Carry for unsigned addition: result < a
    pstate.c = (result < a);
    // Overflow for signed addition: inputs have same sign, but result has different sign
    const bool a_sign = (a >> 31) & 1;
    const bool b_sign = (b >> 31) & 1;
    const bool res_sign = (result >> 31) & 1;
    pstate.v = (a_sign == b_sign) && (a_sign != res_sign);
}

void CpuState::SetNZCV_Add64(u64 a, u64 b, u64 result) noexcept {
    pstate.n = (result >> 63) & 1;
    pstate.z = (result == 0);
    pstate.c = (result < a);
    const bool a_sign = (a >> 63) & 1;
    const bool b_sign = (b >> 63) & 1;
    const bool res_sign = (result >> 63) & 1;
    pstate.v = (a_sign == b_sign) && (a_sign != res_sign);
}

void CpuState::SetNZCV_Sub32(u32 a, u32 b, u32 result) noexcept {
    pstate.n = (result >> 31) & 1;
    pstate.z = (result == 0);
    // In ARM, carry flag on subtraction is NOT-borrow: C = (a >= b)
    pstate.c = (a >= b);
    const bool a_sign = (a >> 31) & 1;
    const bool b_sign = (b >> 31) & 1;
    const bool res_sign = (result >> 31) & 1;
    // Overflow: (a and ~b have same sign, but result differs) -> a_sign != b_sign && a_sign != res_sign
    pstate.v = (a_sign != b_sign) && (a_sign != res_sign);
}

void CpuState::SetNZCV_Sub64(u64 a, u64 b, u64 result) noexcept {
    pstate.n = (result >> 63) & 1;
    pstate.z = (result == 0);
    pstate.c = (a >= b);
    const bool a_sign = (a >> 63) & 1;
    const bool b_sign = (b >> 63) & 1;
    const bool res_sign = (result >> 63) & 1;
    pstate.v = (a_sign != b_sign) && (a_sign != res_sign);
}

void CpuState::SetNZ_Logical32(u32 result) noexcept {
    pstate.n = (result >> 31) & 1;
    pstate.z = (result == 0);
    pstate.c = false;
    pstate.v = false;
}

void CpuState::SetNZ_Logical64(u64 result) noexcept {
    pstate.n = (result >> 63) & 1;
    pstate.z = (result == 0);
    pstate.c = false;
    pstate.v = false;
}

std::string CpuState::DumpState() const {
    std::stringstream ss;
    ss << std::format("PC: 0x{:016X}  SP: 0x{:016X}  PSTATE: [{}{}{}{}]\n",
                      pc, sp,
                      pstate.n ? "N" : "-",
                      pstate.z ? "Z" : "-",
                      pstate.c ? "C" : "-",
                      pstate.v ? "V" : "-");
    for (size_t i = 0; i < 31; i += 2) {
        if (i + 1 < 31) {
            ss << std::format("X{:02}: 0x{:016X}  X{:02}: 0x{:016X}\n", i, x[i], i + 1, x[i + 1]);
        } else {
            ss << std::format("X{:02}: 0x{:016X}\n", i, x[i]);
        }
    }
    return ss.str();
}

} // namespace nemu::core::cpu
