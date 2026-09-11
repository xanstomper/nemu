#include "cpu_state.hpp"
#include <format>
#include <sstream>
#include <cmath>
#include <cstring>

namespace nemu::core::cpu {

void CpuState::Reset() noexcept {
    x.fill(0);
    sp = 0;
    pc = 0;
    pstate = PState{};
    v.fill(u128{});
    fpcr = 0;
    fpsr = 0;
    exclusive_addr = 0;
    exclusive_active = false;
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

float CpuState::GetSingle(u32 reg) const noexcept {
    if (reg >= 32) return 0.0f;
    float val = 0.0f;
    const u32 u = static_cast<u32>(v[reg].low);
    std::memcpy(&val, &u, sizeof(val));
    return val;
}

void CpuState::SetSingle(u32 reg, float val) noexcept {
    if (reg >= 32) return;
    u32 u = 0;
    std::memcpy(&u, &val, sizeof(u));
    v[reg].low = u;
    v[reg].high = 0;
}

double CpuState::GetDouble(u32 reg) const noexcept {
    if (reg >= 32) return 0.0;
    double val = 0.0;
    std::memcpy(&val, &v[reg].low, sizeof(val));
    return val;
}

void CpuState::SetDouble(u32 reg, double val) noexcept {
    if (reg >= 32) return;
    std::memcpy(&v[reg].low, &val, sizeof(val));
    v[reg].high = 0;
}

u128 CpuState::GetVector(u32 reg) const noexcept {
    if (reg >= 32) return u128{};
    return v[reg];
}

void CpuState::SetVector(u32 reg, const u128& val) noexcept {
    if (reg < 32) {
        v[reg] = val;
    }
}

u8 CpuState::GetVectorLane8(u32 reg, size_t lane) const noexcept {
    if (reg >= 32 || lane >= 16) return 0;
    if (lane < 8) {
        return static_cast<u8>((v[reg].low >> (lane * 8)) & 0xFF);
    } else {
        return static_cast<u8>((v[reg].high >> ((lane - 8) * 8)) & 0xFF);
    }
}

void CpuState::SetVectorLane8(u32 reg, size_t lane, u8 val) noexcept {
    if (reg >= 32 || lane >= 16) return;
    if (lane < 8) {
        const u64 mask = ~(0xFFULL << (lane * 8));
        v[reg].low = (v[reg].low & mask) | (static_cast<u64>(val) << (lane * 8));
    } else {
        const u64 mask = ~(0xFFULL << ((lane - 8) * 8));
        v[reg].high = (v[reg].high & mask) | (static_cast<u64>(val) << ((lane - 8) * 8));
    }
}

u16 CpuState::GetVectorLane16(u32 reg, size_t lane) const noexcept {
    if (reg >= 32 || lane >= 8) return 0;
    if (lane < 4) {
        return static_cast<u16>((v[reg].low >> (lane * 16)) & 0xFFFF);
    } else {
        return static_cast<u16>((v[reg].high >> ((lane - 4) * 16)) & 0xFFFF);
    }
}

void CpuState::SetVectorLane16(u32 reg, size_t lane, u16 val) noexcept {
    if (reg >= 32 || lane >= 8) return;
    if (lane < 4) {
        const u64 mask = ~(0xFFFFULL << (lane * 16));
        v[reg].low = (v[reg].low & mask) | (static_cast<u64>(val) << (lane * 16));
    } else {
        const u64 mask = ~(0xFFFFULL << ((lane - 4) * 16));
        v[reg].high = (v[reg].high & mask) | (static_cast<u64>(val) << ((lane - 4) * 16));
    }
}

u32 CpuState::GetVectorLane32(u32 reg, size_t lane) const noexcept {
    if (reg >= 32 || lane >= 4) return 0;
    if (lane < 2) {
        return static_cast<u32>((v[reg].low >> (lane * 32)) & 0xFFFFFFFF);
    } else {
        return static_cast<u32>((v[reg].high >> ((lane - 2) * 32)) & 0xFFFFFFFF);
    }
}

void CpuState::SetVectorLane32(u32 reg, size_t lane, u32 val) noexcept {
    if (reg >= 32 || lane >= 4) return;
    if (lane < 2) {
        const u64 mask = ~(0xFFFFFFFFULL << (lane * 32));
        v[reg].low = (v[reg].low & mask) | (static_cast<u64>(val) << (lane * 32));
    } else {
        const u64 mask = ~(0xFFFFFFFFULL << ((lane - 2) * 32));
        v[reg].high = (v[reg].high & mask) | (static_cast<u64>(val) << ((lane - 2) * 32));
    }
}

u64 CpuState::GetVectorLane64(u32 reg, size_t lane) const noexcept {
    if (reg >= 32 || lane >= 2) return 0;
    return (lane == 0) ? v[reg].low : v[reg].high;
}

void CpuState::SetVectorLane64(u32 reg, size_t lane, u64 val) noexcept {
    if (reg >= 32 || lane >= 2) return;
    if (lane == 0) {
        v[reg].low = val;
    } else {
        v[reg].high = val;
    }
}

void CpuState::SetNZCV_FPCmp(double a, double b) noexcept {
    if (std::isnan(a) || std::isnan(b)) {
        pstate.n = false;
        pstate.z = false;
        pstate.c = true;
        pstate.v = true;
    } else if (a == b) {
        pstate.n = false;
        pstate.z = true;
        pstate.c = true;
        pstate.v = false;
    } else if (a < b) {
        pstate.n = true;
        pstate.z = false;
        pstate.c = false;
        pstate.v = false;
    } else {
        pstate.n = false;
        pstate.z = false;
        pstate.c = true;
        pstate.v = false;
    }
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
