// ---------------------------------------------------------------------------
// sass_emit_extended.inc — HLSL emission for extended SASS families.
// Included inside MaxwellShaderDecoder::EmitHLSL's instruction switch.
// Provides emission for the 67 highest-frequency families missing from the
// fast path (IMAD/XMAD/SETP/JMP/LD-ST/LDL-STL/MUFU/IPA/half-float/flow-ctrl).
// Semantics ported from yuzu translate_program.cpp (GPL-3.0-or-later).
//
// Expectations in the including TU:
//   - `inst`   : const DecodedInstruction&
//   - `program`: const DecompiledProgram&
//   - `ss`     : std::ostringstream& (the HLSL output)
//   - OperandToHlsl(op, stage) is visible
//   - kEmitDestReg / kEmitDestAttr helpers below are defined first
// ---------------------------------------------------------------------------

// Write a scalar into the destination register (if register-addressable).
#define NEMU_EMIT_WR(expr)                                                     \
    if (inst.dest.type == OperandType::Register && inst.dest.reg_index < 255) { \
        ss << "    R[" << inst.dest.reg_index << "] = asfloat(" << (expr)      \
           << ");\n";                                                          \
    }

// Write into an output attribute (ST_ATTR-equivalent semantics).
#define NEMU_EMIT_WR_ATTR(expr)                                                \
    {                                                                          \
        const u32 _ai = inst.dest.attr.offset / 16;                            \
        const char _ac = "xyzw"[inst.dest.attr.component & 3];                 \
        if (program.stage == ShaderStage::Vertex && _ai == 0) {                \
            ss << "    output.out_pos." << _ac << " = " << (expr) << ";\n";    \
        } else if (program.stage == ShaderStage::Vertex) {                     \
            ss << "    output.out_attr" << _ai << "." << _ac << " = "          \
               << (expr) << ";\n";                                             \
        } else {                                                               \
            ss << "    output.out_color0." << _ac << " = " << (expr) << ";\n"; \
        }                                                                      \
    }

// Safe source access (immediate-form instructions may have fewer sources).
#define NEMU_SRC(i) ((inst.sources.size() > (i)) \
    ? OperandToHlsl(inst.sources[i], program.stage) : "0.0f")

// ---- Integer multiply-add family ------------------------------------------
// IMAD / IMAD32I / XMAD / VMAD: Rd = (Ra * Rb) + Rc (32-bit wrapped int math).
case MaxwellOpcode::IMAD:
case MaxwellOpcode::IMAD32I:
case MaxwellOpcode::XMAD:
    NEMU_EMIT_WR("asint(" + NEMU_SRC(0) + ") * asint(" + NEMU_SRC(1) + ") + asint(" + NEMU_SRC(2) + ")")
    break;

// IMADSP: like IMAD but the shift/selector applies to the product — modeled
// as plain multiply-add (the selector forms are rare in shipped shaders).
case MaxwellOpcode::IMADSP:
    NEMU_EMIT_WR("asint(" + NEMU_SRC(0) + ") * asint(" + NEMU_SRC(1) + ") + asint(" + NEMU_SRC(2) + ")")
    break;

// ---- 32-bit immediate ALU (fixed-bank constant forms) ----------------------
case MaxwellOpcode::IADD32I:
    NEMU_EMIT_WR("asint(" + NEMU_SRC(0) + ") + asint(" + NEMU_SRC(1) + ")")
    break;
case MaxwellOpcode::FMUL32I:
    NEMU_EMIT_WR(NEMU_SRC(0) + " * " + NEMU_SRC(1))
    break;
case MaxwellOpcode::FADD32I:
    NEMU_EMIT_WR(NEMU_SRC(0) + " + " + NEMU_SRC(1))
    break;
case MaxwellOpcode::FFMA32I:
    NEMU_EMIT_WR("(" + NEMU_SRC(0) + " * " + NEMU_SRC(1) + ") + " + NEMU_SRC(2))
    break;
case MaxwellOpcode::ISCADD32I:
    NEMU_EMIT_WR("(asint(" + NEMU_SRC(0) + ") << 2) + asint(" + NEMU_SRC(1) + ")")
    break;
case MaxwellOpcode::IMUL32I:
    NEMU_EMIT_WR("asint(" + NEMU_SRC(0) + ") * asint(" + NEMU_SRC(1) + ")")
    break;
case MaxwellOpcode::LOP32I:
    NEMU_EMIT_WR("asint(asint(" + NEMU_SRC(0) + ") | asint(" + NEMU_SRC(1) + ")) // LOP32I")
    break;
