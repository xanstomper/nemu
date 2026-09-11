#include "decoder.hpp"
#include <cmath>

namespace nemu::core::cpu {

std::string_view DecodedInstruction::OpcodeName() const noexcept {
    switch (opcode) {
        case Opcode::ADD_imm: return "ADD (imm)";
        case Opcode::ADDS_imm: return "ADDS (imm)";
        case Opcode::SUB_imm: return "SUB (imm)";
        case Opcode::SUBS_imm: return "SUBS (imm)";
        case Opcode::MOVZ: return "MOVZ";
        case Opcode::MOVN: return "MOVN";
        case Opcode::MOVK: return "MOVK";
        case Opcode::AND_imm: return "AND (imm)";
        case Opcode::ANDS_imm: return "ANDS (imm)";
        case Opcode::ORR_imm: return "ORR (imm)";
        case Opcode::EOR_imm: return "EOR (imm)";
        case Opcode::ADR: return "ADR";
        case Opcode::ADRP: return "ADRP";
        case Opcode::ADD_reg: return "ADD (reg)";
        case Opcode::ADDS_reg: return "ADDS (reg)";
        case Opcode::SUB_reg: return "SUB (reg)";
        case Opcode::SUBS_reg: return "SUBS (reg)";
        case Opcode::AND_reg: return "AND (reg)";
        case Opcode::ANDS_reg: return "ANDS (reg)";
        case Opcode::BIC_reg: return "BIC (reg)";
        case Opcode::BICS_reg: return "BICS (reg)";
        case Opcode::ORR_reg: return "ORR (reg)";
        case Opcode::ORN_reg: return "ORN (reg)";
        case Opcode::EOR_reg: return "EOR (reg)";
        case Opcode::EON_reg: return "EON (reg)";
        case Opcode::LSLV: return "LSLV";
        case Opcode::LSRV: return "LSRV";
        case Opcode::ASRV: return "ASRV";
        case Opcode::RORV: return "RORV";
        case Opcode::MADD: return "MADD";
        case Opcode::MSUB: return "MSUB";
        case Opcode::B: return "B";
        case Opcode::B_cond: return "B.cond";
        case Opcode::BL: return "BL";
        case Opcode::BLR: return "BLR";
        case Opcode::RET: return "RET";
        case Opcode::CBZ: return "CBZ";
        case Opcode::CBNZ: return "CBNZ";
        case Opcode::TBZ: return "TBZ";
        case Opcode::TBNZ: return "TBNZ";
        case Opcode::LDR_imm: return "LDR (imm)";
        case Opcode::STR_imm: return "STR (imm)";
        case Opcode::LDRB_imm: return "LDRB (imm)";
        case Opcode::STRB_imm: return "STRB (imm)";
        case Opcode::LDRH_imm: return "LDRH (imm)";
        case Opcode::STRH_imm: return "STRH (imm)";
        case Opcode::LDRSB_imm: return "LDRSB (imm)";
        case Opcode::LDRSH_imm: return "LDRSH (imm)";
        case Opcode::LDRSW_imm: return "LDRSW (imm)";
        case Opcode::LDP: return "LDP";
        case Opcode::STP: return "STP";
        case Opcode::NOP: return "NOP";
        case Opcode::SVC: return "SVC";
        case Opcode::BRK: return "BRK";
        case Opcode::MRS: return "MRS";
        case Opcode::MSR: return "MSR";
        case Opcode::CSEL: return "CSEL";

        // Scalar Floating-Point
        case Opcode::FADD_scalar: return "FADD (scalar)";
        case Opcode::FSUB_scalar: return "FSUB (scalar)";
        case Opcode::FMUL_scalar: return "FMUL (scalar)";
        case Opcode::FDIV_scalar: return "FDIV (scalar)";
        case Opcode::FMAX_scalar: return "FMAX (scalar)";
        case Opcode::FMIN_scalar: return "FMIN (scalar)";
        case Opcode::FNEG_scalar: return "FNEG (scalar)";
        case Opcode::FABS_scalar: return "FABS (scalar)";
        case Opcode::FSQRT_scalar: return "FSQRT (scalar)";
        case Opcode::FCMP_scalar: return "FCMP (scalar)";
        case Opcode::FCSEL_scalar: return "FCSEL (scalar)";
        case Opcode::FMOV_reg: return "FMOV (reg)";
        case Opcode::FMOV_imm: return "FMOV (imm)";
        case Opcode::FMOV_to_gp: return "FMOV (FP->GP)";
        case Opcode::FMOV_from_gp: return "FMOV (GP->FP)";
        case Opcode::SCVTF: return "SCVTF";
        case Opcode::UCVTF: return "UCVTF";
        case Opcode::FCVTZS: return "FCVTZS";
        case Opcode::FCVTZU: return "FCVTZU";
        case Opcode::FCVT: return "FCVT";
        case Opcode::LDR_fp_imm: return "LDR (FP imm)";
        case Opcode::STR_fp_imm: return "STR (FP imm)";
        case Opcode::LDP_fp: return "LDP (FP)";
        case Opcode::STP_fp: return "STP (FP)";

        // Vector / NEON SIMD
        case Opcode::ADD_vec: return "ADD (vector)";
        case Opcode::SUB_vec: return "SUB (vector)";
        case Opcode::FADD_vec: return "FADD (vector)";
        case Opcode::FSUB_vec: return "FSUB (vector)";
        case Opcode::FMUL_vec: return "FMUL (vector)";
        case Opcode::DUP_gen: return "DUP (gen)";
        case Opcode::DUP_elem: return "DUP (elem)";
        case Opcode::INS_gen: return "INS (gen)";
        case Opcode::INS_elem: return "INS (elem)";
        case Opcode::UMOV: return "UMOV";
        case Opcode::SMOV: return "SMOV";
        case Opcode::AND_vec: return "AND (vector)";
        case Opcode::ORR_vec: return "ORR (vector)";
        case Opcode::EOR_vec: return "EOR (vector)";
        case Opcode::NOT_vec: return "NOT (vector)";

        // Atomics
        case Opcode::LDXR: return "LDXR";
        case Opcode::STXR: return "STXR";
        case Opcode::LDADD: return "LDADD";
        case Opcode::CAS: return "CAS";
        case Opcode::SWP: return "SWP";
        case Opcode::CLREX: return "CLREX";
        default: return "UNDEFINED";
    }
}

DecodedInstruction Decoder::Decode(u32 raw) noexcept {
    // Check NOP explicitly
    if (raw == 0xD503201F) {
        DecodedInstruction inst{};
        inst.opcode = Opcode::NOP;
        inst.raw = raw;
        return inst;
    }

    // Check CLREX explicitly (0xD5033F5F)
    if (raw == 0xD5033F5F) {
        DecodedInstruction inst{};
        inst.opcode = Opcode::CLREX;
        inst.raw = raw;
        return inst;
    }

    const u32 op0 = ExtractBits(raw, 25, 4);

    switch (op0) {
        case 0b1000:
        case 0b1001:
            return DecodeDataProcImm(raw);
        case 0b1010:
        case 0b1011:
            return DecodeBranches(raw);
        case 0b0100:
        case 0b0110:
        case 0b1100:
        case 0b1110:
            return DecodeLoadStore(raw);
        case 0b0101:
        case 0b1101:
            return DecodeDataProcReg(raw);
        case 0b0111:
        case 0b1111:
            return DecodeDataProcSimdFp(raw);
        default:
            break;
    }

    DecodedInstruction inst{};
    inst.opcode = Opcode::UNDEFINED;
    inst.raw = raw;
    return inst;
}

DecodedInstruction Decoder::DecodeBranches(u32 raw) noexcept {
    DecodedInstruction inst{};
    inst.raw = raw;


    // Unconditional branch (immediate): B and BL
    // [op:1] 00101 [imm26]
    if ((raw & 0x7C000000) == 0x14000000) {
        const bool is_bl = ExtractBit(raw, 31);
        inst.opcode = is_bl ? Opcode::BL : Opcode::B;
        const u32 imm26 = ExtractBits(raw, 0, 26);
        inst.imm = SignExtend(static_cast<u64>(imm26) << 2, 28);
        return inst;
    }

    // Compare and branch (immediate): CBZ / CBNZ
    // [sf:1] 011010 [op:1] [imm19] [Rt:5]
    if ((raw & 0x7E000000) == 0x34000000) {
        inst.is_64bit = ExtractBit(raw, 31);
        const bool is_cbnz = ExtractBit(raw, 24);
        inst.opcode = is_cbnz ? Opcode::CBNZ : Opcode::CBZ;
        inst.rd = static_cast<u8>(ExtractBits(raw, 0, 5));
        const u32 imm19 = ExtractBits(raw, 5, 19);
        inst.imm = SignExtend(static_cast<u64>(imm19) << 2, 21);
        return inst;
    }

    // Test and branch (immediate): TBZ / TBNZ
    // [b5:1] 011011 [op:1] [b40:5] [imm14] [Rt:5]
    if ((raw & 0x7E000000) == 0x36000000) {
        const bool b5 = ExtractBit(raw, 31);
        const u32 b40 = ExtractBits(raw, 19, 5);
        inst.bit_pos = static_cast<u8>((b5 ? 32 : 0) | b40);
        const bool is_tbnz = ExtractBit(raw, 24);
        inst.opcode = is_tbnz ? Opcode::TBNZ : Opcode::TBZ;
        inst.rd = static_cast<u8>(ExtractBits(raw, 0, 5));
        const u32 imm14 = ExtractBits(raw, 5, 14);
        inst.imm = SignExtend(static_cast<u64>(imm14) << 2, 16);
        return inst;
    }

    // Conditional branch (immediate): B.cond
    // 01010100 [imm19] 0 [cond:4]
    if ((raw & 0xFF000010) == 0x54000000) {
        inst.opcode = Opcode::B_cond;
        inst.condition = static_cast<Condition>(ExtractBits(raw, 0, 4));
        const u32 imm19 = ExtractBits(raw, 5, 19);
        inst.imm = SignExtend(static_cast<u64>(imm19) << 2, 21);
        return inst;
    }

    // Unconditional branch (register): BLR, RET
    // 1101011 0001 11111 000000 [Rn:5] 00000 -> BLR
    // 1101011 0010 11111 000000 [Rn:5] 00000 -> RET
    if ((raw & 0xFFFFFC1F) == 0xD63F0000) {
        inst.opcode = Opcode::BLR;
        inst.rn = static_cast<u8>(ExtractBits(raw, 5, 5));
        return inst;
    }
    if ((raw & 0xFFFFFC1F) == 0xD65F0000) {
        inst.opcode = Opcode::RET;
        inst.rn = static_cast<u8>(ExtractBits(raw, 5, 5));
        return inst;
    }

    // System instructions: SVC, BRK, MRS, MSR
    if ((raw & 0xFFE0001F) == 0xD4000001) {
        inst.opcode = Opcode::SVC;
        inst.imm = ExtractBits(raw, 5, 16);
        return inst;
    }
    if ((raw & 0xFFE0001F) == 0xD4200000) {
        inst.opcode = Opcode::BRK;
        inst.imm = ExtractBits(raw, 5, 16);
        return inst;
    }

    // MRS: 1101 0101 0011 [sysreg:15] [Rt:5]
    if ((raw & 0xFFF00000) == 0xD5300000) {
        inst.opcode = Opcode::MRS;
        inst.rd = static_cast<u8>(ExtractBits(raw, 0, 5));
        inst.imm = ExtractBits(raw, 5, 15);
        return inst;
    }

    // MSR: 1101 0101 0001 [sysreg:15] [Rt:5]
    if ((raw & 0xFFF00000) == 0xD5100000) {
        inst.opcode = Opcode::MSR;
        inst.rn = static_cast<u8>(ExtractBits(raw, 0, 5));
        inst.imm = ExtractBits(raw, 5, 15);
        return inst;
    }

    return inst;
}

DecodedInstruction Decoder::DecodeDataProcImm(u32 raw) noexcept {
    DecodedInstruction inst{};
    inst.raw = raw;
    inst.is_64bit = ExtractBit(raw, 31);
    inst.rd = static_cast<u8>(ExtractBits(raw, 0, 5));
    inst.rn = static_cast<u8>(ExtractBits(raw, 5, 5));


    // PC-relative addressing: ADR / ADRP
    // [op:1] [immlo:2] 10000 [immhi:19] [Rd:5]
    if ((raw & 0x1F000000) == 0x10000000) {
        const bool is_adrp = ExtractBit(raw, 31);
        inst.opcode = is_adrp ? Opcode::ADRP : Opcode::ADR;
        const u64 immlo = ExtractBits(raw, 29, 2);
        const u64 immhi = ExtractBits(raw, 5, 19);
        const u64 imm21 = (immhi << 2) | immlo;
        inst.imm = SignExtend(imm21, 21);
        return inst;
    }

    // Add/subtract (immediate): ADD, ADDS, SUB, SUBS
    // [sf:1] [op:1] [S:1] 100010 [sh:1] [imm12] [Rn:5] [Rd:5]
    if ((raw & 0x1F000000) == 0x11000000) {
        const bool op = ExtractBit(raw, 30);
        const bool set_flags = ExtractBit(raw, 29);
        const bool sh = ExtractBit(raw, 22);
        const u32 imm12 = ExtractBits(raw, 10, 12);
        inst.imm = sh ? (static_cast<u64>(imm12) << 12) : imm12;

        if (!op && !set_flags) inst.opcode = Opcode::ADD_imm;
        else if (!op && set_flags) inst.opcode = Opcode::ADDS_imm;
        else if (op && !set_flags) inst.opcode = Opcode::SUB_imm;
        else inst.opcode = Opcode::SUBS_imm;

        return inst;
    }

    // Move wide (immediate): MOVZ, MOVN, MOVK
    // [sf:1] [opc:2] 100101 [hw:2] [imm16] [Rd:5]
    if ((raw & 0x1F800000) == 0x12800000) {
        const u32 opc = ExtractBits(raw, 29, 2);
        const u32 hw = ExtractBits(raw, 21, 2);
        const u32 imm16 = ExtractBits(raw, 5, 16);
        inst.shift_amount = static_cast<u8>(hw * 16);
        inst.imm = imm16;

        if (opc == 0b00) inst.opcode = Opcode::MOVN;
        else if (opc == 0b10) inst.opcode = Opcode::MOVZ;
        else if (opc == 0b11) inst.opcode = Opcode::MOVK;

        return inst;
    }

    return inst;
}

DecodedInstruction Decoder::DecodeDataProcReg(u32 raw) noexcept {
    DecodedInstruction inst{};
    inst.raw = raw;
    inst.is_64bit = ExtractBit(raw, 31);
    inst.rd = static_cast<u8>(ExtractBits(raw, 0, 5));
    inst.rn = static_cast<u8>(ExtractBits(raw, 5, 5));
    inst.rm = static_cast<u8>(ExtractBits(raw, 16, 5));

    // Logical (shifted register): AND, BIC, ORR, ORN, EOR, EON, ANDS, BICS
    // [sf:1] [opc:2] 01010 [shift:2] [N:1] [Rm:5] [imm6] [Rn:5] [Rd:5]
    if ((raw & 0x1F000000) == 0x0A000000) {
        const u32 opc = ExtractBits(raw, 29, 2);
        const bool n = ExtractBit(raw, 21);
        inst.shift_type = static_cast<u8>(ExtractBits(raw, 22, 2));
        inst.shift_amount = static_cast<u8>(ExtractBits(raw, 10, 6));

        if (opc == 0b00) inst.opcode = n ? Opcode::BIC_reg : Opcode::AND_reg;
        else if (opc == 0b01) inst.opcode = n ? Opcode::ORN_reg : Opcode::ORR_reg;
        else if (opc == 0b10) inst.opcode = n ? Opcode::EON_reg : Opcode::EOR_reg;
        else if (opc == 0b11) inst.opcode = n ? Opcode::BICS_reg : Opcode::ANDS_reg;

        return inst;
    }

    // Add/subtract (shifted register): ADD, ADDS, SUB, SUBS
    // [sf:1] [op:1] [S:1] 01011 [shift:2] 0 [Rm:5] [imm6] [Rn:5] [Rd:5]
    if ((raw & 0x1F200000) == 0x0B000000) {
        const bool op = ExtractBit(raw, 30);
        const bool set_flags = ExtractBit(raw, 29);
        inst.shift_type = static_cast<u8>(ExtractBits(raw, 22, 2));
        inst.shift_amount = static_cast<u8>(ExtractBits(raw, 10, 6));

        if (!op && !set_flags) inst.opcode = Opcode::ADD_reg;
        else if (!op && set_flags) inst.opcode = Opcode::ADDS_reg;
        else if (op && !set_flags) inst.opcode = Opcode::SUB_reg;
        else inst.opcode = Opcode::SUBS_reg;

        return inst;
    }

    // Variable Shift: LSLV, LSRV, ASRV, RORV
    // [sf:1] 0 0 11010110 [Rm:5] 0010 [op2:2] [Rn:5] [Rd:5]
    if ((raw & 0x7FE0F000) == 0x1AC02000) {
        const u32 op2 = ExtractBits(raw, 10, 2);
        if (op2 == 0b00) inst.opcode = Opcode::LSLV;
        else if (op2 == 0b01) inst.opcode = Opcode::LSRV;
        else if (op2 == 0b10) inst.opcode = Opcode::ASRV;
        else if (op2 == 0b11) inst.opcode = Opcode::RORV;
        return inst;
    }

    // Multiply: MADD, MSUB
    // [sf:1] 00 11011 000 [Rm:5] [o0:1] [Ra:5] [Rn:5] [Rd:5]
    // Mask must NOT fix bit 15 (0x8000): it's the MADD(o0=0) vs MSUB(o0=1)
    // selector and is extracted separately below.
    if ((raw & 0x7FE00000) == 0x1B000000) {
        const bool is_sub = ExtractBit(raw, 15);
        inst.ra = static_cast<u8>(ExtractBits(raw, 10, 5));
        inst.opcode = is_sub ? Opcode::MSUB : Opcode::MADD;
        return inst;
    }

    // Conditional select: CSEL
    // [sf:1] 00 11010100 0 [Rm:5] [cond:4] 0 0 [Rn:5] [Rd:5]
    // Bits 30..21 = 00 11010100, bit 11 = 0, bit 10 = 0; sf is bit 31 (variable).
    if ((raw & 0x7FE00C00) == 0x1A800000) {
        inst.opcode = Opcode::CSEL;
        inst.rm = static_cast<u8>(ExtractBits(raw, 16, 5));
        inst.condition = static_cast<Condition>(ExtractBits(raw, 12, 4));
        return inst;
    }

    return inst;
}

DecodedInstruction Decoder::DecodeLoadStore(u32 raw) noexcept {
    DecodedInstruction inst{};
    inst.raw = raw;
    inst.rd = static_cast<u8>(ExtractBits(raw, 0, 5)); // Rt
    inst.rn = static_cast<u8>(ExtractBits(raw, 5, 5));

    const u32 size = ExtractBits(raw, 30, 2);

    // Load / Store Exclusive
    // [size:2] 001000 [o2:1] [L:1] [o1:1] [Rs:5] [o0:1] [Rt2:5] [Rn:5] [Rt:5]
    if ((raw & 0x3F000000) == 0x08000000) {
        const bool is_load = ExtractBit(raw, 22);
        inst.is_64bit = (size == 0b11);
        inst.rs = static_cast<u8>(ExtractBits(raw, 16, 5));
        inst.opcode = is_load ? Opcode::LDXR : Opcode::STXR;
        return inst;
    }

    // Atomic memory operations (ARMv8.1-A LSE)
    // LDADD: [size:2] 111 0 00 [A:1] [R:1] 1 [Rs:5] 000 [opc:3] [Rn:5] [Rt:5]
    if ((raw & 0x3B200C00) == 0x38200000) {
        const u32 opc = ExtractBits(raw, 12, 3);
        inst.is_64bit = (size == 0b11);
        inst.rs = static_cast<u8>(ExtractBits(raw, 16, 5));
        if (opc == 0b000) {
            inst.opcode = Opcode::LDADD;
            return inst;
        } else if (opc == 0b010) {
            inst.opcode = Opcode::SWP;
            return inst;
        }
    }

    // CAS: [size:2] 001010 [A:1] [R:1] 1 [Rs:5] 1 [o0:4] [Rn:5] [Rt:5]
    if ((raw & 0x3FA00000) == 0x08A00000) {
        inst.is_64bit = (size == 0b11);
        inst.rs = static_cast<u8>(ExtractBits(raw, 16, 5));
        inst.opcode = Opcode::CAS;
        return inst;
    }

    // Load / Store Pair SIMD & FP
    // [opc:2] 101 1 [type:3] [L:1] [imm7] [Rn:5] [Rt:5] [Rt2:5]
    // NOTE: the mask must NOT include bit 22 (the L bit) — bit 22 selects load
    // vs store and is extracted separately below.
    if ((raw & 0x3E000000) == 0x2C000000) {
        const bool is_load = ExtractBit(raw, 22);
        const u32 opc = ExtractBits(raw, 30, 2);
        inst.is_fp_double = (opc == 0b01);
        inst.is_64bit = inst.is_fp_double;
        inst.rt2 = static_cast<u8>(ExtractBits(raw, 10, 5));
        const u32 imm7 = ExtractBits(raw, 15, 7);
        const s64 scale = inst.is_fp_double ? 8 : 4;
        inst.imm = static_cast<u64>(SignExtend(static_cast<s64>(imm7), 7) * scale);
        inst.opcode = is_load ? Opcode::LDP_fp : Opcode::STP_fp;
        return inst;
    }

    // Load / Store Pair (LDP / STP)
    // [opc:2] 101 0 [type:3] [L:1] [imm7] [Rn:5] [Rt:5] [Rt2:5]
    if ((raw & 0x3E400000) == 0x28000000) {
        const bool is_load = ExtractBit(raw, 22);
        inst.is_64bit = ExtractBit(raw, 31);
        inst.rt2 = static_cast<u8>(ExtractBits(raw, 10, 5));
        const u32 imm7 = ExtractBits(raw, 15, 7);
        const s64 scale = inst.is_64bit ? 8 : 4;
        inst.imm = static_cast<u64>(SignExtend(static_cast<s64>(imm7), 7) * scale);
        inst.opcode = is_load ? Opcode::LDP : Opcode::STP;
        return inst;
    }

    // Load / Store Single SIMD & FP (unsigned immediate)
    // [size:2] 111 1 01 [opc:2] [imm12] [Rn:5] [Rt:5]
    if ((raw & 0x3F000000) == 0x3D000000) {
        const bool is_load = ExtractBit(raw, 22);
        const u32 imm12 = ExtractBits(raw, 10, 12);
        if (size == 0b11) { // Double
            inst.is_fp_double = true;
            inst.is_64bit = true;
            inst.imm = static_cast<u64>(imm12) * 8;
            inst.opcode = is_load ? Opcode::LDR_fp_imm : Opcode::STR_fp_imm;
        } else if (size == 0b10) { // Single
            inst.is_fp_double = false;
            inst.is_64bit = false;
            inst.imm = static_cast<u64>(imm12) * 4;
            inst.opcode = is_load ? Opcode::LDR_fp_imm : Opcode::STR_fp_imm;
        }
        return inst;
    }

    // Load / Store Single Register (unsigned immediate)
    // [size:2] 111 0 01 [opc:2] [imm12] [Rn:5] [Rt:5]
    if ((raw & 0x3B200000) == 0x39000000) {
        const bool is_load = ExtractBit(raw, 22);
        const u32 imm12 = ExtractBits(raw, 10, 12);

        if (size == 0b11) { // 64-bit
            inst.is_64bit = true;
            inst.imm = static_cast<u64>(imm12) * 8;
            inst.opcode = is_load ? Opcode::LDR_imm : Opcode::STR_imm;
        } else if (size == 0b10) { // 32-bit
            inst.is_64bit = false;
            inst.imm = static_cast<u64>(imm12) * 4;
            inst.opcode = is_load ? Opcode::LDR_imm : Opcode::STR_imm;
        } else if (size == 0b01) { // 16-bit
            inst.imm = static_cast<u64>(imm12) * 2;
            inst.opcode = is_load ? Opcode::LDRH_imm : Opcode::STRH_imm;
        } else if (size == 0b00) { // 8-bit
            inst.imm = imm12;
            inst.opcode = is_load ? Opcode::LDRB_imm : Opcode::STRB_imm;
        }
        return inst;
    }

    return inst;
}

DecodedInstruction Decoder::DecodeDataProcSimdFp(u32 raw) noexcept {
    DecodedInstruction inst{};
    inst.raw = raw;
    inst.rd = static_cast<u8>(ExtractBits(raw, 0, 5));
    inst.rn = static_cast<u8>(ExtractBits(raw, 5, 5));
    inst.rm = static_cast<u8>(ExtractBits(raw, 16, 5));

    const u32 b28_24 = ExtractBits(raw, 24, 5);
    const bool is_vector = (ExtractBit(raw, 28) == 0);

    if (!is_vector && b28_24 == 0b11110) {
        // Scalar Floating-Point
        const u32 ftype = ExtractBits(raw, 22, 2);
        inst.is_fp_double = (ftype == 1);

        // Check 2-source scalar arithmetic: bit 21=1, bits 11..10 = 0b10
        if (ExtractBit(raw, 21) == 1 && ExtractBits(raw, 10, 2) == 0b10) {
            const u32 op = ExtractBits(raw, 12, 4);
            switch (op) {
                case 0b0000: inst.opcode = Opcode::FMUL_scalar; return inst;
                case 0b0001: inst.opcode = Opcode::FDIV_scalar; return inst;
                case 0b0010: inst.opcode = Opcode::FADD_scalar; return inst;
                case 0b0011: inst.opcode = Opcode::FSUB_scalar; return inst;
                case 0b0100: inst.opcode = Opcode::FMAX_scalar; return inst;
                case 0b0101: inst.opcode = Opcode::FMIN_scalar; return inst;
                default: break;
            }
        }

        // Check FCSEL: bit 21=1, bits 11..10 = 0b11
        if (ExtractBit(raw, 21) == 1 && ExtractBits(raw, 10, 2) == 0b11) {
            inst.opcode = Opcode::FCSEL_scalar;
            inst.condition = static_cast<Condition>(ExtractBits(raw, 12, 4));
            return inst;
        }

        // Check FCMP: bit 21=1, bits 15..10 = 0b001000
        if (ExtractBit(raw, 21) == 1 && ExtractBits(raw, 10, 6) == 0b001000) {
            inst.opcode = Opcode::FCMP_scalar;
            if (ExtractBit(raw, 3)) inst.rm = 31;
            return inst;
        }

        // Check 1-source scalar FP: bit 21=1, bits 14..10 == 0b10000
        if (ExtractBit(raw, 21) == 1 && ExtractBits(raw, 10, 5) == 0b10000) {
            const u32 op = (ExtractBits(raw, 16, 5) << 1) | ExtractBit(raw, 15);
            switch (op) {
                case 0b000000: inst.opcode = Opcode::FMOV_reg; return inst;
                case 0b000001: inst.opcode = Opcode::FABS_scalar; return inst;
                case 0b000010: inst.opcode = Opcode::FNEG_scalar; return inst;
                case 0b000011: inst.opcode = Opcode::FSQRT_scalar; return inst;
                case 0b000100:
                case 0b000101: inst.opcode = Opcode::FCVT; inst.is_fp_double = !ExtractBit(raw, 22); return inst;
                default: break;
            }
        }

        // Check FMOV immediate: bit 21=1, bits 12..10 = 0b100, bits 9..5 = 0
        if (ExtractBit(raw, 21) == 1 && ExtractBits(raw, 10, 3) == 0b100 && ExtractBits(raw, 5, 5) == 0) {
            inst.opcode = Opcode::FMOV_imm;
            const u32 imm8 = ExtractBits(raw, 13, 8);
            const bool sign = (imm8 >> 7) & 1;
            const u32 exp = ((imm8 >> 4) & 7) ^ 4;
            const u32 mant = imm8 & 0xF;
            inst.fp_imm = (sign ? -1.0 : 1.0) * (1.0 + mant / 16.0) * std::pow(2.0, static_cast<int>(exp) - 3);
            return inst;
        }

        // Check conversions & GP transfers: SCVTF, UCVTF, FCVTZS, FCVTZU, FMOV GP<->FP
        const u32 sf = ExtractBit(raw, 31);
        inst.is_64bit = (sf == 1);
        const u32 rmode = ExtractBits(raw, 19, 2);
        const u32 opcode = ExtractBits(raw, 16, 3);
        if (ExtractBit(raw, 21) == 1 && ExtractBits(raw, 10, 6) == 0) {
            if (opcode == 0b010) { inst.opcode = Opcode::SCVTF; return inst; }
            if (opcode == 0b011) { inst.opcode = Opcode::UCVTF; return inst; }
            if (opcode == 0b000 && rmode == 0b11) { inst.opcode = Opcode::FCVTZS; return inst; }
            if (opcode == 0b001 && rmode == 0b11) { inst.opcode = Opcode::FCVTZU; return inst; }
            if (opcode == 0b110) { inst.opcode = Opcode::FMOV_to_gp; return inst; }
            if (opcode == 0b111) { inst.opcode = Opcode::FMOV_from_gp; return inst; }
        }
    } else {
        // Vector / Advanced SIMD
        const u32 u = ExtractBit(raw, 29);
        const u32 size = ExtractBits(raw, 22, 2);
        inst.vec_size = static_cast<u8>(size);
        inst.is_fp_double = (size == 0b11);

        // 3-same instructions (ADD, SUB, FADD, FSUB, FMUL, AND, ORR, EOR)
        if (ExtractBit(raw, 21) == 1 && ExtractBits(raw, 24, 4) == 0b1110) {
            const u32 opcode = ExtractBits(raw, 11, 5);
            if (!u && opcode == 0b10000) { inst.opcode = Opcode::ADD_vec; return inst; }
            if (u && opcode == 0b10000) { inst.opcode = Opcode::SUB_vec; return inst; }
            // FADD/FSUB (vector) share u==0 and opcode 0b11010 and are told apart by
            // bit 23 (FADD=0, FSUB=1). FMUL (vector) uses opcode 0b11011 (u==1).
            if (opcode == 0b11010) {
                inst.opcode = ExtractBit(raw, 23) ? Opcode::FSUB_vec : Opcode::FADD_vec;
                return inst;
            }
            if (opcode == 0b11011) { inst.opcode = Opcode::FMUL_vec; return inst; }
            // NOTE: AND/ORR/EOR/NOT (vector) all share opcode 0b00011; the 29 "u" bit
            // selects EOR/NOT (u=1) vs AND/ORR (u=0), and bit 23 selects the
            // "not"/second operand: AND(u0,b23=0), ORR(u0,b23=1), EOR(u1,b23=0),
            // NOT/ORN(u1,b23=1).
            if (opcode == 0b00011) {
                const bool o23 = ExtractBit(raw, 23);
                if (u) inst.opcode = o23 ? Opcode::NOT_vec : Opcode::EOR_vec;
                else   inst.opcode = o23 ? Opcode::ORR_vec : Opcode::AND_vec;
                return inst;
            }
        }

        // DUP, INS, UMOV
        const u32 op_ins = ExtractBits(raw, 10, 5);
        const u32 imm5 = ExtractBits(raw, 16, 5);
        if (imm5 & 1) { inst.vec_size = 0; inst.vec_index = static_cast<u8>(imm5 >> 1); }
        else if (imm5 & 2) { inst.vec_size = 1; inst.vec_index = static_cast<u8>(imm5 >> 2); }
        else if (imm5 & 4) { inst.vec_size = 2; inst.vec_index = static_cast<u8>(imm5 >> 3); }
        else if (imm5 & 8) { inst.vec_size = 3; inst.vec_index = static_cast<u8>(imm5 >> 4); }

        if (op_ins == 0b00011) { inst.opcode = Opcode::DUP_gen; return inst; }
        if (op_ins == 0b00111) { inst.opcode = Opcode::INS_gen; return inst; }
        if (op_ins == 0b01011) { inst.opcode = Opcode::SMOV; return inst; }
        if (op_ins == 0b01111) { inst.opcode = Opcode::UMOV; return inst; }
    }

    return inst;
}

} // namespace nemu::core::cpu
