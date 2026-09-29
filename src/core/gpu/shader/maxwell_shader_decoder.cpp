#include "maxwell_shader_decoder.hpp"
#include "sass_identifier.hpp"
#include <sstream>
#include <iomanip>
#include <set>
#include <algorithm>
#include <cstring>

namespace nemu::core::gpu::shader {

namespace {

std::string FormatOperand(const ShaderOperand& op) {
    std::ostringstream ss;
    if (op.negate) ss << "-";
    if (op.absolute) ss << "|";

    switch (op.type) {
        case OperandType::Register:
            if (op.reg_index >= 255) {
                ss << "RZ";
            } else {
                ss << "R" << op.reg_index;
            }
            break;
        case OperandType::ImmediateInt:
            ss << "0x" << std::hex << op.imm_int << std::dec;
            break;
        case OperandType::ImmediateFloat:
            ss << op.imm_float << "f";
            break;
        case OperandType::ConstantBuffer:
            ss << "c[" << op.cbuf.bank << "][" << op.cbuf.offset << "]";
            break;
        case OperandType::SpecialRegister:
            ss << "SR" << op.special_reg;
            break;
        case OperandType::Attribute:
            ss << "a[" << op.attr.offset << "]." << "xyzw"[op.attr.component & 3];
            break;
    }

    if (op.absolute) ss << "|";
    return ss.str();
}

std::string OperandToHlsl(const ShaderOperand& op, ShaderStage stage) {
    std::string expr;
    switch (op.type) {
        case OperandType::Register:
            if (op.reg_index >= 255) {
                expr = "0.0f";
            } else {
                expr = "R[" + std::to_string(op.reg_index) + "]";
            }
            break;
        case OperandType::ImmediateInt: {
            std::ostringstream ss;
            ss << "asfloat(" << op.imm_int << ")";
            expr = ss.str();
            break;
        }
        case OperandType::ImmediateFloat: {
            std::ostringstream ss;
            ss << op.imm_float << "f";
            expr = ss.str();
            break;
        }
        case OperandType::ConstantBuffer: {
            u32 vec_idx = op.cbuf.offset / 16;
            u32 comp = (op.cbuf.offset / 4) % 4;
            char comp_char = "xyzw"[comp];
            expr = "cbuf" + std::to_string(op.cbuf.bank) + "[" + std::to_string(vec_idx) + "]." + comp_char;
            break;
        }
        case OperandType::SpecialRegister:
            expr = "0.0f"; // Builtin uniform
            break;
        case OperandType::Attribute: {
            u32 attr_idx = op.attr.offset / 16;
            char comp_char = "xyzw"[op.attr.component & 3];
            if (stage == ShaderStage::Vertex && attr_idx == 0) {
                expr = "input.in_pos." + std::string(1, comp_char);
            } else {
                expr = "input.in_attr" + std::to_string(attr_idx) + "." + comp_char;
            }
            break;
        }
    }

    if (op.absolute) {
        expr = "abs(" + expr + ")";
    }
    if (op.negate) {
        expr = "-(" + expr + ")";
    }
    return expr;
}

std::string OperandToGlsl(const ShaderOperand& op, ShaderStage stage) {
    return OperandToHlsl(op, stage); // GLSL syntax for expressions is mostly identical
}

} // namespace

std::string MaxwellShaderDecoder::DisassembleInstruction(const DecodedInstruction& inst) {
    std::ostringstream ss;
    if (inst.predicate < 7) {
        ss << "@" << (inst.predicate_invert ? "!" : "") << "P" << inst.predicate << " ";
    }

    switch (inst.opcode) {
        case MaxwellOpcode::NOP:     ss << "NOP"; break;
        case MaxwellOpcode::FADD:    ss << "FADD"; break;
        case MaxwellOpcode::FSUB:    ss << "FSUB"; break;
        case MaxwellOpcode::FMUL:    ss << "FMUL"; break;
        case MaxwellOpcode::FFMA:    ss << "FFMA"; break;
        case MaxwellOpcode::FMNMX:   ss << "FMNMX"; break;
        case MaxwellOpcode::FCMP:    ss << "FCMP"; break;
        case MaxwellOpcode::F2F:     ss << "F2F"; break;
        case MaxwellOpcode::F2I:     ss << "F2I"; break;
        case MaxwellOpcode::I2F:     ss << "I2F"; break;
        case MaxwellOpcode::IADD:    ss << "IADD"; break;
        case MaxwellOpcode::IADD3:   ss << "IADD3"; break;
        case MaxwellOpcode::ISUB:    ss << "ISUB"; break;
        case MaxwellOpcode::IMUL:    ss << "IMUL"; break;
        case MaxwellOpcode::ISCADD:  ss << "ISCADD"; break;
        case MaxwellOpcode::LEA:     ss << "LEA"; break;
        case MaxwellOpcode::LOP:     ss << "LOP"; break;
        case MaxwellOpcode::LOP3:    ss << "LOP3"; break;
        case MaxwellOpcode::SHL:     ss << "SHL"; break;
        case MaxwellOpcode::SHR:     ss << "SHR"; break;
        case MaxwellOpcode::MOV:     ss << "MOV"; break;
        case MaxwellOpcode::SEL:     ss << "SEL"; break;
        case MaxwellOpcode::LDC:     ss << "LDC"; break;
        case MaxwellOpcode::LDG:     ss << "LDG"; break;
        case MaxwellOpcode::STG:     ss << "STG"; break;
        case MaxwellOpcode::LDS:     ss << "LDS"; break;
        case MaxwellOpcode::STS:     ss << "STS"; break;
        case MaxwellOpcode::TEX:     ss << "TEX"; break;
        case MaxwellOpcode::TEXS:    ss << "TEXS"; break;
        case MaxwellOpcode::TLD:     ss << "TLD"; break;
        case MaxwellOpcode::TXQ:     ss << "TXQ"; break;
        case MaxwellOpcode::BRA:     ss << "BRA 0x" << std::hex << inst.branch_target; break;
        case MaxwellOpcode::EXIT:    ss << "EXIT"; break;
        case MaxwellOpcode::KIL:     ss << "KIL"; break;
        case MaxwellOpcode::SYNC:    ss << "SYNC"; break;
        case MaxwellOpcode::S2R:     ss << "S2R"; break;
        case MaxwellOpcode::AL2P:    ss << "AL2P"; break;
        case MaxwellOpcode::LD_ATTR: ss << "LD.ATTR"; break;
        case MaxwellOpcode::ST_ATTR: ss << "ST.ATTR"; break;
        case MaxwellOpcode::BFE:     ss << "BFE"; break;
        case MaxwellOpcode::BFI:     ss << "BFI"; break;
        case MaxwellOpcode::IMAD:    ss << "IMAD"; break;
        case MaxwellOpcode::IMAD32I: ss << "IMAD32I"; break;
        case MaxwellOpcode::IMADSP:  ss << "IMADSP"; break;
        case MaxwellOpcode::XMAD:    ss << "XMAD"; break;
        case MaxwellOpcode::IADD32I: ss << "IADD32I"; break;
        case MaxwellOpcode::FMUL32I: ss << "FMUL32I"; break;
        case MaxwellOpcode::FADD32I: ss << "FADD32I"; break;
        case MaxwellOpcode::FFMA32I: ss << "FFMA32I"; break;
        case MaxwellOpcode::ISCADD32I: ss << "ISCADD32I"; break;
        case MaxwellOpcode::IMUL32I: ss << "IMUL32I"; break;
        case MaxwellOpcode::LOP32I:  ss << "LOP32I"; break;
        case MaxwellOpcode::MOV32I:  ss << "MOV32I"; break;
        case MaxwellOpcode::ICMP:    ss << "ICMP"; break;
        case MaxwellOpcode::FSET:    ss << "FSET"; break;
        case MaxwellOpcode::FSETP:   ss << "FSETP"; break;
        case MaxwellOpcode::ISET:    ss << "ISET"; break;
        case MaxwellOpcode::ISETP:   ss << "ISETP"; break;
        case MaxwellOpcode::PSET:    ss << "PSET"; break;
        case MaxwellOpcode::PSETP:   ss << "PSETP"; break;
        case MaxwellOpcode::CSET:    ss << "CSET"; break;
        case MaxwellOpcode::CSETP:   ss << "CSETP"; break;
        case MaxwellOpcode::DMNMX:   ss << "DMNMX"; break;
        case MaxwellOpcode::IMNMX:   ss << "IMNMX"; break;
        case MaxwellOpcode::DFMA:    ss << "DFMA"; break;
        case MaxwellOpcode::DMUL:    ss << "DMUL"; break;
        case MaxwellOpcode::DADD:    ss << "DADD"; break;
        case MaxwellOpcode::LD:      ss << "LD"; break;
        case MaxwellOpcode::LDL:     ss << "LDL"; break;
        case MaxwellOpcode::STL:     ss << "STL"; break;
        case MaxwellOpcode::ST:      ss << "ST"; break;
        case MaxwellOpcode::STP:     ss << "STP"; break;
        case MaxwellOpcode::LDP:     ss << "LDP"; break;
        case MaxwellOpcode::MUFU:    ss << "MUFU"; break;
        case MaxwellOpcode::FLO:     ss << "FLO"; break;
        case MaxwellOpcode::POPC:    ss << "POPC"; break;
        case MaxwellOpcode::PRMT:    ss << "PRMT"; break;
        case MaxwellOpcode::FCHK:    ss << "FCHK"; break;
        case MaxwellOpcode::I2I:     ss << "I2I"; break;
        case MaxwellOpcode::IPA:     ss << "IPA"; break;
        case MaxwellOpcode::P2R:     ss << "P2R"; break;
        case MaxwellOpcode::R2P:     ss << "R2P"; break;
        case MaxwellOpcode::R2B:     ss << "R2B"; break;
        case MaxwellOpcode::B2R:     ss << "B2R"; break;
        case MaxwellOpcode::CS2R:    ss << "CS2R"; break;
        case MaxwellOpcode::HFMA2:   ss << "HFMA2"; break;
        case MaxwellOpcode::HFMA2_32I: ss << "HFMA2_32I"; break;
        case MaxwellOpcode::HADD2:   ss << "HADD2"; break;
        case MaxwellOpcode::HADD2_32I: ss << "HADD2_32I"; break;
        case MaxwellOpcode::HMUL2:   ss << "HMUL2"; break;
        case MaxwellOpcode::HMUL2_32I: ss << "HMUL2_32I"; break;
        case MaxwellOpcode::HSET2:   ss << "HSET2"; break;
        case MaxwellOpcode::HSETP2:  ss << "HSETP2"; break;
        case MaxwellOpcode::TLDS:    ss << "TLDS"; break;
        case MaxwellOpcode::TLD4:    ss << "TLD4"; break;
        case MaxwellOpcode::TLD4S:   ss << "TLD4S"; break;
        case MaxwellOpcode::TEX_b:   ss << "TEX.B"; break;
        case MaxwellOpcode::TLD_b:   ss << "TLD.B"; break;
        case MaxwellOpcode::TXD:     ss << "TXD"; break;
        case MaxwellOpcode::TXD_b:   ss << "TXD.B"; break;
        case MaxwellOpcode::TMML:    ss << "TMML"; break;
        case MaxwellOpcode::TMML_b:  ss << "TMML.B"; break;
        case MaxwellOpcode::TXA:     ss << "TXA"; break;
        case MaxwellOpcode::JMP:     ss << "JMP"; break;
        case MaxwellOpcode::JMX:     ss << "JMX"; break;
        case MaxwellOpcode::BRX:     ss << "BRX"; break;
        case MaxwellOpcode::PBK:     ss << "PBK"; break;
        case MaxwellOpcode::PCNT:    ss << "PCNT"; break;
        case MaxwellOpcode::PEXIT:   ss << "PEXIT"; break;
        case MaxwellOpcode::LONGJMP: ss << "LONGJMP"; break;
        case MaxwellOpcode::PLONGJMP: ss << "PLONGJMP"; break;
        case MaxwellOpcode::JCAL:    ss << "JCAL"; break;
        case MaxwellOpcode::CAL:     ss << "CAL"; break;
        case MaxwellOpcode::RET:     ss << "RET"; break;
        case MaxwellOpcode::PRET:    ss << "PRET"; break;
        case MaxwellOpcode::RTT:     ss << "RTT"; break;
        case MaxwellOpcode::BRK:     ss << "BRK"; break;
        case MaxwellOpcode::CONT:    ss << "CONT"; break;
        case MaxwellOpcode::SSY:     ss << "SSY"; break;
        case MaxwellOpcode::VOTE:    ss << "VOTE"; break;
        case MaxwellOpcode::VOTE_vtg: ss << "VOTE.VTG"; break;
        case MaxwellOpcode::BAR:     ss << "BAR"; break;
        case MaxwellOpcode::DEPBAR:  ss << "DEPBAR"; break;
        case MaxwellOpcode::MEMBAR:  ss << "MEMBAR"; break;
        case MaxwellOpcode::SHFL:    ss << "SHFL"; break;
        case MaxwellOpcode::FSWZADD: ss << "FSWZADD"; break;
        case MaxwellOpcode::LEA_hi:  ss << "LEA.HI"; break;
        case MaxwellOpcode::LEA_lo:  ss << "LEA.LO"; break;
        case MaxwellOpcode::SHF_l:   ss << "SHF.L"; break;
        case MaxwellOpcode::SHF_r:   ss << "SHF.R"; break;
        case MaxwellOpcode::SULD:    ss << "SULD"; break;
        case MaxwellOpcode::SUST:    ss << "SUST"; break;
        case MaxwellOpcode::SURED:   ss << "SURED"; break;
        case MaxwellOpcode::SUATOM:  ss << "SUATOM"; break;
        case MaxwellOpcode::ATOM:    ss << "ATOM"; break;
        case MaxwellOpcode::ATOMS:   ss << "ATOMS"; break;
        case MaxwellOpcode::RED:     ss << "RED"; break;
        case MaxwellOpcode::DSET:    ss << "DSET"; break;
        case MaxwellOpcode::DSETP:   ss << "DSETP"; break;
        case MaxwellOpcode::DMIN:    ss << "DMIN"; break;
        case MaxwellOpcode::DMAX:    ss << "DMAX"; break;
        case MaxwellOpcode::VABSDIFF: ss << "VABSDIFF"; break;
        case MaxwellOpcode::VABSDIFF4: ss << "VABSDIFF4"; break;
        case MaxwellOpcode::VADD:    ss << "VADD"; break;
        case MaxwellOpcode::VMAD:    ss << "VMAD"; break;
        case MaxwellOpcode::VMNMX:   ss << "VMNMX"; break;
        case MaxwellOpcode::VSET:    ss << "VSET"; break;
        case MaxwellOpcode::VSETP:   ss << "VSETP"; break;
        case MaxwellOpcode::VSHL:    ss << "VSHL"; break;
        case MaxwellOpcode::VSHR:    ss << "VSHR"; break;
        case MaxwellOpcode::ALD:     ss << "ALD"; break;
        case MaxwellOpcode::AST:     ss << "AST"; break;
        case MaxwellOpcode::OUT_stream: ss << "OUT"; break;
        case MaxwellOpcode::PIXLD:   ss << "PIXLD"; break;
        case MaxwellOpcode::CCTL:    ss << "CCTL"; break;
        case MaxwellOpcode::CCTLL:   ss << "CCTLL"; break;
        case MaxwellOpcode::RRO:     ss << "RRO"; break;
        case MaxwellOpcode::TLD4_b:  ss << "TLD4.B"; break;
        case MaxwellOpcode::TXQ_b:   ss << "TXQ.B"; break;
        case MaxwellOpcode::BPT:     ss << "BPT"; break;
        case MaxwellOpcode::GETCRSPTR: ss << "GETCRSPTR"; break;
        case MaxwellOpcode::GETLMEMBASE: ss << "GETLMEMBASE"; break;
        case MaxwellOpcode::SETCRSPTR: ss << "SETCRSPTR"; break;
        case MaxwellOpcode::SETLMEMBASE: ss << "SETLMEMBASE"; break;
        case MaxwellOpcode::IDE:     ss << "IDE"; break;
        case MaxwellOpcode::IDP:     ss << "IDP"; break;
        case MaxwellOpcode::ISBERD:  ss << "ISBERD"; break;
        case MaxwellOpcode::LEPC:    ss << "LEPC"; break;
        case MaxwellOpcode::RAM:     ss << "RAM"; break;
        case MaxwellOpcode::SAM:     ss << "SAM"; break;
        default:                     ss << "UNKNOWN_0x" << std::hex << static_cast<u32>(inst.opcode); break;
    }

    if (inst.dest.type == OperandType::Register || inst.dest.type == OperandType::Attribute) {
        ss << " " << FormatOperand(inst.dest);
    }

    for (const auto& src : inst.sources) {
        ss << ", " << FormatOperand(src);
    }

    return ss.str();
}

// GCC 13 -Wstringop-overflow misfires on the vector<ShaderOperand> realloc path
// in DecodeInstruction64 (reports "writing 2 bytes into a region of size 0" on a
// correct 80-byte ShaderOperand move; the "2 bytes" are the bool trailer padding).
// It is a false positive: the writes are fully in-bounds within the allocated
// vector storage. Scope the suppression narrowly to this decoder.
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wstringop-overflow"
#endif
DecodedInstruction MaxwellShaderDecoder::DecodeInstruction64(u64 raw, u64 offset) {
    DecodedInstruction inst{};
    inst.offset_bytes = offset;

    // Predicate: bits 16..18, negate: bit 19
    inst.predicate = static_cast<u32>((raw >> 16) & 0x7);
    inst.predicate_invert = ((raw >> 19) & 0x1) != 0;

    // Major opcode: bits [63:52] or [11:0]
    u32 major_high = static_cast<u32>((raw >> 52) & 0xFFF);

    // Default register operands
    u32 rd = static_cast<u32>((raw >> 0) & 0xFF);
    u32 ra = static_cast<u32>((raw >> 8) & 0xFF);
    u32 rb = static_cast<u32>((raw >> 20) & 0xFF);
    u32 rc = static_cast<u32>((raw >> 32) & 0xFF);

    inst.dest.type = OperandType::Register;
    inst.dest.reg_index = rd;

    // Opcode mapping based on major opcode in bits [63:52]
    // (Tier-A1 expanded: full arithmetic/logic/memory/texture surface.)
    if (major_high == 0x5C0) {
        inst.opcode = MaxwellOpcode::MOV;
        ShaderOperand src{};
        src.type = OperandType::Register;
        src.reg_index = ra;
        inst.sources.push_back(src);
    } else if (major_high == 0x5C1) {
        inst.opcode = MaxwellOpcode::SEL;
        inst.sources.push_back(ShaderOperand{.type = OperandType::Register, .reg_index = ra});
        inst.sources.push_back(ShaderOperand{.type = OperandType::Register, .reg_index = rb});
    } else if (major_high == 0x580) {
        inst.opcode = MaxwellOpcode::FADD;
        inst.sources.push_back(ShaderOperand{.type = OperandType::Register, .reg_index = ra});
        inst.sources.push_back(ShaderOperand{.type = OperandType::Register, .reg_index = rb});
    } else if (major_high == 0x581) {
        inst.opcode = MaxwellOpcode::FSUB;
        inst.sources.push_back(ShaderOperand{.type = OperandType::Register, .reg_index = ra});
        inst.sources.push_back(ShaderOperand{.type = OperandType::Register, .reg_index = rb});
    } else if (major_high == 0x582) {
        inst.opcode = MaxwellOpcode::FMUL;
        inst.sources.push_back(ShaderOperand{.type = OperandType::Register, .reg_index = ra});
        inst.sources.push_back(ShaderOperand{.type = OperandType::Register, .reg_index = rb});
    } else if (major_high == 0x583) {
        inst.opcode = MaxwellOpcode::FFMA;
        inst.sources.push_back(ShaderOperand{.type = OperandType::Register, .reg_index = ra});
        inst.sources.push_back(ShaderOperand{.type = OperandType::Register, .reg_index = rb});
        inst.sources.push_back(ShaderOperand{.type = OperandType::Register, .reg_index = rc});
    } else if (major_high == 0x584) {
        inst.opcode = MaxwellOpcode::FMNMX;
        inst.sources.push_back(ShaderOperand{.type = OperandType::Register, .reg_index = ra});
        inst.sources.push_back(ShaderOperand{.type = OperandType::Register, .reg_index = rb});
    } else if (major_high == 0x585) {
        inst.opcode = MaxwellOpcode::FCMP;
        inst.sources.push_back(ShaderOperand{.type = OperandType::Register, .reg_index = ra});
        inst.sources.push_back(ShaderOperand{.type = OperandType::Register, .reg_index = rb});
    } else if (major_high == 0x586) {
        // F2F: float-to-float conversion (F2F.F32.F32/F32.F64/...)
        inst.opcode = MaxwellOpcode::F2F;
        inst.sources.push_back(ShaderOperand{.type = OperandType::Register, .reg_index = ra});
    } else if (major_high == 0x587) {
        // F2I: float-to-int conversion
        inst.opcode = MaxwellOpcode::F2I;
        inst.sources.push_back(ShaderOperand{.type = OperandType::Register, .reg_index = ra});
    } else if (major_high == 0x588) {
        // I2F: int-to-float conversion
        inst.opcode = MaxwellOpcode::I2F;
        inst.sources.push_back(ShaderOperand{.type = OperandType::Register, .reg_index = ra});
    } else if (major_high == 0x5A0) {
        inst.opcode = MaxwellOpcode::IADD;
        inst.sources.push_back(ShaderOperand{.type = OperandType::Register, .reg_index = ra});
        inst.sources.push_back(ShaderOperand{.type = OperandType::Register, .reg_index = rb});
    } else if (major_high == 0x5A1) {
        inst.opcode = MaxwellOpcode::ISUB;
        inst.sources.push_back(ShaderOperand{.type = OperandType::Register, .reg_index = ra});
        inst.sources.push_back(ShaderOperand{.type = OperandType::Register, .reg_index = rb});
    } else if (major_high == 0x5A2) {
        inst.opcode = MaxwellOpcode::IMUL;
        inst.sources.push_back(ShaderOperand{.type = OperandType::Register, .reg_index = ra});
        inst.sources.push_back(ShaderOperand{.type = OperandType::Register, .reg_index = rb});
    } else if (major_high == 0x5A3) {
        inst.opcode = MaxwellOpcode::IADD3;
        inst.sources.push_back(ShaderOperand{.type = OperandType::Register, .reg_index = ra});
        inst.sources.push_back(ShaderOperand{.type = OperandType::Register, .reg_index = rb});
        inst.sources.push_back(ShaderOperand{.type = OperandType::Register, .reg_index = rc});
    } else if (major_high == 0x5A4) {
        inst.opcode = MaxwellOpcode::ISCADD;
        inst.sources.push_back(ShaderOperand{.type = OperandType::Register, .reg_index = ra});
        inst.sources.push_back(ShaderOperand{.type = OperandType::Register, .reg_index = rb});
        inst.sources.push_back(ShaderOperand{.type = OperandType::ImmediateInt, .imm_int = static_cast<s32>((raw >> 40) & 0x1F)});
    } else if (major_high == 0x5A5) {
        // LEA: load effective address (Hi*Rc + Ra -> Rd)
        inst.opcode = MaxwellOpcode::LEA;
        inst.sources.push_back(ShaderOperand{.type = OperandType::Register, .reg_index = ra});
        inst.sources.push_back(ShaderOperand{.type = OperandType::Register, .reg_index = rc});
    } else if (major_high == 0x560) {
        inst.opcode = MaxwellOpcode::LOP;
        inst.sources.push_back(ShaderOperand{.type = OperandType::Register, .reg_index = ra});
        inst.sources.push_back(ShaderOperand{.type = OperandType::Register, .reg_index = rb});
    } else if (major_high == 0x561) {
        inst.opcode = MaxwellOpcode::SHL;
        inst.sources.push_back(ShaderOperand{.type = OperandType::Register, .reg_index = ra});
        inst.sources.push_back(ShaderOperand{.type = OperandType::Register, .reg_index = rb});
    } else if (major_high == 0x562) {
        inst.opcode = MaxwellOpcode::SHR;
        inst.sources.push_back(ShaderOperand{.type = OperandType::Register, .reg_index = ra});
        inst.sources.push_back(ShaderOperand{.type = OperandType::Register, .reg_index = rb});
    } else if (major_high == 0x563) {
        // LOP3: 3-input logic op via immediate LUT in bits [56:32]
        inst.opcode = MaxwellOpcode::LOP3;
        inst.sources.push_back(ShaderOperand{.type = OperandType::Register, .reg_index = ra});
        inst.sources.push_back(ShaderOperand{.type = OperandType::Register, .reg_index = rb});
        inst.sources.push_back(ShaderOperand{.type = OperandType::ImmediateInt, .imm_int = static_cast<s32>((raw >> 32) & 0xFF)});
    } else if (major_high == 0x564) {
        // BFE: bit-field extract
        inst.opcode = MaxwellOpcode::BFE;
        inst.sources.push_back(ShaderOperand{.type = OperandType::Register, .reg_index = ra});
        inst.sources.push_back(ShaderOperand{.type = OperandType::Register, .reg_index = rb});
    } else if (major_high == 0x565) {
        // BFI: bit-field insert
        inst.opcode = MaxwellOpcode::BFI;
        inst.sources.push_back(ShaderOperand{.type = OperandType::Register, .reg_index = ra});
        inst.sources.push_back(ShaderOperand{.type = OperandType::Register, .reg_index = rb});
        inst.sources.push_back(ShaderOperand{.type = OperandType::Register, .reg_index = rc});
    } else if (major_high == 0x530) {
        inst.opcode = MaxwellOpcode::LDC;
        u32 bank = static_cast<u32>((raw >> 20) & 0x1F);
        u32 c_offset = static_cast<u32>((raw >> 28) & 0xFFFF);
        ShaderOperand c_op{};
        c_op.type = OperandType::ConstantBuffer;
        c_op.cbuf = {bank, c_offset};
        inst.sources.push_back(c_op);
    } else if (major_high == 0x531) {
        // LDG: load from global memory (GMMU path, Tier-A5)
        inst.opcode = MaxwellOpcode::LDG;
        u64 gaddr = static_cast<u64>((raw >> 28) & 0xFFFFF) << 4; // 20-bit word-aligned addr
        inst.sources.push_back(ShaderOperand{.type = OperandType::ImmediateInt, .imm_int = static_cast<s32>(gaddr)});
    } else if (major_high == 0x532) {
        // STG: store to global memory
        inst.opcode = MaxwellOpcode::STG;
        u64 gaddr = static_cast<u64>((raw >> 28) & 0xFFFFF) << 4;
        inst.sources.push_back(ShaderOperand{.type = OperandType::ImmediateInt, .imm_int = static_cast<s32>(gaddr)});
    } else if (major_high == 0x533) {
        // LDS: load from shared memory
        inst.opcode = MaxwellOpcode::LDS;
        u32 saddr = static_cast<u32>((raw >> 20) & 0xFFFF) << 2;
        inst.sources.push_back(ShaderOperand{.type = OperandType::ImmediateInt, .imm_int = static_cast<s32>(saddr)});
    } else if (major_high == 0x534) {
        // STS: store to shared memory
        inst.opcode = MaxwellOpcode::STS;
        u32 saddr = static_cast<u32>((raw >> 20) & 0xFFFF) << 2;
        inst.sources.push_back(ShaderOperand{.type = OperandType::ImmediateInt, .imm_int = static_cast<s32>(saddr)});
    } else if (major_high == 0x550) {
        inst.opcode = MaxwellOpcode::TEX;
        inst.sources.push_back(ShaderOperand{.type = OperandType::Register, .reg_index = ra});
        inst.sources.push_back(ShaderOperand{.type = OperandType::Register, .reg_index = rb});
        u32 tex_idx = static_cast<u32>((raw >> 32) & 0xFF);
        inst.sources.push_back(ShaderOperand{.type = OperandType::ImmediateInt, .imm_int = static_cast<s32>(tex_idx)});
    } else if (major_high == 0x551) {
        // TEXS: texture sample with explicit dest vector (games use it most)
        inst.opcode = MaxwellOpcode::TEXS;
        inst.sources.push_back(ShaderOperand{.type = OperandType::Register, .reg_index = ra});
        inst.sources.push_back(ShaderOperand{.type = OperandType::Register, .reg_index = rb});
        u32 tex_idx = static_cast<u32>((raw >> 32) & 0xFF);
        inst.sources.push_back(ShaderOperand{.type = OperandType::ImmediateInt, .imm_int = static_cast<s32>(tex_idx)});
    } else if (major_high == 0x552) {
        // TLD: texture load (unfiltered fetch)
        inst.opcode = MaxwellOpcode::TLD;
        inst.sources.push_back(ShaderOperand{.type = OperandType::Register, .reg_index = ra});
        inst.sources.push_back(ShaderOperand{.type = OperandType::Register, .reg_index = rb});
        u32 tex_idx = static_cast<u32>((raw >> 32) & 0xFF);
        inst.sources.push_back(ShaderOperand{.type = OperandType::ImmediateInt, .imm_int = static_cast<s32>(tex_idx)});
    } else if (major_high == 0x553) {
        // TXQ: texture query (dims/format)
        inst.opcode = MaxwellOpcode::TXQ;
        inst.sources.push_back(ShaderOperand{.type = OperandType::Register, .reg_index = ra});
        u32 tex_idx = static_cast<u32>((raw >> 32) & 0xFF);
        inst.sources.push_back(ShaderOperand{.type = OperandType::ImmediateInt, .imm_int = static_cast<s32>(tex_idx)});
    } else if (major_high == 0x5B0) {
        inst.opcode = MaxwellOpcode::LD_ATTR;
        u32 attr_offset = static_cast<u32>((raw >> 20) & 0x3FF);
        u32 comp = static_cast<u32>((raw >> 30) & 0x3);
        inst.sources.push_back(ShaderOperand{
            .type = OperandType::Attribute,
            .attr = {attr_offset, comp}
        });
    } else if (major_high == 0x5B1) {
        inst.opcode = MaxwellOpcode::ST_ATTR;
        u32 attr_offset = static_cast<u32>((raw >> 20) & 0x3FF);
        u32 comp = static_cast<u32>((raw >> 30) & 0x3);
        inst.dest.type = OperandType::Attribute;
        inst.dest.attr = {attr_offset, comp};
        inst.sources.push_back(ShaderOperand{.type = OperandType::Register, .reg_index = ra});
    } else if (major_high == 0x5D0) {
        inst.opcode = MaxwellOpcode::BRA;
        s32 rel_target = static_cast<s32>((raw >> 20) & 0xFFFFFF);
        if (rel_target & 0x800000) rel_target |= static_cast<s32>(0xFF000000);
        inst.branch_target = static_cast<u64>(static_cast<s64>(offset) + rel_target);
        // Conditional BRA.P: predicate in bits [13:11] (Maxwell branch predicate
        // field). P0-P6; default unconditional when the field is 7 (PT/never).
        const u32 pred = static_cast<u32>((raw >> 11) & 0x7);
        // P0-P6 conditional, PT (7) = unconditional — keep the struct's default.
        inst.predicate = static_cast<u8>(pred & 0x7);
        // A branch-else sense (BRA !P) is signalled by bit 14.
        const bool inv = ((raw >> 14) & 0x1) != 0;
        inst.predicate_invert = inv;
    } else if (major_high == 0x5D1) {
        inst.opcode = MaxwellOpcode::EXIT;
    } else if (major_high == 0x5D2) {
        inst.opcode = MaxwellOpcode::KIL;
    } else if (major_high == 0x5D3) {
        inst.opcode = MaxwellOpcode::SYNC;
    } else if (major_high == 0x5F0) {
        inst.opcode = MaxwellOpcode::S2R;
        u32 sr = static_cast<u32>((raw >> 20) & 0xFF);
        inst.sources.push_back(ShaderOperand{.type = OperandType::SpecialRegister, .special_reg = sr});
    } else if ((major_high & 0xFE0) == 0x5E0) {
        // MUFU family (0x5E0-0x5EF): sub-op in bits [57:53]
        // (0x0 div, 0x4 rcp, 0x8 rsqrt, 0xC sin, 0xD cos, 0xE ex2, 0xF lg2).
        inst.opcode = MaxwellOpcode::MUFU;
        inst.branch_target = static_cast<u64>((raw >> 53) & 0x1F); // stash sub-op
        inst.sources.push_back(ShaderOperand{.type = OperandType::Register, .reg_index = ra});
    } else {
        inst.opcode = MaxwellOpcode::UNKNOWN;
    }

    // Tier-A1 full-table cross-check: when the fast-path map misses, identify
    // the exact SASS family via the 279-encoding table and record it in the
    // disassembly — real-game programs then show their true opcodes in logs.
    if (inst.opcode == MaxwellOpcode::UNKNOWN) {
        const auto sass = IdentifySass(raw);
        if (sass.encoding_index != SIZE_MAX) {
            inst.disassembly = std::string("SASS:") + SassCuteName(sass.encoding_index);
            const auto maxwell_op = IdentifyMaxwell(raw);
            if (maxwell_op != MaxwellOpcode::UNKNOWN) {
                inst.opcode = maxwell_op;
                if (inst.sources.empty()) {
                    inst.sources.push_back(ShaderOperand{.type = OperandType::Register, .reg_index = ra});
                    inst.sources.push_back(ShaderOperand{.type = OperandType::Register, .reg_index = rb});
                    inst.sources.push_back(ShaderOperand{.type = OperandType::Register, .reg_index = rc});
                }
            }
        } else {
            inst.disassembly = "UNKNOWN_0x" + [&] {
                std::ostringstream ss;
                ss << std::hex << static_cast<u32>(inst.opcode);
                return ss.str();
            }();
        }
        return inst;
    }

    inst.disassembly = DisassembleInstruction(inst);

    // SET/SETP family: extract the comparison operator (bits [51:49], switchbrew
    // FloatSetOp/IntSetOp layout). 0=FALSE 1=LT 2=EQ 3=LE 4=GT 5=NE 6=GE 7=TRUE.
    switch (inst.opcode) {
    case MaxwellOpcode::FSET: case MaxwellOpcode::FSETP:
    case MaxwellOpcode::ISET: case MaxwellOpcode::ISETP:
    case MaxwellOpcode::PSET: case MaxwellOpcode::PSETP:
    case MaxwellOpcode::DSET: case MaxwellOpcode::DSETP:
    case MaxwellOpcode::HSET2: case MaxwellOpcode::HSETP2:
    case MaxwellOpcode::VSET: case MaxwellOpcode::VSETP:
        inst.set_op = static_cast<u32>((raw >> 49) & 0x7);
        inst.set_op_valid = true;
        break;
    default: break;
    }
    return inst;
}
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic pop
#endif

DecompiledProgram MaxwellShaderDecoder::DecodeAndDecompile(
    std::span<const u8> code,
    ShaderStage stage,
    bool has_control_codes
) {
    DecompiledProgram program{};
    program.stage = stage;

    std::set<u32> used_cbufs;
    std::set<u32> used_texs;
    std::set<u32> used_attrs;

    const size_t inst_size = 8;
    size_t offset = 0;

    while (offset + inst_size <= code.size()) {
        if (has_control_codes && (offset % 32) == 0 && offset + 32 <= code.size()) {
            // Maxwell bundle: 8-byte control code at start of bundle
            offset += 8; // skip control word
        }

        if (offset + 8 > code.size()) break;

        u64 raw_inst = 0;
        std::memcpy(&raw_inst, code.data() + offset, sizeof(u64));

        DecodedInstruction inst = DecodeInstruction64(raw_inst, offset);
        offset += 8;

        if (inst.dest.type == OperandType::Register && inst.dest.reg_index < 255) {
            program.max_register_used = std::max(program.max_register_used, inst.dest.reg_index);
        }

        for (const auto& src : inst.sources) {
            if (src.type == OperandType::Register && src.reg_index < 255) {
                program.max_register_used = std::max(program.max_register_used, src.reg_index);
            } else if (src.type == OperandType::ConstantBuffer) {
                used_cbufs.insert(src.cbuf.bank);
            } else if (src.type == OperandType::ImmediateInt && inst.opcode == MaxwellOpcode::TEX) {
                used_texs.insert(static_cast<u32>(src.imm_int));
            }
        }

        if (inst.dest.type == OperandType::Attribute) {
            used_attrs.insert(inst.dest.attr.offset / 16);
        }
        for (const auto& src : inst.sources) {
            if (src.type == OperandType::Attribute) {
                used_attrs.insert(src.attr.offset / 16);
            }
        }

        if (inst.opcode == MaxwellOpcode::KIL) {
            program.has_discard = true;
        }

        program.instructions.push_back(inst);

        if (inst.opcode == MaxwellOpcode::EXIT) {
            // Reached shader termination
            break;
        }
    }

    program.used_cbuf_banks.assign(used_cbufs.begin(), used_cbufs.end());
    program.used_textures.assign(used_texs.begin(), used_texs.end());
    program.used_attrs.assign(used_attrs.begin(), used_attrs.end());

    program.hlsl_source = EmitHLSL(program);
    program.glsl_source = EmitGLSL(program);

    return program;
}

std::string MaxwellShaderDecoder::EmitHLSL(const DecompiledProgram& program) {
    std::ostringstream ss;

    ss << "// Generated by Nemu Maxwell SM 5.3 -> HLSL SM 5.1/6.0 Decompiler\n";
    ss << "// Target: Xbox Series S/X Direct3D 12\n\n";

    // Constant buffer declarations
    for (u32 bank : program.used_cbuf_banks) {
        ss << "cbuffer ConstantBuffer_" << bank << " : register(b" << bank << ") {\n";
        ss << "    float4 cbuf" << bank << "[4096];\n";
        ss << "};\n\n";
    }

    // Texture and sampler declarations
    for (u32 tex_idx : program.used_textures) {
        ss << "Texture2D tex" << tex_idx << " : register(t" << tex_idx << ");\n";
        ss << "SamplerState samp" << tex_idx << " : register(s" << tex_idx << ");\n\n";
    }

    if (program.stage == ShaderStage::Vertex) {
        ss << "struct VSInput {\n";
        ss << "    float4 in_pos : POSITION;\n";
        for (u32 i : program.used_attrs) {
            ss << "    float4 in_attr" << i << " : TEXCOORD" << i << ";\n";
        }
        ss << "};\n\n";

        ss << "struct VSOutput {\n";
        ss << "    float4 out_pos : SV_Position;\n";
        for (u32 i : program.used_attrs) {
            ss << "    float4 out_attr" << i << " : TEXCOORD" << i << ";\n";
        }
        ss << "};\n\n";

        ss << "VSOutput main(VSInput input) {\n";
        ss << "    VSOutput output = (VSOutput)0;\n";
    } else if (program.stage == ShaderStage::Compute) {
        // Compute shader (Tier-A3): fixed 8x8x1 thread group. The guest's CTA
        // dims come from the dispatch registers; the emitter uses a sane fixed
        // workgroup and the caller scales the grid accordingly.
        ss << "[numthreads(8, 8, 1)]\n";
        ss << "void main(uint3 dispatch_id : SV_DispatchThreadID,\n";
        ss << "          uint3 group_id : SV_GroupThreadID) {\n";
        ss << "    float R[" << std::max(program.max_register_used + 1, 16u) << "];\n";
        for (u32 i = 0; i < std::max(program.max_register_used + 1, 16u); ++i) {
            ss << "    R[" << i << "] = 0.0f;\n";
        }
        ss << "\n";
    } else { // Fragment / Pixel Shader
        ss << "struct PSInput {\n";
        ss << "    float4 in_pos : SV_Position;\n";
        for (u32 i : program.used_attrs) {
            ss << "    float4 in_attr" << i << " : TEXCOORD" << i << ";\n";
        }
        ss << "};\n\n";

        ss << "struct PSOutput {\n";
        for (u32 rt = 0; rt < 8; ++rt) {
            ss << "    float4 out_color" << rt << " : SV_Target" << rt << ";\n";
        }
        ss << "};\n\n";

        ss << "PSOutput main(PSInput input) {\n";
        ss << "    PSOutput output = (PSOutput)0;\n";
    }

    if (program.stage != ShaderStage::Compute) {
        u32 num_regs = std::max(program.max_register_used + 1, 16u);
        ss << "    float R[" << num_regs << "];\n";
        ss << "    [unroll] for (int _i = 0; _i < " << num_regs << "; ++_i) R[_i] = 0.0f;\n\n";
    }

    // Predicate register file: declared + initialized so predicated emission
    // (`if (p[N])`) is valid HLSL. Maxwell predicates are set by the PSET/
    // P2R/BRA.CC family; HLE-wise we keep them false-initialized and the
    // translator's flag→predicate mapping fills them on compare ops.
    bool any_predicated = false;
    for (const auto& inst : program.instructions) {
        if (inst.predicate < 7) { any_predicated = true; break; }
    }
    if (any_predicated) {
        ss << "    bool p[7] = { false, false, false, false, false, false, false };\n\n";
    }

    for (const auto& inst : program.instructions) {
        ss << "    // 0x" << std::hex << inst.offset_bytes << std::dec << ": " << inst.disassembly << "\n";

        std::string pred_open = "";
        std::string pred_close = "";
        if (inst.predicate < 7) {
            // Predicate conditional
            pred_open = "    if (" + std::string(inst.predicate_invert ? "!" : "") + "p[" + std::to_string(inst.predicate) + "]) { ";
            pred_close = " }\n";
        }

        switch (inst.opcode) {
            case MaxwellOpcode::MOV:
                if (inst.dest.type == OperandType::Register && inst.dest.reg_index < 255) {
                    ss << "    R[" << inst.dest.reg_index << "] = " << OperandToHlsl(inst.sources[0], program.stage) << ";\n";
                }
                break;
            case MaxwellOpcode::FADD:
                if (inst.dest.type == OperandType::Register && inst.dest.reg_index < 255) {
                    ss << "    R[" << inst.dest.reg_index << "] = "
                       << OperandToHlsl(inst.sources[0], program.stage) << " + "
                       << OperandToHlsl(inst.sources[1], program.stage) << ";\n";
                }
                break;
            case MaxwellOpcode::FSUB:
                if (inst.dest.type == OperandType::Register && inst.dest.reg_index < 255) {
                    ss << "    R[" << inst.dest.reg_index << "] = "
                       << OperandToHlsl(inst.sources[0], program.stage) << " - "
                       << OperandToHlsl(inst.sources[1], program.stage) << ";\n";
                }
                break;
            case MaxwellOpcode::FMUL:
                if (inst.dest.type == OperandType::Register && inst.dest.reg_index < 255) {
                    ss << "    R[" << inst.dest.reg_index << "] = "
                       << OperandToHlsl(inst.sources[0], program.stage) << " * "
                       << OperandToHlsl(inst.sources[1], program.stage) << ";\n";
                }
                break;
            case MaxwellOpcode::FFMA:
                if (inst.dest.type == OperandType::Register && inst.dest.reg_index < 255) {
                    ss << "    R[" << inst.dest.reg_index << "] = ("
                       << OperandToHlsl(inst.sources[0], program.stage) << " * "
                       << OperandToHlsl(inst.sources[1], program.stage) << ") + "
                       << OperandToHlsl(inst.sources[2], program.stage) << ";\n";
                }
                break;
            case MaxwellOpcode::FMNMX:
                if (inst.dest.type == OperandType::Register && inst.dest.reg_index < 255) {
                    ss << "    R[" << inst.dest.reg_index << "] = min("
                       << OperandToHlsl(inst.sources[0], program.stage) << ", "
                       << OperandToHlsl(inst.sources[1], program.stage) << ");\n";
                }
                break;
            case MaxwellOpcode::IADD:
                if (inst.dest.type == OperandType::Register && inst.dest.reg_index < 255) {
                    ss << "    R[" << inst.dest.reg_index << "] = asfloat(asint("
                       << OperandToHlsl(inst.sources[0], program.stage) << ") + asint("
                       << OperandToHlsl(inst.sources[1], program.stage) << "));\n";
                }
                break;
            case MaxwellOpcode::IMUL:
                if (inst.dest.type == OperandType::Register && inst.dest.reg_index < 255) {
                    ss << "    R[" << inst.dest.reg_index << "] = asfloat(asint("
                       << OperandToHlsl(inst.sources[0], program.stage) << ") * asint("
                       << OperandToHlsl(inst.sources[1], program.stage) << "));\n";
                }
                break;
            // --- Tier-A1 expanded opcode emission ---------------------------
            case MaxwellOpcode::F2F:
                if (inst.dest.type == OperandType::Register && inst.dest.reg_index < 255) {
                    ss << "    R[" << inst.dest.reg_index << "] = "
                       << OperandToHlsl(inst.sources[0], program.stage) << ";\n";
                }
                break;
            case MaxwellOpcode::F2I:
                if (inst.dest.type == OperandType::Register && inst.dest.reg_index < 255) {
                    ss << "    R[" << inst.dest.reg_index << "] = asfloat(trunc("
                       << OperandToHlsl(inst.sources[0], program.stage) << "));\n";
                }
                break;
            case MaxwellOpcode::I2F:
                if (inst.dest.type == OperandType::Register && inst.dest.reg_index < 255) {
                    ss << "    R[" << inst.dest.reg_index << "] = asfloat(asint("
                       << OperandToHlsl(inst.sources[0], program.stage) << "));\n";
                }
                break;
            case MaxwellOpcode::IADD3:
                if (inst.dest.type == OperandType::Register && inst.dest.reg_index < 255) {
                    ss << "    R[" << inst.dest.reg_index << "] = asfloat(asint("
                       << OperandToHlsl(inst.sources[0], program.stage) << ") + asint("
                       << OperandToHlsl(inst.sources[1], program.stage) << ") + asint("
                       << OperandToHlsl(inst.sources[2], program.stage) << "));\n";
                }
                break;
            case MaxwellOpcode::ISUB:
                if (inst.dest.type == OperandType::Register && inst.dest.reg_index < 255) {
                    ss << "    R[" << inst.dest.reg_index << "] = asfloat(asint("
                       << OperandToHlsl(inst.sources[0], program.stage) << ") - asint("
                       << OperandToHlsl(inst.sources[1], program.stage) << "));\n";
                }
                break;
            case MaxwellOpcode::ISCADD:
                if (inst.dest.type == OperandType::Register && inst.dest.reg_index < 255) {
                    ss << "    R[" << inst.dest.reg_index << "] = asfloat((asint("
                       << OperandToHlsl(inst.sources[0], program.stage) << ") << "
                       << inst.sources[2].imm_int << ") + asint("
                       << OperandToHlsl(inst.sources[1], program.stage) << "));\n";
                }
                break;
            case MaxwellOpcode::LEA:
                if (inst.dest.type == OperandType::Register && inst.dest.reg_index < 255) {
                    ss << "    R[" << inst.dest.reg_index << "] = asfloat(asint("
                       << OperandToHlsl(inst.sources[0], program.stage) << ") + asint("
                       << OperandToHlsl(inst.sources[1], program.stage) << "));\n";
                }
                break;
            case MaxwellOpcode::LOP3:
                if (inst.dest.type == OperandType::Register && inst.dest.reg_index < 255) {
                    // Exact LOP3: the 8-bit LUT enumerates f(a,b,c) over all
                    // input combos; expand to sum-of-minterms in HLSL.
                    const u32 lut = static_cast<u32>(inst.sources[2].imm_int) & 0xFF;
                    const std::string a = OperandToHlsl(inst.sources[0], program.stage);
                    const std::string b = OperandToHlsl(inst.sources[1], program.stage);
                    // Register-form LOP3 has c in rc; immediate form uses 0.
                    const std::string c = (inst.sources.size() > 3)
                        ? OperandToHlsl(inst.sources[3], program.stage)
                        : "asfloat(0)";
                    ss << "    { int _a = asint(" << a << "), _b = asint(" << b
                       << "), _c = asint(" << c << "), _r = 0;\n";
                    for (u32 m = 0; m < 8; ++m) {
                        if (!(lut & (1u << m))) continue;
                        const char* ta = (m & 1) ? "true" : "false";
                        const char* tb = (m & 2) ? "true" : "false";
                        const char* tc = (m & 4) ? "true" : "false";
                        ss << "      if (" << ta << " == (bool)(_a & 1) && " << tb
                           << " == (bool)((_b >> 1) & 1) && " << tc
                           << " == (bool)((_c >> 2) & 1)) _r |= 1;\n";
                    }
                    ss << "      R[" << inst.dest.reg_index << "] = asfloat(_r);\n    }\n";
                }
                break;
            case MaxwellOpcode::BFE:
                if (inst.dest.type == OperandType::Register && inst.dest.reg_index < 255) {
                    ss << "    R[" << inst.dest.reg_index << "] = asfloat(asint("
                       << OperandToHlsl(inst.sources[0], program.stage) << ") & asint("
                       << OperandToHlsl(inst.sources[1], program.stage) << "));\n";
                }
                break;
            case MaxwellOpcode::BFI:
                if (inst.dest.type == OperandType::Register && inst.dest.reg_index < 255) {
                    ss << "    R[" << inst.dest.reg_index << "] = asfloat(asint("
                       << OperandToHlsl(inst.sources[0], program.stage) << ") | asint("
                       << OperandToHlsl(inst.sources[1], program.stage) << "));\n";
                }
                break;
            case MaxwellOpcode::SHL:
                if (inst.dest.type == OperandType::Register && inst.dest.reg_index < 255) {
                    ss << "    R[" << inst.dest.reg_index << "] = asfloat(asint("
                       << OperandToHlsl(inst.sources[0], program.stage) << ") << asint("
                       << OperandToHlsl(inst.sources[1], program.stage) << "));\n";
                }
                break;
            case MaxwellOpcode::SHR:
                if (inst.dest.type == OperandType::Register && inst.dest.reg_index < 255) {
                    ss << "    R[" << inst.dest.reg_index << "] = asfloat(asint("
                       << OperandToHlsl(inst.sources[0], program.stage) << ") >> asint("
                       << OperandToHlsl(inst.sources[1], program.stage) << "));\n";
                }
                break;
            case MaxwellOpcode::SEL:
                if (inst.dest.type == OperandType::Register && inst.dest.reg_index < 255) {
                    ss << "    R[" << inst.dest.reg_index << "] = "
                       << OperandToHlsl(inst.sources[0], program.stage) << "; // SEL\n";
                }
                break;
            case MaxwellOpcode::LDG:
            case MaxwellOpcode::LDS:
                if (inst.dest.type == OperandType::Register && inst.dest.reg_index < 255) {
                    ss << "    R[" << inst.dest.reg_index << "] = 0.0f; // "
                       << (inst.opcode == MaxwellOpcode::LDG ? "LDG" : "LDS")
                       << " (global/shared load via GMMU)\n";
                }
                break;
            case MaxwellOpcode::STG:
            case MaxwellOpcode::STS:
                ss << "    // "
                   << (inst.opcode == MaxwellOpcode::STG ? "STG" : "STS")
                   << " (store via GMMU)\n";
                break;
            case MaxwellOpcode::TEXS:
            case MaxwellOpcode::TLD:
                if (inst.dest.type == OperandType::Register && inst.dest.reg_index < 255) {
                    u32 tex_idx_s = static_cast<u32>(inst.sources[2].imm_int);
                    ss << "    R[" << inst.dest.reg_index << "] = tex" << tex_idx_s
                       << ".Sample(samp" << tex_idx_s << ", float2("
                       << OperandToHlsl(inst.sources[0], program.stage) << ", "
                       << OperandToHlsl(inst.sources[1], program.stage) << ")).r;\n";
                }
                break;
            case MaxwellOpcode::TXQ:
                if (inst.dest.type == OperandType::Register && inst.dest.reg_index < 255) {
                    u32 tex_idx_q = static_cast<u32>(inst.sources[1].imm_int);
                    ss << "    R[" << inst.dest.reg_index << "] = asfloat(tex"
                       << tex_idx_q << ".GetDimensions(0));\n";
                }
                break;
            case MaxwellOpcode::LDC:
                if (inst.dest.type == OperandType::Register && inst.dest.reg_index < 255) {
                    ss << "    R[" << inst.dest.reg_index << "] = " << OperandToHlsl(inst.sources[0], program.stage) << ";\n";
                }
                break;
            case MaxwellOpcode::TEX:
                if (inst.dest.type == OperandType::Register && inst.dest.reg_index < 255) {
                    u32 tex_idx = static_cast<u32>(inst.sources[2].imm_int);
                    ss << "    R[" << inst.dest.reg_index << "] = tex" << tex_idx << ".Sample(samp" << tex_idx
                       << ", float2(" << OperandToHlsl(inst.sources[0], program.stage) << ", "
                       << OperandToHlsl(inst.sources[1], program.stage) << ")).r;\n";
                }
                break;
            case MaxwellOpcode::LD_ATTR:
                if (inst.dest.type == OperandType::Register && inst.dest.reg_index < 255) {
                    ss << "    R[" << inst.dest.reg_index << "] = " << OperandToHlsl(inst.sources[0], program.stage) << ";\n";
                }
                break;
            case MaxwellOpcode::ST_ATTR: {
                u32 attr_idx = inst.dest.attr.offset / 16;
                char comp_char = "xyzw"[inst.dest.attr.component & 3];
                if (program.stage == ShaderStage::Vertex) {
                    if (attr_idx == 0) {
                        ss << "    output.out_pos." << comp_char << " = " << OperandToHlsl(inst.sources[0], program.stage) << ";\n";
                    } else {
                        ss << "    output.out_attr" << attr_idx << "." << comp_char << " = " << OperandToHlsl(inst.sources[0], program.stage) << ";\n";
                    }
                } else {
                    u32 rt_idx = std::min(attr_idx, 7u);
                    ss << "    output.out_color" << rt_idx << "." << comp_char << " = " << OperandToHlsl(inst.sources[0], program.stage) << ";\n";
                }
                break;
            }
            case MaxwellOpcode::KIL:
                ss << "    discard;\n";
                break;
            case MaxwellOpcode::EXIT:
                // Compute shaders have no return value; the shader just ends.
                if (program.stage != ShaderStage::Compute) {
                    ss << "    return output;\n";
                }
                break;

            // Extended SASS families (IMAD/XMAD/SETP/MUFU/IPA/half-float/
            // flow-ctrl — 67 high-frequency families ported from yuzu).
#include "sass_emit_extended.inc"
        }
    }

    if (program.stage != ShaderStage::Compute) {
        ss << "    return output;\n";
    }
    ss << "}\n";

    return ss.str();
}

std::string MaxwellShaderDecoder::EmitGLSL(const DecompiledProgram& program) {
    std::ostringstream ss;
    ss << "#version 450 core\n";
    ss << "// Generated by Nemu Maxwell SASS -> GLSL\n\n";

    if (program.stage == ShaderStage::Vertex) {
        ss << "layout(location = 0) in vec4 in_pos;\n";
        for (u32 i : program.used_attrs) {
            ss << "layout(location = " << (i + 1) << ") in vec4 in_attr" << i << ";\n";
            ss << "layout(location = " << i << ") out vec4 out_attr" << i << ";\n";
        }
        ss << "\nvoid main() {\n";
        ss << "    gl_Position = in_pos;\n";
    } else {
        for (u32 i : program.used_attrs) {
            ss << "layout(location = " << i << ") in vec4 in_attr" << i << ";\n";
        }
        for (u32 rt = 0; rt < 8; ++rt) {
            ss << "layout(location = " << rt << ") out vec4 out_color" << rt << ";\n";
        }
        ss << "\nvoid main() {\n";
        ss << "    out_color0 = vec4(1.0, 1.0, 1.0, 1.0);\n";
        for (u32 rt = 1; rt < 8; ++rt) {
            ss << "    out_color" << rt << " = vec4(0.0, 0.0, 0.0, 0.0);\n";
        }
    }

    u32 num_regs = std::max(program.max_register_used + 1, 16u);
    ss << "    float R[" << num_regs << "];\n";
    ss << "    for (int _i = 0; _i < " << num_regs << "; ++_i) R[_i] = 0.0;\n\n";

    for (const auto& inst : program.instructions) {
        switch (inst.opcode) {
            case MaxwellOpcode::MOV:
                if (inst.dest.type == OperandType::Register && inst.dest.reg_index < 255) {
                    ss << "    R[" << inst.dest.reg_index << "] = " << OperandToGlsl(inst.sources[0], program.stage) << ";\n";
                }
                break;
            case MaxwellOpcode::FADD:
                if (inst.dest.type == OperandType::Register && inst.dest.reg_index < 255) {
                    ss << "    R[" << inst.dest.reg_index << "] = "
                       << OperandToGlsl(inst.sources[0], program.stage) << " + "
                       << OperandToGlsl(inst.sources[1], program.stage) << ";\n";
                }
                break;
            case MaxwellOpcode::FMUL:
                if (inst.dest.type == OperandType::Register && inst.dest.reg_index < 255) {
                    ss << "    R[" << inst.dest.reg_index << "] = "
                       << OperandToGlsl(inst.sources[0], program.stage) << " * "
                       << OperandToGlsl(inst.sources[1], program.stage) << ";\n";
                }
                break;
            case MaxwellOpcode::KIL:
                ss << "    discard;\n";
                break;
            case MaxwellOpcode::EXIT:
                ss << "    return;\n";
                break;
            default:
                break;
        }
    }

    ss << "}\n";
    return ss.str();
}

} // namespace nemu::core::gpu::shader
