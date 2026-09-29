#pragma once

#include "core/types.hpp"
#include <span>
#include <string>
#include <vector>
#include <optional>
#include <unordered_map>
#include <string_view>

namespace nemu::core::gpu::shader {

enum class ShaderStage : u32 {
    Vertex,
    TessellationControl,
    TessellationEval,
    Geometry,
    Fragment, // Pixel shader
    Compute,
};

#ifdef OUT
#undef OUT
#endif
#ifdef IN
#undef IN
#endif

enum class MaxwellOpcode : u32 {
    NOP = 0,
    // Float arithmetic
    FADD,
    FSUB,
    FMUL,
    FFMA,
    FMNMX,
    FCMP,
    F2F,
    F2I,
    I2F,
    // Integer arithmetic
    IADD,
    IADD3,
    ISUB,
    IMUL,
    ISCADD,
    LEA,
    // Logical & Shifts
    LOP,
    LOP3,
    SHL,
    SHR,
    BFE,
    BFI,
    // Movement & Selection
    MOV,
    SEL,
    // Memory & Constant Buffers
    LDC,      // Load Constant
    LDG,      // Load Global
    STG,      // Store Global
    LDS,      // Load Shared
    STS,      // Store Shared
    // Texture
    TEX,      // Texture Sample
    TEXS,
    TLD,      // Texture Load
    TXQ,      // Texture Query
    // Flow control & Special
    BRA,      // Branch
    EXIT,     // Shader Return
    KIL,      // Discard fragment
    SYNC,     // Barrier
    S2R,      // Special register
    // Attributes
    AL2P,
    LD_ATTR,
    ST_ATTR,
    // --- Extended SASS families (Tier-A1 IR layer; ported from yuzu
    //     maxwell.inc families with HLSL emission in sass_emit_extended.inc)
    // Integer multiply-add / immediate ALU
    IMAD, IMAD32I, IMADSP, XMAD, IADD32I, FMUL32I, FADD32I, FFMA32I,
    ISCADD32I, IMUL32I, LOP32I, MOV32I,
    // Float/int compare + select
    ICMP, FSET, FSETP, ISET, ISETP, PSET, PSETP, CSET, CSETP,
    // Float min/max + double family (fp32-mapped)
    DMNMX, IMNMX, DFMA, DMUL, DADD,
    // Memory: generic + local
    LD, LDL, STL, ST, STP, LDP,
    // Special function + bit ops
    MUFU, FLO, POPC, PRMT, FCHK, I2I,
    // Interpolation + predicate plumbing
    IPA, P2R, R2P, R2B, B2R, CS2R,
    // Half-float family (fp32-computed)
    HFMA2, HFMA2_32I, HADD2, HADD2_32I, HMUL2, HMUL2_32I, HSET2, HSETP2,
    // Texture variants
    TLDS, TLD4, TLD4S, TEX_b, TLD_b, TXD, TXD_b, TMML, TMML_b, TXA,
    // Flow control extended
    JMP, JMX, BRX, PBK, PCNT, PEXIT, LONGJMP, PLONGJMP, JCAL, CAL, RET,
    PRET, RTT, BRK, CONT, SSY,
    // Warp/consensus + barriers
    VOTE, VOTE_vtg, BAR, DEPBAR, MEMBAR, SHFL, FSWZADD, LEA_hi, LEA_lo,
    // Funnel shift & bit manipulation
    SHF_l, SHF_r,
    // Surface loads, stores & atomics
    SULD, SUST, SURED, SUATOM, ATOM, ATOMS, RED,
    // Double precision comparison & min/max
    DSET, DSETP, DMIN, DMAX,
    // SIMD packed video operations
    VABSDIFF, VABSDIFF4, VADD, VMAD, VMNMX, VSET, VSETP, VSHL, VSHR,
    // Attributes & transform feedback / stream output
    ALD, AST, OUT_stream,
    // Pixel, cache control & math
    PIXLD, CCTL, CCTLL, RRO,
    // Bindless texture variants
    TLD4_b, TXQ_b,
    // System & pointer manipulation
    BPT, GETCRSPTR, GETLMEMBASE, SETCRSPTR, SETLMEMBASE, IDE, IDP, ISBERD, LEPC, RAM, SAM,
    UNKNOWN,
};

enum class OperandType : u32 {
    Register,
    ImmediateInt,
    ImmediateFloat,
    ConstantBuffer,
    SpecialRegister,
    Attribute,
};

struct ConstantBufferRef {
    u32 bank{0};
    u32 offset{0};
};

struct AttributeRef {
    u32 offset{0};
    u32 component{0}; // 0=x, 1=y, 2=z, 3=w
};

struct ShaderOperand {
    OperandType type{OperandType::Register};
    u32 reg_index{255}; // 255 = RZ (Zero register)
    s32 imm_int{0};
    float imm_float{0.0f};
    ConstantBufferRef cbuf{};
    u32 special_reg{0};
    AttributeRef attr{};
    bool negate{false};
    bool absolute{false};
};

struct DecodedInstruction {
    u64 offset_bytes{0};
    MaxwellOpcode opcode{MaxwellOpcode::UNKNOWN};
    u32 predicate{7}; // P0..P6, PT=7 (Always True)
    bool predicate_invert{false};

    ShaderOperand dest{};
    std::vector<ShaderOperand> sources{};

    u64 branch_target{0};
    // SET/SETP family comparison op (bits [51:49] per switchbrew SASS layout):
    // 0=FALSE 1=LT 2=EQ 3=LE 4=GT 5=NE 6=GE 7=TRUE. Valid when set_op_valid.
    u32 set_op{1}; // default LT (previous emitter behavior)
    bool set_op_valid{false};
    std::string disassembly{};
};

struct DecompiledProgram {
    ShaderStage stage{ShaderStage::Vertex};
    std::vector<DecodedInstruction> instructions{};
    std::string hlsl_source{};
    std::string glsl_source{};
    std::vector<u32> used_cbuf_banks{};
    std::vector<u32> used_textures{};
    std::vector<u32> used_surfaces{};  // UAV binding slots (SULD/SUST/SURED/SUATOM)
    std::vector<u32> used_attrs{};  // attribute slot indices actually referenced
    u32 max_register_used{0};
    bool has_discard{false};
};

class MaxwellShaderDecoder {
public:
    /// Decodes a Maxwell binary shader microcode stream
    /// @param code Bytecode buffer
    /// @param stage Target shader stage (Vertex, Fragment, etc.)
    static DecompiledProgram DecodeAndDecompile(
        std::span<const u8> code,
        ShaderStage stage,
        bool has_control_codes = false
    );

    /// Disassembles a single instruction to text representation
    static std::string DisassembleInstruction(const DecodedInstruction& inst);

    /// Translates decoded program into Direct3D 12 HLSL (Shader Model 5.1/6.0)
    static std::string EmitHLSL(const DecompiledProgram& program);

    /// Translates decoded program into GLSL / SPIR-V compatible source
    static std::string EmitGLSL(const DecompiledProgram& program);

private:
    static DecodedInstruction DecodeInstruction64(u64 raw_inst, u64 offset);
};

} // namespace nemu::core::gpu::shader
