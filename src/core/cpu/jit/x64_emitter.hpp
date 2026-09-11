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

    void MovR64Imm(X64Reg dst, u64 imm);
    void MovR64R64(X64Reg dst, X64Reg src);
    void MovR64Mem(X64Reg dst, X64Reg base, s32 disp);
    void MovMemR64(X64Reg base, s32 disp, X64Reg src);

    void AddR64R64(X64Reg dst, X64Reg src);
    void SubR64R64(X64Reg dst, X64Reg src);
    void AndR64R64(X64Reg dst, X64Reg src);
    void OrR64R64(X64Reg dst, X64Reg src);
    void XorR64R64(X64Reg dst, X64Reg src);

    void AddR64Imm32(X64Reg dst, s32 imm);

private:
    void EmitRex(bool w, bool r, bool x, bool b);
    void EmitModRM(u8 mod, u8 reg, u8 rm);
    void EmitSIB(u8 scale, u8 index, u8 base);

    std::vector<u8> code_;
};

} // namespace nemu::core::cpu::jit
