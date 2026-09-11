#pragma once

#include "core/types.hpp"
#include <vector>
#include <span>
#include <cstddef>

namespace nemu::core::cpu::jit {

enum class X64Reg : u8 {
    RAX = 0,
    RCX = 1,
    RDX = 2,
    RBX = 3,
    RSP = 4,
    RBP = 5,
    RSI = 6,
    RDI = 7,
    R8  = 8,
    R9  = 9,
    R10 = 10,
    R11 = 11,
    R12 = 12,
    R13 = 13,
    R14 = 14,
    R15 = 15
};

// x86-64 condition-code mnemonics (the low nibble of the SETcc / CMOVcc opcode).
enum class Cc : u8 {
    O  = 0x0, // Overflow (OF == 1)
    NO = 0x1, // Not overflow
    B  = 0x2, // Below (unsigned, CF == 1)
    AE = 0x3, // Above or equal (CF == 0)
    E  = 0x4, // Equal (ZF == 1)
    NE = 0x5, // Not equal
    BE = 0x6, // Below or equal
    A  = 0x7, // Above
    S  = 0x8, // Sign (SF == 1)
    NS = 0x9, // Not sign
    P  = 0xA, // Parity even
    NP = 0xB, // Parity odd
    L  = 0xC, // Less (SF != OF)
    GE = 0xD, // Greater or equal (SF == OF)
    LE = 0xE, // Less or equal
    G  = 0xF  // Greater
};

class X64Emitter {
public:
    X64Emitter() = default;

    void Clear() { code_.clear(); }
    [[nodiscard]] const std::vector<u8>& GetCode() const noexcept { return code_; }
    [[nodiscard]] size_t GetSize() const noexcept { return code_.size(); }

    void EmitByte(u8 b) { code_.push_back(b); }
    void Emit32(u32 val);
    void Emit64(u64 val);

    // Instructions
    void Push(X64Reg reg);
    void Pop(X64Reg reg);
    void Ret();

    /// Indirect call through a register (E9-style FF /2). Used to jump into a
    /// native dispatch stub. The stub address must be resident for the lifetime
    /// of the compiled block.
    void CallR64(X64Reg reg);

    void MovR64Imm(X64Reg dst, u64 imm);
    void MovR64R64(X64Reg dst, X64Reg src);
    void MovR64Mem(X64Reg dst, X64Reg base, s32 disp);
    void MovMemR64(X64Reg base, s32 disp, X64Reg src);

    // 32-bit variants (zero-extend into the destination's upper half).
    void MovR32Imm(X64Reg dst, u32 imm);
    void MovR32R32(X64Reg dst, X64Reg src);
    void MovR32Mem(X64Reg dst, X64Reg base, s32 disp);
    void MovMemR32(X64Reg base, s32 disp, X64Reg src);

    /// movzx dst, byte ptr [base + disp] -- zero-extend a byte (0..255) to 64-bit.
    void MovzxR64Mem8(X64Reg dst, X64Reg base, s32 disp);

    void AddR64R64(X64Reg dst, X64Reg src);
    void SubR64R64(X64Reg dst, X64Reg src);
    void AndR64R64(X64Reg dst, X64Reg src);
    void OrR64R64(X64Reg dst, X64Reg src);
    void XorR64R64(X64Reg dst, X64Reg src);

    // 32-bit logical/move (zero-extending).
    void AndR32R32(X64Reg dst, X64Reg src);
    void OrR32R32(X64Reg dst, X64Reg src);
    void XorR32R32(X64Reg dst, X64Reg src);
    void XorR32Imm(X64Reg dst, u32 imm);

    void AddR64Imm32(X64Reg dst, s32 imm);
    void SubR64Imm32(X64Reg dst, s32 imm);

    // Immediate shifts. Only the low 6 bits of the immediate are meaningful.
    void ShlR64Imm(X64Reg dst, u8 imm);
    void ShrR64Imm(X64Reg dst, u8 imm);
    void SarR64Imm(X64Reg dst, u8 imm);
    void RorR64Imm(X64Reg dst, u8 imm);
    void ShlR32Imm(X64Reg dst, u8 imm);
    void ShrR32Imm(X64Reg dst, u8 imm);
    void SarR32Imm(X64Reg dst, u8 imm);
    void RorR32Imm(X64Reg dst, u8 imm);

    /// SETcc byte ptr [base + disp] -- write 0x01 if the x86 condition holds,
    /// else 0x00. Completion of the block is not affected.
    void SetccMem(X64Reg base, s32 disp, Cc cc);

    /// CMOVcc dst, src (64-bit). Reads the flags set by a prior CMP/TEST/etc.
    void CmovccR64R64(X64Reg dst, X64Reg src, Cc cc);
    void CmovccR32R32(X64Reg dst, X64Reg src, Cc cc);

    /// cmp dst, imm8 -- sets ZF = (dst == imm), CF based on the arithmetic.
    void CmpR64Imm(X64Reg dst, s8 imm);

private:
    void EmitRex(bool w, bool r, bool x, bool b);
    void EmitModRM(u8 mod, u8 reg, u8 rm);
    void EmitSIB(u8 scale, u8 index, u8 base);

    std::vector<u8> code_;
};

} // namespace nemu::core::cpu::jit
