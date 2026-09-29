#include "sass_identifier.hpp"
#include "maxwell_shader_decoder.hpp"
#include <array>
#include <cstddef>

namespace nemu::core::gpu::shader {

// SassOpcode must be at namespace scope (same entity as the header decl);
// only the encoding TABLE is TU-local.
#include "sass_opcode_table.inc" // defines SassOpcode + SassEncoding + kSassEncodings

namespace {
#include "sass_cute_names.inc" // parallel kSassCuteNames (TU-local)
} // namespace

SassInfo IdentifySass(u64 raw_inst) {
    for (size_t i = 0; i < kSassEncodings.size(); ++i) {
        const auto& e = kSassEncodings[i];
        if ((raw_inst & e.mask) == e.value) {
            return {e.opcode, i};
        }
    }
    // Unknown opcode: NOP family, no encoding index. Callers log + skip
    // (matches how a faulting card treats garbage words).
    return {SassOpcode::NOP, SIZE_MAX};
}

const char* SassCuteName(size_t encoding_index) {
    if (encoding_index < kSassCuteNames.size()) {
        return kSassCuteNames[encoding_index];
    }
    return "UNKNOWN";
}

// ---------------------------------------------------------------------------
// IdentifyMaxwell: bridge the SASS identification layer to the HLSL emitter's
// MaxwellOpcode enum. Identifies a raw word and maps its SASS family to the
// concrete Maxwell opcode the emitter switch understands, or UNKNOWN when the
// ping family has no emitter case (handled as a comment / passthrough).
// ---------------------------------------------------------------------------
namespace {
MaxwellOpcode ToMaxwell(SassOpcode sass) noexcept {
    switch (sass) {
        case SassOpcode::AL2P:        return MaxwellOpcode::AL2P;
        case SassOpcode::ALD:         return MaxwellOpcode::ALD;
        case SassOpcode::AST:         return MaxwellOpcode::AST;
        case SassOpcode::ATOM:        return MaxwellOpcode::ATOM;
        case SassOpcode::ATOMS:       return MaxwellOpcode::ATOMS;
        case SassOpcode::B2R:         return MaxwellOpcode::B2R;
        case SassOpcode::BAR:         return MaxwellOpcode::BAR;
        case SassOpcode::BFE:         return MaxwellOpcode::BFE;
        case SassOpcode::BFI:         return MaxwellOpcode::BFI;
        case SassOpcode::BPT:         return MaxwellOpcode::BPT;
        case SassOpcode::BRA:         return MaxwellOpcode::BRA;
        case SassOpcode::BRK:         return MaxwellOpcode::BRK;
        case SassOpcode::BRX:         return MaxwellOpcode::BRX;
        case SassOpcode::CAL:         return MaxwellOpcode::CAL;
        case SassOpcode::CCTL:        return MaxwellOpcode::CCTL;
        case SassOpcode::CCTLL:       return MaxwellOpcode::CCTLL;
        case SassOpcode::CONT:        return MaxwellOpcode::CONT;
        case SassOpcode::CS2R:        return MaxwellOpcode::CS2R;
        case SassOpcode::CSET:        return MaxwellOpcode::CSET;
        case SassOpcode::CSETP:       return MaxwellOpcode::CSETP;
        case SassOpcode::DADD:        return MaxwellOpcode::DADD;
        case SassOpcode::DEPBAR:      return MaxwellOpcode::DEPBAR;
        case SassOpcode::DFMA:        return MaxwellOpcode::DFMA;
        case SassOpcode::DMNMX:       return MaxwellOpcode::DMNMX;
        case SassOpcode::DMUL:        return MaxwellOpcode::DMUL;
        case SassOpcode::DSET:        return MaxwellOpcode::DSET;
        case SassOpcode::DSETP:       return MaxwellOpcode::DSETP;
        case SassOpcode::EXIT:        return MaxwellOpcode::EXIT;
        case SassOpcode::F2F:         return MaxwellOpcode::F2F;
        case SassOpcode::F2I:         return MaxwellOpcode::F2I;
        case SassOpcode::FADD:        return MaxwellOpcode::FADD;
        case SassOpcode::FADD32I:     return MaxwellOpcode::FADD32I;
        case SassOpcode::FCHK:        return MaxwellOpcode::FCHK;
        case SassOpcode::FCMP:        return MaxwellOpcode::FCMP;
        case SassOpcode::FFMA:        return MaxwellOpcode::FFMA;
        case SassOpcode::FFMA32I:     return MaxwellOpcode::FFMA32I;
        case SassOpcode::FLO:         return MaxwellOpcode::FLO;
        case SassOpcode::FMNMX:       return MaxwellOpcode::FMNMX;
        case SassOpcode::FMUL:        return MaxwellOpcode::FMUL;
        case SassOpcode::FMUL32I:     return MaxwellOpcode::FMUL32I;
        case SassOpcode::FSET:        return MaxwellOpcode::FSET;
        case SassOpcode::FSETP:       return MaxwellOpcode::FSETP;
        case SassOpcode::FSWZADD:     return MaxwellOpcode::FSWZADD;
        case SassOpcode::GETCRSPTR:   return MaxwellOpcode::GETCRSPTR;
        case SassOpcode::GETLMEMBASE: return MaxwellOpcode::GETLMEMBASE;
        case SassOpcode::HADD2:       return MaxwellOpcode::HADD2;
        case SassOpcode::HADD2_32I:   return MaxwellOpcode::HADD2_32I;
        case SassOpcode::HFMA2:       return MaxwellOpcode::HFMA2;
        case SassOpcode::HFMA2_32I:   return MaxwellOpcode::HFMA2_32I;
        case SassOpcode::HMUL2:       return MaxwellOpcode::HMUL2;
        case SassOpcode::HMUL2_32I:   return MaxwellOpcode::HMUL2_32I;
        case SassOpcode::HSET2:       return MaxwellOpcode::HSET2;
        case SassOpcode::HSETP2:      return MaxwellOpcode::HSETP2;
        case SassOpcode::I2F:         return MaxwellOpcode::I2F;
        case SassOpcode::I2I:         return MaxwellOpcode::I2I;
        case SassOpcode::IADD:        return MaxwellOpcode::IADD;
        case SassOpcode::IADD3:       return MaxwellOpcode::IADD3;
        case SassOpcode::IADD32I:     return MaxwellOpcode::IADD32I;
        case SassOpcode::ICMP:        return MaxwellOpcode::ICMP;
        case SassOpcode::IDE:         return MaxwellOpcode::IDE;
        case SassOpcode::IDP:         return MaxwellOpcode::IDP;
        case SassOpcode::IMAD:        return MaxwellOpcode::IMAD;
        case SassOpcode::IMAD32I:     return MaxwellOpcode::IMAD32I;
        case SassOpcode::IMADSP:      return MaxwellOpcode::IMADSP;
        case SassOpcode::IMNMX:       return MaxwellOpcode::IMNMX;
        case SassOpcode::IMUL:        return MaxwellOpcode::IMUL;
        case SassOpcode::IMUL32I:     return MaxwellOpcode::IMUL32I;
        case SassOpcode::IPA:         return MaxwellOpcode::IPA;
        case SassOpcode::ISBERD:      return MaxwellOpcode::ISBERD;
        case SassOpcode::ISCADD:      return MaxwellOpcode::ISCADD;
        case SassOpcode::ISCADD32I:   return MaxwellOpcode::ISCADD32I;
        case SassOpcode::ISET:        return MaxwellOpcode::ISET;
        case SassOpcode::ISETP:       return MaxwellOpcode::ISETP;
        case SassOpcode::JCAL:        return MaxwellOpcode::JCAL;
        case SassOpcode::JMP:         return MaxwellOpcode::JMP;
        case SassOpcode::JMX:         return MaxwellOpcode::JMX;
        case SassOpcode::KIL:         return MaxwellOpcode::KIL;
        case SassOpcode::LD:          return MaxwellOpcode::LD;
        case SassOpcode::LDC:         return MaxwellOpcode::LDC;
        case SassOpcode::LDG:         return MaxwellOpcode::LDG;
        case SassOpcode::LDL:         return MaxwellOpcode::LDL;
        case SassOpcode::LDS:         return MaxwellOpcode::LDS;
        case SassOpcode::LEA_hi:      return MaxwellOpcode::LEA_hi;
        case SassOpcode::LEA_lo:      return MaxwellOpcode::LEA_lo;
        case SassOpcode::LEPC:        return MaxwellOpcode::LEPC;
        case SassOpcode::LONGJMP:     return MaxwellOpcode::LONGJMP;
        case SassOpcode::LOP:         return MaxwellOpcode::LOP;
        case SassOpcode::LOP3:        return MaxwellOpcode::LOP3;
        case SassOpcode::LOP32I:      return MaxwellOpcode::LOP32I;
        case SassOpcode::MEMBAR:      return MaxwellOpcode::MEMBAR;
        case SassOpcode::MOV:         return MaxwellOpcode::MOV;
        case SassOpcode::MOV32I:      return MaxwellOpcode::MOV32I;
        case SassOpcode::MUFU:        return MaxwellOpcode::MUFU;
        case SassOpcode::NOP:         return MaxwellOpcode::NOP;
        case SassOpcode::OUT:         return MaxwellOpcode::OUT_stream;
        case SassOpcode::P2R:         return MaxwellOpcode::P2R;
        case SassOpcode::PBK:         return MaxwellOpcode::PBK;
        case SassOpcode::PCNT:        return MaxwellOpcode::PCNT;
        case SassOpcode::PEXIT:       return MaxwellOpcode::PEXIT;
        case SassOpcode::PIXLD:       return MaxwellOpcode::PIXLD;
        case SassOpcode::PLONGJMP:    return MaxwellOpcode::PLONGJMP;
        case SassOpcode::POPC:        return MaxwellOpcode::POPC;
        case SassOpcode::PRET:        return MaxwellOpcode::PRET;
        case SassOpcode::PRMT:        return MaxwellOpcode::PRMT;
        case SassOpcode::PSET:        return MaxwellOpcode::PSET;
        case SassOpcode::PSETP:       return MaxwellOpcode::PSETP;
        case SassOpcode::R2B:         return MaxwellOpcode::R2B;
        case SassOpcode::R2P:         return MaxwellOpcode::R2P;
        case SassOpcode::RAM:         return MaxwellOpcode::RAM;
        case SassOpcode::RED:         return MaxwellOpcode::RED;
        case SassOpcode::RET:         return MaxwellOpcode::RET;
        case SassOpcode::RRO:         return MaxwellOpcode::RRO;
        case SassOpcode::RTT:         return MaxwellOpcode::RTT;
        case SassOpcode::S2R:         return MaxwellOpcode::S2R;
        case SassOpcode::SAM:         return MaxwellOpcode::SAM;
        case SassOpcode::SEL:         return MaxwellOpcode::SEL;
        case SassOpcode::SETCRSPTR:   return MaxwellOpcode::SETCRSPTR;
        case SassOpcode::SETLMEMBASE: return MaxwellOpcode::SETLMEMBASE;
        case SassOpcode::SHFL:        return MaxwellOpcode::SHFL;
        case SassOpcode::SHF_l:       return MaxwellOpcode::SHF_l;
        case SassOpcode::SHF_r:       return MaxwellOpcode::SHF_r;
        case SassOpcode::SHL:         return MaxwellOpcode::SHL;
        case SassOpcode::SHR:         return MaxwellOpcode::SHR;
        case SassOpcode::SSY:         return MaxwellOpcode::SSY;
        case SassOpcode::ST:          return MaxwellOpcode::ST;
        case SassOpcode::STG:         return MaxwellOpcode::STG;
        case SassOpcode::STL:         return MaxwellOpcode::STL;
        case SassOpcode::STP:         return MaxwellOpcode::STP;
        case SassOpcode::STS:         return MaxwellOpcode::STS;
        case SassOpcode::SUATOM:      return MaxwellOpcode::SUATOM;
        case SassOpcode::SULD:        return MaxwellOpcode::SULD;
        case SassOpcode::SURED:       return MaxwellOpcode::SURED;
        case SassOpcode::SUST:        return MaxwellOpcode::SUST;
        case SassOpcode::SYNC:        return MaxwellOpcode::SYNC;
        case SassOpcode::TEX:         return MaxwellOpcode::TEX;
        case SassOpcode::TEXS:        return MaxwellOpcode::TEXS;
        case SassOpcode::TEX_b:       return MaxwellOpcode::TEX_b;
        case SassOpcode::TLD:         return MaxwellOpcode::TLD;
        case SassOpcode::TLD4:        return MaxwellOpcode::TLD4;
        case SassOpcode::TLD4S:       return MaxwellOpcode::TLD4S;
        case SassOpcode::TLD4_b:      return MaxwellOpcode::TLD4_b;
        case SassOpcode::TLDS:        return MaxwellOpcode::TLDS;
        case SassOpcode::TLD_b:       return MaxwellOpcode::TLD_b;
        case SassOpcode::TMML:        return MaxwellOpcode::TMML;
        case SassOpcode::TMML_b:      return MaxwellOpcode::TMML_b;
        case SassOpcode::TXA:         return MaxwellOpcode::TXA;
        case SassOpcode::TXD:         return MaxwellOpcode::TXD;
        case SassOpcode::TXD_b:       return MaxwellOpcode::TXD_b;
        case SassOpcode::TXQ:         return MaxwellOpcode::TXQ;
        case SassOpcode::TXQ_b:       return MaxwellOpcode::TXQ_b;
        case SassOpcode::VABSDIFF:    return MaxwellOpcode::VABSDIFF;
        case SassOpcode::VABSDIFF4:   return MaxwellOpcode::VABSDIFF4;
        case SassOpcode::VADD:        return MaxwellOpcode::VADD;
        case SassOpcode::VMAD:        return MaxwellOpcode::VMAD;
        case SassOpcode::VMNMX:       return MaxwellOpcode::VMNMX;
        case SassOpcode::VOTE:        return MaxwellOpcode::VOTE;
        case SassOpcode::VOTE_vtg:    return MaxwellOpcode::VOTE_vtg;
        case SassOpcode::VSET:        return MaxwellOpcode::VSET;
        case SassOpcode::VSETP:       return MaxwellOpcode::VSETP;
        case SassOpcode::VSHL:        return MaxwellOpcode::VSHL;
        case SassOpcode::VSHR:        return MaxwellOpcode::VSHR;
        case SassOpcode::XMAD:        return MaxwellOpcode::XMAD;
        default:                      return MaxwellOpcode::UNKNOWN;
    }
}
} // namespace

MaxwellOpcode IdentifyMaxwell(u64 raw_inst) {
    const SassInfo sass = IdentifySass(raw_inst);
    if (sass.encoding_index == SIZE_MAX) {
        return MaxwellOpcode::UNKNOWN;
    }
    return ToMaxwell(sass.opcode);
}

} // namespace nemu::core::gpu::shader