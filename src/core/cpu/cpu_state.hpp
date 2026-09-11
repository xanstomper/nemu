#pragma once

#include "core/types.hpp"
#include <array>
#include <string>

namespace nemu::core::cpu {

enum class Condition : u8 {
    EQ = 0b0000, // Equal (Z == 1)
    NE = 0b0001, // Not equal (Z == 0)
    CS = 0b0010, // Carry set / unsigned higher or same (C == 1)
    CC = 0b0011, // Carry clear / unsigned lower (C == 0)
    MI = 0b0100, // Minus / negative (N == 1)
    PL = 0b0101, // Plus / positive or zero (N == 0)
    VS = 0b0110, // Overflow set (V == 1)
    VC = 0b0111, // Overflow clear (V == 0)
    HI = 0b1000, // Unsigned higher (C == 1 && Z == 0)
    LS = 0b1001, // Unsigned lower or same (C == 0 || Z == 1)
    GE = 0b1010, // Signed greater than or equal (N == V)
    LT = 0b1011, // Signed less than (N != V)
    GT = 0b1100, // Signed greater than (Z == 0 && N == V)
    LE = 0b1101, // Signed less than or equal (Z == 1 || N != V)
    AL = 0b1110, // Always
    NV = 0b1111  // Always (legacy/obsolete alias)
};

struct alignas(16) CpuState {
    std::array<u64, 31> x{};
    u64 sp{0};
    u64 pc{0};

    // Processor State Flags (NZCV)
    struct PState {
        bool n{false}; // Negative
        bool z{false}; // Zero
        bool c{false}; // Carry
        bool v{false}; // Overflow

        constexpr u32 Pack() const noexcept {
            return (n ? (1u << 31) : 0) |
                   (z ? (1u << 30) : 0) |
                   (c ? (1u << 29) : 0) |
                   (v ? (1u << 28) : 0);
        }

        constexpr void Unpack(u32 val) noexcept {
            n = (val & (1u << 31)) != 0;
            z = (val & (1u << 30)) != 0;
            c = (val & (1u << 29)) != 0;
            v = (val & (1u << 28)) != 0;
        }
    } pstate;

    // Vector / FP Registers (V0 - V31)
    std::array<u128, 32> v{};
    u32 fpcr{0};
    u32 fpsr{0};

    // System Registers
    u64 tpidrro_el0{0}; // TLS base register for user-mode
    u64 tpidr_el0{0};
    u64 cntfrq_el0{19200000}; // Switch counter frequency = 19.2 MHz
    u64 cntpct_el0{0};

    // Execution control
    bool halted{false};
    u64 total_instructions{0};

    void Reset() noexcept;

    // Register accessors (handling XZR/WZR vs SP context)
    [[nodiscard]] u64 GetX(u32 reg) const noexcept;
    void SetX(u32 reg, u64 val) noexcept;

    [[nodiscard]] u32 GetW(u32 reg) const noexcept;
    void SetW(u32 reg, u32 val) noexcept;

    [[nodiscard]] u64 GetRegOrSP(u32 reg) const noexcept;
    void SetRegOrSP(u32 reg, u64 val) noexcept;

    [[nodiscard]] u32 GetWRegOrSP(u32 reg) const noexcept;
    void SetWRegOrSP(u32 reg, u32 val) noexcept;

    [[nodiscard]] bool CheckCondition(Condition cond) const noexcept;

    // Flag update helpers
    void SetNZCV_Add32(u32 a, u32 b, u32 result) noexcept;
    void SetNZCV_Add64(u64 a, u64 b, u64 result) noexcept;
    void SetNZCV_Sub32(u32 a, u32 b, u32 result) noexcept;
    void SetNZCV_Sub64(u64 a, u64 b, u64 result) noexcept;
    void SetNZ_Logical32(u32 result) noexcept;
    void SetNZ_Logical64(u64 result) noexcept;

    std::string DumpState() const;
};

} // namespace nemu::core::cpu