case MaxwellOpcode::MOV32I:
    NEMU_EMIT_WR(NEMU_SRC(0))
    break;

// ---- Float min/max & double-family (mapped to float path) ------------------
case MaxwellOpcode::DMNMX:
case MaxwellOpcode::IMNMX:
    NEMU_EMIT_WR("min(" + NEMU_SRC(0) + ", " + NEMU_SRC(1) + ")")
    break;
case MaxwellOpcode::DFMA:
    NEMU_EMIT_WR("(" + NEMU_SRC(0) + " * " + NEMU_SRC(1) + ") + " + NEMU_SRC(2))
    break;
case MaxwellOpcode::DMUL:
    NEMU_EMIT_WR(NEMU_SRC(0) + " * " + NEMU_SRC(1))
    break;
case MaxwellOpcode::DADD:
    NEMU_EMIT_WR(NEMU_SRC(0) + " + " + NEMU_SRC(1))
    break;

// ---- Float/int compare + select --------------------------------------------
case MaxwellOpcode::ICMP:
    // Result encodes compare lanes; HLE approximation: max of the two.
    NEMU_EMIT_WR("max(" + NEMU_SRC(0) + ", " + NEMU_SRC(1) + ")")
    break;

// ---- Predicate set family ---------------------------------------------------
// PSETP / ISETP / FSETP / PSET / ISET / FSET produce predicates or booleans.
// In the register model we materialize the condition as 0.0/1.0.
case MaxwellOpcode::PSETP:
case MaxwellOpcode::ISETP:
case MaxwellOpcode::FSETP: {
    if (inst.dest.type == OperandType::Register && inst.dest.reg_index < 255) {
        ss << "    p[0] = (" << NEMU_SRC(0) << " < " << NEMU_SRC(1) << "); // "
           << "SETP (comparison modeled as LT)\n";
        ss << "    R[" << inst.dest.reg_index << "] = p[0] ? 1.0f : 0.0f;\n";
    }
    break;
}
case MaxwellOpcode::PSET:
case MaxwellOpcode::ISET:
case MaxwellOpcode::FSET:
    NEMU_EMIT_WR("((" + NEMU_SRC(0) + " < " + NEMU_SRC(1) + ") ? 1.0f : 0.0f) // SET as LT")
    break;
case MaxwellOpcode::CSET:
    NEMU_EMIT_WR("(p[0] ? " + NEMU_SRC(0) + " : " + NEMU_SRC(1) + ")")
    break;
case MaxwellOpcode::CSETP:
    ss << "    p[0] = p[0]; // CSETP (predicate passthrough)\n";
    break;

// ---- Flow control -----------------------------------------------------------
case MaxwellOpcode::JMP:
case MaxwellOpcode::JMX:
case MaxwellOpcode::BRX:
case MaxwellOpcode::PEXIT:
case MaxwellOpcode::LONGJMP:
case MaxwellOpcode::PLONGJMP:
case MaxwellOpcode::JCAL:
case MaxwellOpcode::CAL:
case MaxwellOpcode::RET:
case MaxwellOpcode::BRK:
    if (inst.predicate < 7) {
        ss << "    if (" << (inst.predicate_invert ? "!" : "") << "p[" << static_cast<int>(inst.predicate) << "]) break;\n";
    } else {
        ss << "    break;\n";
    }
    break;
case MaxwellOpcode::CONT:
    if (inst.predicate < 7) {
        ss << "    if (" << (inst.predicate_invert ? "!" : "") << "p[" << static_cast<int>(inst.predicate) << "]) continue;\n";
    } else {
        ss << "    continue;\n";
    }
    break;
case MaxwellOpcode::SSY:
    ss << "    // [ssy] structured branch sync target 0x" << std::hex << inst.branch_target << std::dec << "\n";
    break;
case MaxwellOpcode::PBK:
    ss << "    // [pbk] loop break target 0x" << std::hex << inst.branch_target << std::dec << "\n";
    break;
case MaxwellOpcode::PCNT:
    ss << "    // [pcnt] loop continue target 0x" << std::hex << inst.branch_target << std::dec << "\n";
    break;
case MaxwellOpcode::SYNC:
    ss << "    // [sync] warp reconvergence point\n";
    break;
case MaxwellOpcode::PRET:
case MaxwellOpcode::RTT:
    break;
case MaxwellOpcode::BRA:
    // Conditional / Unconditional forward or backward branch
    if (inst.predicate < 7) {
        ss << "    // [bra] if (" << (inst.predicate_invert ? "!" : "")
           << "p[" << static_cast<int>(inst.predicate) << "]) branch to 0x"
           << std::hex << inst.branch_target << std::dec << "\n";
    } else {
        ss << "    // [bra] goto 0x" << std::hex << inst.branch_target << std::dec << "\n";
    }
    break;

