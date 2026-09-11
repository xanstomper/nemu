#include "maxwell_shader_decoder.hpp"
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
    } else if (major_high == 0x530) {
        inst.opcode = MaxwellOpcode::LDC;
        u32 bank = static_cast<u32>((raw >> 20) & 0x1F);
        u32 c_offset = static_cast<u32>((raw >> 28) & 0xFFFF);
        ShaderOperand c_op{};
        c_op.type = OperandType::ConstantBuffer;
        c_op.cbuf = {bank, c_offset};
        inst.sources.push_back(c_op);
    } else if (major_high == 0x550) {
        inst.opcode = MaxwellOpcode::TEX;
        inst.sources.push_back(ShaderOperand{.type = OperandType::Register, .reg_index = ra});
        inst.sources.push_back(ShaderOperand{.type = OperandType::Register, .reg_index = rb});
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
    } else {
        inst.opcode = MaxwellOpcode::UNKNOWN;
    }

    inst.disassembly = DisassembleInstruction(inst);
    return inst;
}

DecompiledProgram MaxwellShaderDecoder::DecodeAndDecompile(
    std::span<const u8> code,
    ShaderStage stage,
    bool has_control_codes
) {
    DecompiledProgram program{};
    program.stage = stage;

    std::set<u32> used_cbufs;
    std::set<u32> used_texs;

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
        for (u32 i = 0; i < 8; ++i) {
            ss << "    float4 in_attr" << i << " : TEXCOORD" << i << ";\n";
        }
        ss << "};\n\n";

        ss << "struct VSOutput {\n";
        ss << "    float4 out_pos : SV_Position;\n";
        for (u32 i = 0; i < 8; ++i) {
            ss << "    float4 out_attr" << i << " : TEXCOORD" << i << ";\n";
        }
        ss << "};\n\n";

        ss << "VSOutput main(VSInput input) {\n";
        ss << "    VSOutput output = (VSOutput)0;\n";
    } else { // Fragment / Pixel Shader
        ss << "struct PSInput {\n";
        ss << "    float4 in_pos : SV_Position;\n";
        for (u32 i = 0; i < 8; ++i) {
            ss << "    float4 in_attr" << i << " : TEXCOORD" << i << ";\n";
        }
        ss << "};\n\n";

        ss << "struct PSOutput {\n";
        ss << "    float4 out_color0 : SV_Target0;\n";
        ss << "};\n\n";

        ss << "PSOutput main(PSInput input) {\n";
        ss << "    PSOutput output = (PSOutput)0;\n";
    }

    u32 num_regs = std::max(program.max_register_used + 1, 16u);
    ss << "    float R[" << num_regs << "];\n";
    ss << "    [unroll] for (int _i = 0; _i < " << num_regs << "; ++_i) R[_i] = 0.0f;\n\n";

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
                    ss << "    output.out_color0." << comp_char << " = " << OperandToHlsl(inst.sources[0], program.stage) << ";\n";
                }
                break;
            }
            case MaxwellOpcode::KIL:
                ss << "    discard;\n";
                break;
            case MaxwellOpcode::EXIT:
                ss << "    return output;\n";
                break;
            default:
                break;
        }
    }

    ss << "    return output;\n";
    ss << "}\n";

    return ss.str();
}

std::string MaxwellShaderDecoder::EmitGLSL(const DecompiledProgram& program) {
    std::ostringstream ss;
    ss << "#version 450 core\n";
    ss << "// Generated by Nemu Maxwell SASS -> GLSL\n\n";

    if (program.stage == ShaderStage::Vertex) {
        ss << "layout(location = 0) in vec4 in_pos;\n";
        for (u32 i = 0; i < 8; ++i) {
            ss << "layout(location = " << (i + 1) << ") in vec4 in_attr" << i << ";\n";
            ss << "layout(location = " << i << ") out vec4 out_attr" << i << ";\n";
        }
        ss << "\nvoid main() {\n";
        ss << "    gl_Position = in_pos;\n";
    } else {
        for (u32 i = 0; i < 8; ++i) {
            ss << "layout(location = " << i << ") in vec4 in_attr" << i << ";\n";
        }
        ss << "layout(location = 0) out vec4 out_color0;\n";
        ss << "\nvoid main() {\n";
        ss << "    out_color0 = vec4(1.0, 1.0, 1.0, 1.0);\n";
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
