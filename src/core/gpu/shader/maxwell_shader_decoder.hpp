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
    std::string disassembly{};
};

struct DecompiledProgram {
    ShaderStage stage{ShaderStage::Vertex};
    std::vector<DecodedInstruction> instructions{};
    std::string hlsl_source{};
    std::string glsl_source{};
    std::vector<u32> used_cbuf_banks{};
    std::vector<u32> used_textures{};
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