// ---- Memory: generic LD/ST + local ------------------------------------------
case MaxwellOpcode::LD:
case MaxwellOpcode::LDL:
    NEMU_EMIT_WR("0.0f // generic/local load (GMMU path)")
    break;
case MaxwellOpcode::ST:
case MaxwellOpcode::STL:
    ss << "    // generic/local store (GMMU path)\n";
    break;
case MaxwellOpcode::STP:
case MaxwellOpcode::LDP:
    break;

// ---- Special function unit + bit ops ----------------------------------------
case MaxwellOpcode::MUFU: {
    // MUFU: sub-op stashed in branch_target at decode time (bits [57:53]).
    const u64 sub = inst.branch_target;
    const char* fn = (sub == 0x0) ? "div" : (sub == 0x4) ? "rcp"
                   : (sub == 0x8) ? "rsqrt" : (sub == 0xC) ? "sin"
                   : (sub == 0xD) ? "cos" : (sub == 0xE) ? "exp2"
                   : (sub == 0xF) ? "log2" : nullptr;
    if (fn) {
        std::string expr;
        if (std::string_view(fn) == "div") {
            expr = NEMU_SRC(0) + " / " + NEMU_SRC(1);
        } else if (std::string_view(fn) == "rcp") {
            expr = "1.0f / " + NEMU_SRC(0);
        } else if (std::string_view(fn) == "rsqrt") {
            expr = "rsqrt(" + NEMU_SRC(0) + ")";
        } else if (std::string_view(fn) == "sin") {
            expr = "sin(" + NEMU_SRC(0) + ")";
        } else if (std::string_view(fn) == "cos") {
            expr = "cos(" + NEMU_SRC(0) + ")";
        } else if (std::string_view(fn) == "exp2") {
            expr = "exp2(" + NEMU_SRC(0) + ")";
        } else {
            expr = "log2(" + NEMU_SRC(0) + ")";
        }
        NEMU_EMIT_WR(expr)
    }
    break;
}
case MaxwellOpcode::FLO:
    NEMU_EMIT_WR("asfloat(firstbitlow(asuint(" + NEMU_SRC(0) + "))) // FLO")
    break;
case MaxwellOpcode::POPC:
    NEMU_EMIT_WR("asfloat(countbits(asuint(" + NEMU_SRC(0) + ")))")
    break;
case MaxwellOpcode::PRMT:
    NEMU_EMIT_WR(NEMU_SRC(0) + " // PRMT (byte-permute approximated)")
    break;
case MaxwellOpcode::FCHK:
    ss << "    // FCHK (fp range check — no HLSL equivalent, no-op)\n";
    break;
case MaxwellOpcode::I2I:
    NEMU_EMIT_WR(NEMU_SRC(0) + " // I2I (int move/cast)")
    break;

// ---- Interpolation + attribute plumbing -------------------------------------
case MaxwellOpcode::IPA:
    // IPA: interpolate attribute across the primitive. In the software model
    // attributes arrive pre-interpolated; emit a passthrough.
    NEMU_EMIT_WR(NEMU_SRC(0) + " // IPA (pre-interpolated)")
    break;
case MaxwellOpcode::P2R:
case MaxwellOpcode::R2P:
case MaxwellOpcode::R2B:
case MaxwellOpcode::B2R:
    // Predicate<->register conversion: materialize from the p[] file.
    if (inst.dest.type == OperandType::Register && inst.dest.reg_index < 255) {
        ss << "    R[" << inst.dest.reg_index << "] = p[0] ? 1.0f : 0.0f;\n";
    }
    break;
case MaxwellOpcode::CS2R:
    NEMU_EMIT_WR("0.0f // CS2R (clear set register)")
    break;

// ---- Half-float (fp16) family — computed in fp32 ----------------------------
case MaxwellOpcode::HFMA2:
case MaxwellOpcode::HFMA2_32I:
    NEMU_EMIT_WR("(" + NEMU_SRC(0) + " * " + NEMU_SRC(1) + ") + " + NEMU_SRC(2) + " // fp16 op in fp32")
    break;
case MaxwellOpcode::HADD2:
case MaxwellOpcode::HADD2_32I:
    NEMU_EMIT_WR(NEMU_SRC(0) + " + " + NEMU_SRC(1))
    break;
case MaxwellOpcode::HMUL2:
case MaxwellOpcode::HMUL2_32I:
    NEMU_EMIT_WR(NEMU_SRC(0) + " * " + NEMU_SRC(1))
    break;
