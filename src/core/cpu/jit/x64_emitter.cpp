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

void X64Emitter::CallR64(X64Reg reg) {
    const u8 r = static_cast<u8>(reg);
    // FF /2: call r/m64. REX.R selects high register on the r/m encoding.
    EmitRex(false, false, false, r >= 8);
    EmitByte(0xFF);
    EmitModRM(3, 2, r);
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

void X64Emitter::SubR64Imm32(X64Reg dst, s32 imm) {
    const u8 d = static_cast<u8>(dst);
    EmitRex(true, false, false, d >= 8);
    EmitByte(0x81);
    EmitModRM(3, 5, d);
    Emit32(static_cast<u32>(imm));
}

void X64Emitter::MovR32Imm(X64Reg dst, u32 imm) {
    const u8 d = static_cast<u8>(dst);
    EmitRex(false, false, false, d >= 8);
    EmitByte(static_cast<u8>(0xB8 + (d & 7)));
    Emit32(imm);
}

void X64Emitter::MovR32R32(X64Reg dst, X64Reg src) {
    const u8 d = static_cast<u8>(dst);
    const u8 s = static_cast<u8>(src);
    EmitRex(false, s >= 8, false, d >= 8);
    EmitByte(0x89);
    EmitModRM(3, s, d);
}

void X64Emitter::MovR32Mem(X64Reg dst, X64Reg base, s32 disp) {
    const u8 d = static_cast<u8>(dst);
    const u8 b = static_cast<u8>(base);
    EmitRex(false, d >= 8, false, b >= 8);
    EmitByte(0x8B);
    EmitModRM(2, d, b);
    if ((b & 7) == 4) {
        EmitSIB(0, 4, 4);
    }
    Emit32(static_cast<u32>(disp));
}

void X64Emitter::MovMemR32(X64Reg base, s32 disp, X64Reg src) {
    const u8 b = static_cast<u8>(base);
    const u8 s = static_cast<u8>(src);
    EmitRex(false, s >= 8, false, b >= 8);
    EmitByte(0x89);
    EmitModRM(2, s, b);
    if ((b & 7) == 4) {
        EmitSIB(0, 4, 4);
    }
    Emit32(static_cast<u32>(disp));
}

void X64Emitter::MovzxR64Mem8(X64Reg dst, X64Reg base, s32 disp) {
    const u8 d = static_cast<u8>(dst);
    const u8 b = static_cast<u8>(base);
    EmitRex(true, d >= 8, false, b >= 8);
    EmitByte(0x0F);
    EmitByte(0xB6);
    EmitModRM(2, d, b);
    if ((b & 7) == 4) {
        EmitSIB(0, 4, 4);
    }
    Emit32(static_cast<u32>(disp));
}

void X64Emitter::AndR32R32(X64Reg dst, X64Reg src) {
    const u8 d = static_cast<u8>(dst);
    const u8 s = static_cast<u8>(src);
    EmitRex(false, s >= 8, false, d >= 8);
    EmitByte(0x21);
    EmitModRM(3, s, d);
}

void X64Emitter::OrR32R32(X64Reg dst, X64Reg src) {
    const u8 d = static_cast<u8>(dst);
    const u8 s = static_cast<u8>(src);
    EmitRex(false, s >= 8, false, d >= 8);
    EmitByte(0x09);
    EmitModRM(3, s, d);
}

void X64Emitter::XorR32R32(X64Reg dst, X64Reg src) {
    const u8 d = static_cast<u8>(dst);
    const u8 s = static_cast<u8>(src);
    EmitRex(false, s >= 8, false, d >= 8);
    EmitByte(0x31);
    EmitModRM(3, s, d);
}

void X64Emitter::XorR32Imm(X64Reg dst, u32 imm) {
    const u8 d = static_cast<u8>(dst);
    EmitRex(false, false, false, d >= 8);
    EmitByte(0x81);
    EmitModRM(3, 6, d);
    Emit32(imm);
}

void X64Emitter::ShlR64Imm(X64Reg dst, u8 imm) {
    const u8 d = static_cast<u8>(dst);
    EmitRex(true, false, false, d >= 8);
    EmitByte(0xC1);
    EmitModRM(3, 4, d);
    EmitByte(imm & 0x3F);
}
void X64Emitter::ShrR64Imm(X64Reg dst, u8 imm) {
    const u8 d = static_cast<u8>(dst);
    EmitRex(true, false, false, d >= 8);
    EmitByte(0xC1);
    EmitModRM(3, 5, d);
    EmitByte(imm & 0x3F);
}
void X64Emitter::SarR64Imm(X64Reg dst, u8 imm) {
    const u8 d = static_cast<u8>(dst);
    EmitRex(true, false, false, d >= 8);
    EmitByte(0xC1);
    EmitModRM(3, 7, d);
    EmitByte(imm & 0x3F);
}
void X64Emitter::RorR64Imm(X64Reg dst, u8 imm) {
    const u8 d = static_cast<u8>(dst);
    EmitRex(true, false, false, d >= 8);
    EmitByte(0xC1);
    EmitModRM(3, 1, d);
    EmitByte(imm & 0x3F);
}

void X64Emitter::ShlR32Imm(X64Reg dst, u8 imm) {
    const u8 d = static_cast<u8>(dst);
    EmitRex(false, false, false, d >= 8);
    EmitByte(0xC1);
    EmitModRM(3, 4, d);
    EmitByte(imm & 0x1F);
}
void X64Emitter::ShrR32Imm(X64Reg dst, u8 imm) {
    const u8 d = static_cast<u8>(dst);
    EmitRex(false, false, false, d >= 8);
    EmitByte(0xC1);
    EmitModRM(3, 5, d);
    EmitByte(imm & 0x1F);
}
void X64Emitter::SarR32Imm(X64Reg dst, u8 imm) {
    const u8 d = static_cast<u8>(dst);
    EmitRex(false, false, false, d >= 8);
    EmitByte(0xC1);
    EmitModRM(3, 7, d);
    EmitByte(imm & 0x1F);
}
void X64Emitter::RorR32Imm(X64Reg dst, u8 imm) {
    const u8 d = static_cast<u8>(dst);
    EmitRex(false, false, false, d >= 8);
    EmitByte(0xC1);
    EmitModRM(3, 1, d);
    EmitByte(imm & 0x1F);
}

void X64Emitter::SetccMem(X64Reg base, s32 disp, Cc cc) {
    const u8 b = static_cast<u8>(base);
    EmitRex(false, false, false, b >= 8);
    EmitByte(0x0F);
    EmitByte(static_cast<u8>(0x90 + static_cast<u8>(cc)));
    EmitModRM(2, 0, b);
    if ((b & 7) == 4) {
        EmitSIB(0, 4, 4);
    }
    Emit32(static_cast<u32>(disp));
}

void X64Emitter::CmovccR64R64(X64Reg dst, X64Reg src, Cc cc) {
    const u8 d = static_cast<u8>(dst);
    const u8 s = static_cast<u8>(src);
    EmitRex(true, d >= 8, false, s >= 8);
    EmitByte(0x0F);
    EmitByte(static_cast<u8>(0x40 + static_cast<u8>(cc)));
    EmitModRM(3, d, s);
}

void X64Emitter::CmovccR32R32(X64Reg dst, X64Reg src, Cc cc) {
    const u8 d = static_cast<u8>(dst);
    const u8 s = static_cast<u8>(src);
    EmitRex(false, d >= 8, false, s >= 8);
    EmitByte(0x0F);
    EmitByte(static_cast<u8>(0x40 + static_cast<u8>(cc)));
    EmitModRM(3, d, s);
}

void X64Emitter::CmpR64Imm(X64Reg dst, s8 imm) {
    const u8 d = static_cast<u8>(dst);
    EmitRex(true, false, false, d >= 8);
    EmitByte(0x83);
    EmitModRM(3, 7, d);
    EmitByte(static_cast<u8>(imm));
}

void X64Emitter::MovssXmmMem(XmmReg dst, X64Reg base, s32 disp) {
    const u8 d = static_cast<u8>(dst);
    const u8 b = static_cast<u8>(base);
    EmitByte(0xF3);
    EmitRex(false, d >= 8, false, b >= 8);
    EmitByte(0x0F);
    EmitByte(0x10);
    EmitModRM(2, d, b);
    if ((b & 7) == 4) EmitSIB(0, 4, 4);
    Emit32(static_cast<u32>(disp));
}

void X64Emitter::MovssMemXmm(X64Reg base, s32 disp, XmmReg src) {
    const u8 b = static_cast<u8>(base);
    const u8 s = static_cast<u8>(src);
    EmitByte(0xF3);
    EmitRex(false, s >= 8, false, b >= 8);
    EmitByte(0x0F);
    EmitByte(0x11);
    EmitModRM(2, s, b);
    if ((b & 7) == 4) EmitSIB(0, 4, 4);
    Emit32(static_cast<u32>(disp));
}

void X64Emitter::MovsdXmmMem(XmmReg dst, X64Reg base, s32 disp) {
    const u8 d = static_cast<u8>(dst);
    const u8 b = static_cast<u8>(base);
    EmitByte(0xF2);
    EmitRex(false, d >= 8, false, b >= 8);
    EmitByte(0x0F);
    EmitByte(0x10);
    EmitModRM(2, d, b);
    if ((b & 7) == 4) EmitSIB(0, 4, 4);
    Emit32(static_cast<u32>(disp));
}

void X64Emitter::MovsdMemXmm(X64Reg base, s32 disp, XmmReg src) {
    const u8 b = static_cast<u8>(base);
    const u8 s = static_cast<u8>(src);
    EmitByte(0xF2);
    EmitRex(false, s >= 8, false, b >= 8);
    EmitByte(0x0F);
    EmitByte(0x11);
    EmitModRM(2, s, b);
    if ((b & 7) == 4) EmitSIB(0, 4, 4);
    Emit32(static_cast<u32>(disp));
}

void X64Emitter::MovdqaXmmMem(XmmReg dst, X64Reg base, s32 disp) {
    const u8 d = static_cast<u8>(dst);
    const u8 b = static_cast<u8>(base);
    EmitByte(0x66);
    EmitRex(false, d >= 8, false, b >= 8);
    EmitByte(0x0F);
    EmitByte(0x6F);
    EmitModRM(2, d, b);
    if ((b & 7) == 4) EmitSIB(0, 4, 4);
    Emit32(static_cast<u32>(disp));
}

void X64Emitter::MovdqaMemXmm(X64Reg base, s32 disp, XmmReg src) {
    const u8 b = static_cast<u8>(base);
    const u8 s = static_cast<u8>(src);
    EmitByte(0x66);
    EmitRex(false, s >= 8, false, b >= 8);
    EmitByte(0x0F);
    EmitByte(0x7F);
    EmitModRM(2, s, b);
    if ((b & 7) == 4) EmitSIB(0, 4, 4);
    Emit32(static_cast<u32>(disp));
}

void X64Emitter::Addss(XmmReg dst, XmmReg src) {
    EmitByte(0xF3);
    EmitRex(false, static_cast<u8>(dst) >= 8, false, static_cast<u8>(src) >= 8);
    EmitByte(0x0F); EmitByte(0x58);
    EmitModRM(3, static_cast<u8>(dst), static_cast<u8>(src));
}

void X64Emitter::Subss(XmmReg dst, XmmReg src) {
    EmitByte(0xF3);
    EmitRex(false, static_cast<u8>(dst) >= 8, false, static_cast<u8>(src) >= 8);
    EmitByte(0x0F); EmitByte(0x5C);
    EmitModRM(3, static_cast<u8>(dst), static_cast<u8>(src));
}

void X64Emitter::Mulss(XmmReg dst, XmmReg src) {
    EmitByte(0xF3);
    EmitRex(false, static_cast<u8>(dst) >= 8, false, static_cast<u8>(src) >= 8);
    EmitByte(0x0F); EmitByte(0x59);
    EmitModRM(3, static_cast<u8>(dst), static_cast<u8>(src));
}

void X64Emitter::Divss(XmmReg dst, XmmReg src) {
    EmitByte(0xF3);
    EmitRex(false, static_cast<u8>(dst) >= 8, false, static_cast<u8>(src) >= 8);
    EmitByte(0x0F); EmitByte(0x5E);
    EmitModRM(3, static_cast<u8>(dst), static_cast<u8>(src));
}

void X64Emitter::Sqrtss(XmmReg dst, XmmReg src) {
    EmitByte(0xF3);
    EmitRex(false, static_cast<u8>(dst) >= 8, false, static_cast<u8>(src) >= 8);
    EmitByte(0x0F); EmitByte(0x51);
    EmitModRM(3, static_cast<u8>(dst), static_cast<u8>(src));
}

void X64Emitter::Addsd(XmmReg dst, XmmReg src) {
    EmitByte(0xF2);
    EmitRex(false, static_cast<u8>(dst) >= 8, false, static_cast<u8>(src) >= 8);
    EmitByte(0x0F); EmitByte(0x58);
    EmitModRM(3, static_cast<u8>(dst), static_cast<u8>(src));
}

void X64Emitter::Subsd(XmmReg dst, XmmReg src) {
    EmitByte(0xF2);
    EmitRex(false, static_cast<u8>(dst) >= 8, false, static_cast<u8>(src) >= 8);
    EmitByte(0x0F); EmitByte(0x5C);
    EmitModRM(3, static_cast<u8>(dst), static_cast<u8>(src));
}

void X64Emitter::Mulsd(XmmReg dst, XmmReg src) {
    EmitByte(0xF2);
    EmitRex(false, static_cast<u8>(dst) >= 8, false, static_cast<u8>(src) >= 8);
    EmitByte(0x0F); EmitByte(0x59);
    EmitModRM(3, static_cast<u8>(dst), static_cast<u8>(src));
}

void X64Emitter::Divsd(XmmReg dst, XmmReg src) {
    EmitByte(0xF2);
    EmitRex(false, static_cast<u8>(dst) >= 8, false, static_cast<u8>(src) >= 8);
    EmitByte(0x0F); EmitByte(0x5E);
    EmitModRM(3, static_cast<u8>(dst), static_cast<u8>(src));
}

void X64Emitter::Sqrtsd(XmmReg dst, XmmReg src) {
    EmitByte(0xF2);
    EmitRex(false, static_cast<u8>(dst) >= 8, false, static_cast<u8>(src) >= 8);
    EmitByte(0x0F); EmitByte(0x51);
    EmitModRM(3, static_cast<u8>(dst), static_cast<u8>(src));
}

void X64Emitter::Ucomiss(XmmReg a, XmmReg b) {
    EmitRex(false, static_cast<u8>(a) >= 8, false, static_cast<u8>(b) >= 8);
    EmitByte(0x0F); EmitByte(0x2E);
    EmitModRM(3, static_cast<u8>(a), static_cast<u8>(b));
}

void X64Emitter::Ucomisd(XmmReg a, XmmReg b) {
    EmitByte(0x66);
    EmitRex(false, static_cast<u8>(a) >= 8, false, static_cast<u8>(b) >= 8);
    EmitByte(0x0F); EmitByte(0x2E);
    EmitModRM(3, static_cast<u8>(a), static_cast<u8>(b));
}

void X64Emitter::Xorps(XmmReg dst, XmmReg src) {
    EmitRex(false, static_cast<u8>(dst) >= 8, false, static_cast<u8>(src) >= 8);
    EmitByte(0x0F); EmitByte(0x57);
    EmitModRM(3, static_cast<u8>(dst), static_cast<u8>(src));
}

void X64Emitter::Xorpd(XmmReg dst, XmmReg src) {
    EmitByte(0x66);
    EmitRex(false, static_cast<u8>(dst) >= 8, false, static_cast<u8>(src) >= 8);
    EmitByte(0x0F); EmitByte(0x57);
    EmitModRM(3, static_cast<u8>(dst), static_cast<u8>(src));
}

void X64Emitter::Addps(XmmReg dst, XmmReg src) {
    EmitRex(false, static_cast<u8>(dst) >= 8, false, static_cast<u8>(src) >= 8);
    EmitByte(0x0F); EmitByte(0x58);
    EmitModRM(3, static_cast<u8>(dst), static_cast<u8>(src));
}

void X64Emitter::Subps(XmmReg dst, XmmReg src) {
    EmitRex(false, static_cast<u8>(dst) >= 8, false, static_cast<u8>(src) >= 8);
    EmitByte(0x0F); EmitByte(0x5C);
    EmitModRM(3, static_cast<u8>(dst), static_cast<u8>(src));
}

void X64Emitter::Mulps(XmmReg dst, XmmReg src) {
    EmitRex(false, static_cast<u8>(dst) >= 8, false, static_cast<u8>(src) >= 8);
    EmitByte(0x0F); EmitByte(0x59);
    EmitModRM(3, static_cast<u8>(dst), static_cast<u8>(src));
}

void X64Emitter::Andps(XmmReg dst, XmmReg src) {
    EmitRex(false, static_cast<u8>(dst) >= 8, false, static_cast<u8>(src) >= 8);
    EmitByte(0x0F); EmitByte(0x54);
    EmitModRM(3, static_cast<u8>(dst), static_cast<u8>(src));
}

void X64Emitter::Orps(XmmReg dst, XmmReg src) {
    EmitRex(false, static_cast<u8>(dst) >= 8, false, static_cast<u8>(src) >= 8);
    EmitByte(0x0F); EmitByte(0x56);
    EmitModRM(3, static_cast<u8>(dst), static_cast<u8>(src));
}

void X64Emitter::Cvtsi2ss(XmmReg dst, X64Reg src, bool is_64bit) {
    EmitByte(0xF3);
    EmitRex(is_64bit, static_cast<u8>(dst) >= 8, false, static_cast<u8>(src) >= 8);
    EmitByte(0x0F); EmitByte(0x2A);
    EmitModRM(3, static_cast<u8>(dst), static_cast<u8>(src));
}

void X64Emitter::Cvtsi2sd(XmmReg dst, X64Reg src, bool is_64bit) {
    EmitByte(0xF2);
    EmitRex(is_64bit, static_cast<u8>(dst) >= 8, false, static_cast<u8>(src) >= 8);
    EmitByte(0x0F); EmitByte(0x2A);
    EmitModRM(3, static_cast<u8>(dst), static_cast<u8>(src));
}

void X64Emitter::Cvttss2si(X64Reg dst, XmmReg src, bool is_64bit) {
    EmitByte(0xF3);
    EmitRex(is_64bit, static_cast<u8>(dst) >= 8, false, static_cast<u8>(src) >= 8);
    EmitByte(0x0F); EmitByte(0x2C);
    EmitModRM(3, static_cast<u8>(dst), static_cast<u8>(src));
}

void X64Emitter::Cvttsd2si(X64Reg dst, XmmReg src, bool is_64bit) {
    EmitByte(0xF2);
    EmitRex(is_64bit, static_cast<u8>(dst) >= 8, false, static_cast<u8>(src) >= 8);
    EmitByte(0x0F); EmitByte(0x2C);
    EmitModRM(3, static_cast<u8>(dst), static_cast<u8>(src));
}

void X64Emitter::Cvtss2sd(XmmReg dst, XmmReg src) {
    EmitByte(0xF3);
    EmitRex(false, static_cast<u8>(dst) >= 8, false, static_cast<u8>(src) >= 8);
    EmitByte(0x0F); EmitByte(0x5A);
    EmitModRM(3, static_cast<u8>(dst), static_cast<u8>(src));
}

void X64Emitter::Cvtsd2ss(XmmReg dst, XmmReg src) {
    EmitByte(0xF2);
    EmitRex(false, static_cast<u8>(dst) >= 8, false, static_cast<u8>(src) >= 8);
    EmitByte(0x0F); EmitByte(0x5A);
    EmitModRM(3, static_cast<u8>(dst), static_cast<u8>(src));
}

void X64Emitter::LockXaddMemR64(X64Reg base, s32 disp, X64Reg src, bool is_64bit) {
    const u8 b = static_cast<u8>(base);
    const u8 s = static_cast<u8>(src);
    EmitByte(0xF0); // LOCK
    EmitRex(is_64bit, s >= 8, false, b >= 8);
    EmitByte(0x0F); EmitByte(0xC1);
    EmitModRM(2, s, b);
    if ((b & 7) == 4) EmitSIB(0, 4, 4);
    Emit32(static_cast<u32>(disp));
}

void X64Emitter::LockCmpxchgMemR64(X64Reg base, s32 disp, X64Reg src, bool is_64bit) {
    const u8 b = static_cast<u8>(base);
    const u8 s = static_cast<u8>(src);
    EmitByte(0xF0); // LOCK
    EmitRex(is_64bit, s >= 8, false, b >= 8);
    EmitByte(0x0F); EmitByte(0xB1);
    EmitModRM(2, s, b);
    if ((b & 7) == 4) EmitSIB(0, 4, 4);
    Emit32(static_cast<u32>(disp));
}

void X64Emitter::XchgMemR64(X64Reg base, s32 disp, X64Reg src, bool is_64bit) {
    const u8 b = static_cast<u8>(base);
    const u8 s = static_cast<u8>(src);
    EmitRex(is_64bit, s >= 8, false, b >= 8);
    EmitByte(0x87);
    EmitModRM(2, s, b);
    if ((b & 7) == 4) EmitSIB(0, 4, 4);
    Emit32(static_cast<u32>(disp));
}

} // namespace nemu::core::cpu::jit
