#pragma once

#include "core/types.hpp"
#include "cpu_state.hpp"
#include <string_view>

namespace nemu::core::cpu {

enum class Opcode : u16 {
    UNDEFINED = 0,

    // Data Processing - Immediate
    ADD_imm,
    ADDS_imm,
    SUB_imm,
    SUBS_imm,
    MOVZ,
    MOVN,
    MOVK,
    AND_imm,
    ANDS_imm,
    ORR_imm,
    EOR_imm,
    ADR,
    ADRP,

    // Data Processing - Register
    ADD_reg,
    ADDS_reg,
    SUB_reg,
    SUBS_reg,
    AND_reg,
    ANDS_reg,
    BIC_reg,
    BICS_reg,
    ORR_reg,
    ORN_reg,
    EOR_reg,
    EON_reg,
    LSLV,
    LSRV,
    ASRV,
    RORV,
    MADD,
    MSUB,
    SMULL,
    UMULL,

    // Branches & Control Flow
    B,
    B_cond,
    BL,
    BLR,
    RET,
    CBZ,
    CBNZ,
    TBZ,
    TBNZ,

    // Loads and Stores
    LDR_imm,
    STR_imm,
    LDRB_imm,
    STRB_imm,
    LDRH_imm,
    STRH_imm,
    LDRSB_imm,
    LDRSH_imm,
    LDRSW_imm,
    LDR_reg,
    STR_reg,
    LDP,
    STP,

    // System & Exception
    NOP,
    SVC,
    BRK,
    MRS,
    MSR,

    // Conditional Select
    CSEL
};

enum class AddressingMode : u8 {
    UnscaledImmediate,
    UnsignedOffset,
    PreIndexed,
    PostIndexed,
    RegisterOffset
};

struct DecodedInstruction {
    Opcode opcode{Opcode::UNDEFINED};
    u32 raw{0};

    // Operands
    bool is_64bit{true};
    u8 rd{0};   // Destination register (or Rt)
    u8 rn{0};   // First source register
    u8 rm{0};   // Second source register
    u8 ra{0};   // Third source register (e.g. for MADD/MSUB)
    u8 rt2{0};  // Second transfer register for LDP/STP

    u64 imm{0}; // Immediate value (or sign-extended offset)
    u8 shift_type{0}; // 0: LSL, 1: LSR, 2: ASR, 3: ROR
    u8 shift_amount{0};

    Condition condition{Condition::AL};
    AddressingMode addr_mode{AddressingMode::UnsignedOffset};

    // Bitfield / test bit
    u8 bit_pos{0};

    std::string_view OpcodeName() const noexcept;
};

} // namespace nemu::core::cpu
