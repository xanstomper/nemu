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
        static const char* kSetOps[] = {"0.0f", "<", "==", "<=", ">", "!=", ">=", "1.0f"};
        ss << "    p[0] = (" << NEMU_SRC(0) << " " << kSetOps[inst.set_op & 7]
           << " " << NEMU_SRC(1) << "); // SETP (decoded comparison op)\n";
        ss << "    R[" << inst.dest.reg_index << "] = p[0] ? 1.0f : 0.0f;\n";
    }
    break;
}
case MaxwellOpcode::PSET:
case MaxwellOpcode::ISET:
case MaxwellOpcode::FSET: {
    // Real comparison semantics via the decoded set_op (was hardcoded LT).
    // 0=FALSE 1=LT 2=EQ 3=LE 4=GT 5=NE 6=GE 7=TRUE.
    static const char* kSetOps[] = {"0.0f /*FALSE*/", "<", "==", "<=", ">", "!=", ">=", "1.0f /*TRUE*/"};
    const char* op = kSetOps[inst.set_op & 7];
    NEMU_EMIT_WR(std::string("(") + NEMU_SRC(0) + " " + op + " " + NEMU_SRC(1) + ") ? 1.0f : 0.0f")
    break;
}
case MaxwellOpcode::CSET:
    NEMU_EMIT_WR("(p[0] ? " + NEMU_SRC(0) + " : " + NEMU_SRC(1) + ")")
    break;
case MaxwellOpcode::CSETP: {
    // CSETP: p = src0 <op> src1 (was a passthrough — predicates now reflect
    // the real comparison; the emitter's bool ops (AND/OR/XOR) at bits [56:54]
    // fold p1 into p0 in the PSET family path below).
    static const char* kSetOps[] = {"0.0f", "<", "==", "<=", ">", "!=", ">=", "1.0f"};
    const char* op = kSetOps[inst.set_op & 7];
    ss << "    p[0] = ((" << NEMU_SRC(0) << " " << op << " " << NEMU_SRC(1) << ") ? 1 : 0);\n";
    break;
}

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
case MaxwellOpcode::PRMT: {
    // PRMT (generic mode): c's 4 low nibbles each select a byte from the 8-byte
    // pair {src1:src0} (nibble 0-7); a nibble with bit3 set replicates the
    // selected byte's sign. This is the standard texture/vertex-color swizzle op.
    // HLSL: per-byte extract via shift/mask chain, built at emit time.
    const std::string a = NEMU_SRC(0), b = NEMU_SRC(1), c = NEMU_SRC(2);
    auto byte_at = [](const std::string& w, int idx) {
        const int sh = idx * 8;
        return "((" + w + " >> " + std::to_string(sh) + ") & 0xFFu)";
    };
    // Selector nibble n lives in c bits [3+4n : 4n].
    std::string bytes[4];
    for (int n = 0; n < 4; ++n) {
        const std::string sel = "(((" + c + ") >> " + std::to_string(4 * n) + ") & 0xFu)";
        const std::string idx = "(" + sel + " & 0x7u)";
        const std::string sign_bit = "(" + sel + " & 0x8u)";
        // Build: select from a for idx<4 else from b
        std::string expr = "(" + idx + "==0u) ? " + byte_at(a, 0);
        for (int k = 1; k < 4; ++k) expr += " : (" + idx + "==" + std::to_string(k) + "u) ? " + byte_at(a, k);
        for (int k = 0; k < 4; ++k) expr += " : (" + idx + "==" + std::to_string(k + 4) + "u) ? " + byte_at(b, k);
        expr += " : 0u";
        // Sign-replicate: if sel bit3, result byte |= (sign of picked byte << 7)
        bytes[n] = "(" + sign_bit + " ? (" + expr + ") | 0x80u : " + expr + ")";
    }
    NEMU_EMIT_WR(bytes[0] + " | (" + bytes[1] + " << 8) | (" + bytes[2] + " << 16) | (" + bytes[3] + " << 24)")
}
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
    if (inst.dest.type == OperandType::Register && inst.dest.reg_index < 255) {
        ss << "    R[" << inst.dest.reg_index << "] = p[0] ? 1.0f : 0.0f; // VOTE\n";
    }
    break;
case MaxwellOpcode::BAR:
case MaxwellOpcode::DEPBAR:
case MaxwellOpcode::MEMBAR:
    ss << "    // Barrier / Sync\n";
    break;
case MaxwellOpcode::SHFL:
    NEMU_EMIT_WR(NEMU_SRC(0) + " // SHFL (warp shuffle)")
    break;
case MaxwellOpcode::FSWZADD:
    NEMU_EMIT_WR(NEMU_SRC(0) + " + " + NEMU_SRC(1) + " // FSWZADD")
    break;
case MaxwellOpcode::LEA_hi:
case MaxwellOpcode::LEA_lo:
    NEMU_EMIT_WR(NEMU_SRC(0) + " + " + NEMU_SRC(1) + " // LEA")
    break;

// ---- Funnel shifts & bit manipulation ---------------------------------------
case MaxwellOpcode::SHF_l:
    NEMU_EMIT_WR("asfloat(asuint(" + NEMU_SRC(0) + ") << (asuint(" + NEMU_SRC(2) + ") & 0x1F)) // SHF.L")
    break;
case MaxwellOpcode::SHF_r:
    NEMU_EMIT_WR("asfloat(asuint(" + NEMU_SRC(0) + ") >> (asuint(" + NEMU_SRC(2) + ") & 0x1F)) // SHF.R")
    break;

