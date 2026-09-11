#include "x64_emitter.hpp"
#include <cstring>

namespace nemu::core::cpu::jit {

void X64Emitter::Emit32(u32 val) {
    code_.push_back(static_cast<u8>(val & 0xFF));
    code_.push_back(static_cast<u8>((val >> 8) & 0xFF));
    code_.push_back(static_cast<u8>((val >> 16) & 0xFF));
    code_.push_back(static_cast<u8>((val >> 24) & 0xFF));
}

void X64Emitter::Emit64(u64 val) {
    Emit32(static_cast<u32>(val & 0xFFFFFFFF));
    Emit32(static_cast<u32>(val >> 32));
}

void X64Emitter::EmitRex(bool w, bool r, bool x, bool b) {
    const u8 rex = 0x40 | (w ? 8 : 0) | (r ? 4 : 0) | (x ? 2 : 0) | (b ? 1 : 0);
    EmitByte(rex);
}

void X64Emitter::EmitModRM(u8 mod, u8 reg, u8 rm) {
    EmitByte(static_cast<u8>((mod << 6) | ((reg & 7) << 3) | (rm & 7)));
}

void X64Emitter::EmitSIB(u8 scale, u8 index, u8 base) {
    EmitByte(static_cast<u8>((scale << 6) | ((index & 7) << 3) | (base & 7)));
}

void X64Emitter::Push(X64Reg reg) {
    const u8 r = static_cast<u8>(reg);
    if (r >= 8) {
        EmitByte(0x41);
        EmitByte(static_cast<u8>(0x50 + (r & 7)));
    } else {
        EmitByte(static_cast<u8>(0x50 + r));
    }
}

void X64Emitter::Pop(X64Reg reg) {
    const u8 r = static_cast<u8>(reg);
    if (r >= 8) {
        EmitByte(0x41);
        EmitByte(static_cast<u8>(0x58 + (r & 7)));
    } else {
        EmitByte(static_cast<u8>(0x58 + r));
    }
}

void X64Emitter::Ret() {
    EmitByte(0xC3);
}

void X64Emitter::MovR64Imm(X64Reg dst, u64 imm) {
    const u8 r = static_cast<u8>(dst);
    EmitRex(true, false, false, r >= 8);
    EmitByte(static_cast<u8>(0xB8 + (r & 7)));
    Emit64(imm);
}

void X64Emitter::MovR64R64(X64Reg dst, X64Reg src) {
    const u8 d = static_cast<u8>(dst);
    const u8 s = static_cast<u8>(src);
    EmitRex(true, s >= 8, false, d >= 8);
    EmitByte(0x89);
    EmitModRM(3, s, d);
}

void X64Emitter::MovR64Mem(X64Reg dst, X64Reg base, s32 disp) {
    const u8 d = static_cast<u8>(dst);
    const u8 b = static_cast<u8>(base);
    EmitRex(true, d >= 8, false, b >= 8);
    EmitByte(0x8B);
    EmitModRM(2, d, b);
    if ((b & 7) == 4) { // RSP / R12 requires SIB
        EmitSIB(0, 4, 4);
    }
    Emit32(static_cast<u32>(disp));
}

void X64Emitter::MovMemR64(X64Reg base, s32 disp, X64Reg src) {
    const u8 b = static_cast<u8>(base);
    const u8 s = static_cast<u8>(src);
    EmitRex(true, s >= 8, false, b >= 8);
    EmitByte(0x89);
    EmitModRM(2, s, b);
    if ((b & 7) == 4) { // RSP / R12 requires SIB
        EmitSIB(0, 4, 4);
    }
    Emit32(static_cast<u32>(disp));
}

void X64Emitter::AddR64R64(X64Reg dst, X64Reg src) {
    const u8 d = static_cast<u8>(dst);
    const u8 s = static_cast<u8>(src);
    EmitRex(true, s >= 8, false, d >= 8);
    EmitByte(0x01);
    EmitModRM(3, s, d);
}

void X64Emitter::SubR64R64(X64Reg dst, X64Reg src) {
    const u8 d = static_cast<u8>(dst);
    const u8 s = static_cast<u8>(src);
    EmitRex(true, s >= 8, false, d >= 8);
    EmitByte(0x29);
    EmitModRM(3, s, d);
}

void X64Emitter::AndR64R64(X64Reg dst, X64Reg src) {
    const u8 d = static_cast<u8>(dst);
    const u8 s = static_cast<u8>(src);
    EmitRex(true, s >= 8, false, d >= 8);
    EmitByte(0x21);
    EmitModRM(3, s, d);
}

void X64Emitter::OrR64R64(X64Reg dst, X64Reg src) {
    const u8 d = static_cast<u8>(dst);
    const u8 s = static_cast<u8>(src);
    EmitRex(true, s >= 8, false, d >= 8);
    EmitByte(0x09);
    EmitModRM(3, s, d);
}

void X64Emitter::XorR64R64(X64Reg dst, X64Reg src) {
    const u8 d = static_cast<u8>(dst);
    const u8 s = static_cast<u8>(src);
    EmitRex(true, s >= 8, false, d >= 8);
    EmitByte(0x31);
    EmitModRM(3, s, d);
}

void X64Emitter::AddR64Imm32(X64Reg dst, s32 imm) {
    const u8 d = static_cast<u8>(dst);
    EmitRex(true, false, false, d >= 8);
    EmitByte(0x81);
    EmitModRM(3, 0, d);
    Emit32(static_cast<u32>(imm));
}

} // namespace nemu::core::cpu::jit