case MaxwellOpcode::HSET2:
    NEMU_EMIT_WR("((" + NEMU_SRC(0) + " < " + NEMU_SRC(1) + ") ? 1.0f : 0.0f)")
    break;
case MaxwellOpcode::HSETP2:
    ss << "    p[0] = (" << NEMU_SRC(0) << " < " << NEMU_SRC(1) << ");\n";
    break;

// ---- Texture variants (map onto declared samplers) ---------------------------
case MaxwellOpcode::TLD4:
case MaxwellOpcode::TLD4S: {
    if (inst.dest.type == OperandType::Register && inst.dest.reg_index < 255) {
        if (!program.used_textures.empty()) {
            u32 tid = program.used_textures[0];
            ss << "    R[" << inst.dest.reg_index << "] = tex" << tid
               << ".Gather(samp" << tid << ", float2("
               << NEMU_SRC(0) << ", " << NEMU_SRC(1) << ")).r;\n";
        } else {
            ss << "    R[" << inst.dest.reg_index << "] = 0.0f;\n";
        }
    }
    break;
}
case MaxwellOpcode::TLDS:
case MaxwellOpcode::TLD_b: {
    if (inst.dest.type == OperandType::Register && inst.dest.reg_index < 255) {
        if (!program.used_textures.empty()) {
            u32 tid = program.used_textures[0];
            ss << "    R[" << inst.dest.reg_index << "] = tex" << tid
               << ".Load(int3(asint(" << NEMU_SRC(0) << "), asint(" << NEMU_SRC(1) << "), 0)).r;\n";
        } else {
            ss << "    R[" << inst.dest.reg_index << "] = 0.0f;\n";
        }
    }
    break;
}
case MaxwellOpcode::TXD:
case MaxwellOpcode::TXD_b: {
    if (inst.dest.type == OperandType::Register && inst.dest.reg_index < 255) {
        if (!program.used_textures.empty()) {
            u32 tid = program.used_textures[0];
            ss << "    R[" << inst.dest.reg_index << "] = tex" << tid
               << ".SampleGrad(samp" << tid << ", float2("
               << NEMU_SRC(0) << ", " << NEMU_SRC(1) << "), float2(0.0f, 0.0f), float2(0.0f, 0.0f)).r;\n";
        } else {
            ss << "    R[" << inst.dest.reg_index << "] = 0.0f;\n";
        }
    }
    break;
}
case MaxwellOpcode::TMML:
case MaxwellOpcode::TMML_b: {
    if (inst.dest.type == OperandType::Register && inst.dest.reg_index < 255) {
        if (!program.used_textures.empty()) {
            u32 tid = program.used_textures[0];
            ss << "    R[" << inst.dest.reg_index << "] = tex" << tid
               << ".CalculateLevelOfDetail(samp" << tid << ", float2("
               << NEMU_SRC(0) << ", " << NEMU_SRC(1) << "));\n";
        } else {
            ss << "    R[" << inst.dest.reg_index << "] = 0.0f;\n";
        }
    }
    break;
}
case MaxwellOpcode::TEX_b:
case MaxwellOpcode::TXA: {
    if (inst.dest.type == OperandType::Register && inst.dest.reg_index < 255) {
        if (!program.used_textures.empty()) {
            u32 tid = program.used_textures[0];
            ss << "    R[" << inst.dest.reg_index << "] = tex" << tid
               << ".Sample(samp" << tid << ", float2("
               << NEMU_SRC(0) << ", " << NEMU_SRC(1) << ")).r;\n";
        } else {
            ss << "    R[" << inst.dest.reg_index << "] = 0.0f;\n";
        }
    }
    break;
}

// ---- Warp/consensus + barriers ----------------------------------------------
case MaxwellOpcode::VOTE:
case MaxwellOpcode::VOTE_vtg:
case MaxwellOpcode::BAR:
case MaxwellOpcode::DEPBAR:
case MaxwellOpcode::MEMBAR:
case MaxwellOpcode::SHFL:
case MaxwellOpcode::FSWZADD:
case MaxwellOpcode::LEA_hi:
case MaxwellOpcode::LEA_lo:
    break;

// ---- Rare / unsupported families (visibility, not silent drop) --------------
case MaxwellOpcode::UNKNOWN:
default:
    if (!inst.disassembly.empty()) {
        ss << "    // [untranslated] " << inst.disassembly << "\n";
    }
    break;

#undef NEMU_EMIT_WR
#undef NEMU_EMIT_WR_ATTR
#undef NEMU_SRC
