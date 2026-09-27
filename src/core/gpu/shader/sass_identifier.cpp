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
        case SassOpcode::NOP:    return MaxwellOpcode::NOP;
        case SassOpcode::MOV:    return MaxwellOpcode::MOV;
        case SassOpcode::SEL:    return MaxwellOpcode::SEL;
        case SassOpcode::FADD:   return MaxwellOpcode::FADD;
        case SassOpcode::FMUL:   return MaxwellOpcode::FMUL;
        case SassOpcode::FFMA:   return MaxwellOpcode::FFMA;
        case SassOpcode::FMNMX:  return MaxwellOpcode::FMNMX;
        case SassOpcode::FCMP:   return MaxwellOpcode::FCMP;
        case SassOpcode::F2F:    return MaxwellOpcode::F2F;
        case SassOpcode::F2I:    return MaxwellOpcode::F2I;
        case SassOpcode::I2F:    return MaxwellOpcode::I2F;
        case SassOpcode::IADD:   return MaxwellOpcode::IADD;
        case SassOpcode::IADD3:  return MaxwellOpcode::IADD3;
        case SassOpcode::IMUL:   return MaxwellOpcode::IMUL;
        case SassOpcode::ISCADD: return MaxwellOpcode::ISCADD;
        case SassOpcode::LOP:    return MaxwellOpcode::LOP;
        case SassOpcode::LOP3:   return MaxwellOpcode::LOP3;
        case SassOpcode::SHL:    return MaxwellOpcode::SHL;
        case SassOpcode::SHR:    return MaxwellOpcode::SHR;
        case SassOpcode::BFE:    return MaxwellOpcode::BFE;
        case SassOpcode::BFI:    return MaxwellOpcode::BFI;
        case SassOpcode::LDC:    return MaxwellOpcode::LDC;
        case SassOpcode::LDG:    return MaxwellOpcode::LDG;
        case SassOpcode::STG:    return MaxwellOpcode::STG;
        case SassOpcode::LDS:    return MaxwellOpcode::LDS;
        case SassOpcode::STS:    return MaxwellOpcode::STS;
        case SassOpcode::TEX:    return MaxwellOpcode::TEX;
        case SassOpcode::TEXS:   return MaxwellOpcode::TEXS;
        case SassOpcode::TLD:    return MaxwellOpcode::TLD;
        case SassOpcode::TXQ:    return MaxwellOpcode::TXQ;
        case SassOpcode::BRA:    return MaxwellOpcode::BRA;
        case SassOpcode::EXIT:   return MaxwellOpcode::EXIT;
        case SassOpcode::KIL:    return MaxwellOpcode::KIL;
        case SassOpcode::SYNC:   return MaxwellOpcode::SYNC;
        case SassOpcode::S2R:    return MaxwellOpcode::S2R;
        case SassOpcode::AL2P:   return MaxwellOpcode::AL2P;
        // FSUB/ISUB/LEA/LD_ATTR/ST_ATTR are not top-level SASS families in
        // yuzu's maxwell.inc (encoded via immediate/register variants or the
        // LD.ATTR family under a different encode); they fall through to
        // UNKNOWN here and the extended emitter covers their shape.
        default:                 return MaxwellOpcode::UNKNOWN;
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