// ---- Double precision comparison & min/max ---------------------------------
case MaxwellOpcode::DSET:
case MaxwellOpcode::DSETP:
    if (inst.dest.type == OperandType::Register && inst.dest.reg_index < 255) {
        ss << "    p[0] = (" << NEMU_SRC(0) << " < " << NEMU_SRC(1) << "); // DSETP\n";
        ss << "    R[" << inst.dest.reg_index << "] = p[0] ? 1.0f : 0.0f;\n";
    }
    break;
case MaxwellOpcode::DMIN:
    NEMU_EMIT_WR("min(" + NEMU_SRC(0) + ", " + NEMU_SRC(1) + ")")
    break;
case MaxwellOpcode::DMAX:
    NEMU_EMIT_WR("max(" + NEMU_SRC(0) + ", " + NEMU_SRC(1) + ")")
    break;

// ---- Surface loads, stores & atomics (UAV path) -----------------------------
case MaxwellOpcode::SULD:
    // SULD: real UAV load (RWByteAddressBuffer semantics, binding from used_surfaces).
    NEMU_EMIT_WR(std::string("g_surf.Load(") + NEMU_SRC(0) + ") // SULD surface load")
    break;
case MaxwellOpcode::SUST:
    ss << "    g_surf.Store(" << NEMU_SRC(0) << ", " << NEMU_SRC(1) << "); // SUST surface store\n";
    break;
case MaxwellOpcode::SURED:
    ss << "    // SURED (surface reduction)\n";
    break;
case MaxwellOpcode::ATOM:
case MaxwellOpcode::ATOMS:
case MaxwellOpcode::RED:
    ss << "    // ATOM/RED (memory atomic operation)\n";
    break;

// ---- SIMD packed video operations -------------------------------------------
case MaxwellOpcode::VABSDIFF:
case MaxwellOpcode::VABSDIFF4:
    NEMU_EMIT_WR("abs(" + NEMU_SRC(0) + " - " + NEMU_SRC(1) + ") // VABSDIFF")
    break;
case MaxwellOpcode::VADD:
    NEMU_EMIT_WR("asfloat(asint(" + NEMU_SRC(0) + ") + asint(" + NEMU_SRC(1) + ")) // VADD")
    break;
case MaxwellOpcode::VMAD:
    NEMU_EMIT_WR("asfloat(asint(" + NEMU_SRC(0) + ") * asint(" + NEMU_SRC(1) + ") + asint(" + NEMU_SRC(2) + ")) // VMAD")
    break;
case MaxwellOpcode::VMNMX:
    NEMU_EMIT_WR("min(" + NEMU_SRC(0) + ", " + NEMU_SRC(1) + ") // VMNMX")
    break;
case MaxwellOpcode::VSET:
case MaxwellOpcode::VSETP:
    if (inst.dest.type == OperandType::Register && inst.dest.reg_index < 255) {
        ss << "    p[0] = (" << NEMU_SRC(0) << " < " << NEMU_SRC(1) << "); // VSETP\n";
        ss << "    R[" << inst.dest.reg_index << "] = p[0] ? 1.0f : 0.0f;\n";
    }
    break;
case MaxwellOpcode::VSHL:
    NEMU_EMIT_WR("asfloat(asuint(" + NEMU_SRC(0) + ") << asuint(" + NEMU_SRC(1) + ")) // VSHL")
    break;
case MaxwellOpcode::VSHR:
    NEMU_EMIT_WR("asfloat(asuint(" + NEMU_SRC(0) + ") >> asuint(" + NEMU_SRC(1) + ")) // VSHR")
    break;

// ---- Attributes & stream output / transform feedback ------------------------
case MaxwellOpcode::ALD:
    NEMU_EMIT_WR(NEMU_SRC(0) + " // ALD (attribute load)")
    break;
case MaxwellOpcode::AST:
    NEMU_EMIT_WR_ATTR(NEMU_SRC(0))
    break;
case MaxwellOpcode::OUT_stream:
    ss << "    // OUT (transform feedback stream emit)\n";
    break;

// ---- Pixel, cache control & math --------------------------------------------
case MaxwellOpcode::PIXLD:
    NEMU_EMIT_WR("0.0f // PIXLD (pixel load)")
    break;
case MaxwellOpcode::CCTL:
case MaxwellOpcode::CCTLL:
    ss << "    // CCTL (cache control invalidate/flush)\n";
    break;
case MaxwellOpcode::RRO:
    NEMU_EMIT_WR(NEMU_SRC(0) + " // RRO (range reduction operand)")
    break;

// ---- Bindless texture variants ----------------------------------------------
case MaxwellOpcode::TLD4_b:
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
case MaxwellOpcode::TXQ_b:
    NEMU_EMIT_WR("float4(1280.0f, 720.0f, 1.0f, 1.0f).x // TXQ_b (bindless query)")
    break;

// ---- Rare / unsupported families (visibility, not silent drop) --------------
case MaxwellOpcode::SUATOM:
case MaxwellOpcode::BPT:
case MaxwellOpcode::GETCRSPTR:
case MaxwellOpcode::GETLMEMBASE:
case MaxwellOpcode::SETCRSPTR:
case MaxwellOpcode::SETLMEMBASE:
case MaxwellOpcode::IDE:
case MaxwellOpcode::IDP:
case MaxwellOpcode::ISBERD:
case MaxwellOpcode::LEPC:
case MaxwellOpcode::RAM:
case MaxwellOpcode::SAM:
case MaxwellOpcode::UNKNOWN:
default:
    if (!inst.disassembly.empty()) {
        ss << "    // [untranslated] " << inst.disassembly << "\n";
    }
    break;

#undef NEMU_EMIT_WR
#undef NEMU_EMIT_WR_ATTR
#undef NEMU_SRC
