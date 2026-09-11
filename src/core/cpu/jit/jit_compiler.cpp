#include "jit_compiler.hpp"
#include "core/cpu/decoder.hpp"
#include "platform/logger.hpp"
#include <cmath>
#include <cstring>
#include <functional>

namespace nemu::core::cpu::jit {

namespace {
    constexpr s32 OFFSET_PC = 256;
    constexpr size_t MAX_BLOCK_INSTRUCTIONS = 32;

    // CpuState (alignas 16) field offsets, relative to R15 = &CpuState.
    // Layout: x[0..30] at 0..247, sp at 248, pc at 256, pstate.n/z/c/v at 264..267.
    constexpr s32 OFF_SP = 248;
    constexpr s32 OFF_N  = 264;
    constexpr s32 OFF_Z  = 265;
    constexpr s32 OFF_C  = 266;
    constexpr s32 OFF_V  = 267;

    inline s32 VSlot(u32 reg) noexcept {
        return static_cast<s32>(offsetof(CpuState, v)) + static_cast<s32>(reg * sizeof(u128));
    }
    inline s32 ExclAddrSlot() noexcept {
        return static_cast<s32>(offsetof(CpuState, exclusive_addr));
    }
    inline s32 ExclActSlot() noexcept {
        return static_cast<s32>(offsetof(CpuState, exclusive_active));
    }

    // Calling-convention pointer registers for the SVC thunk. On System V,
    // CpuState* goes in RDI and svc_id in RSI; on Windows x64, RCX and RDX.
#ifdef _WIN32
    constexpr X64Reg REG_STATE_ARG = X64Reg::RCX;
    constexpr X64Reg REG_SVC_ARG  = X64Reg::RDX;
#else
    constexpr X64Reg REG_STATE_ARG = X64Reg::RDI;
    constexpr X64Reg REG_SVC_ARG  = X64Reg::RSI;
#endif

    // Scratch register holding the thunk target address across the arg setup.
    constexpr X64Reg REG_THUNK = X64Reg::R11;

    // Argument registers for the VirtualMemory load/store thunks.
#ifdef _WIN32
    constexpr X64Reg MEM_ARG1 = X64Reg::RCX;
    constexpr X64Reg MEM_ARG2 = X64Reg::RDX;
    constexpr X64Reg MEM_ARG3 = X64Reg::R8;
#else
    constexpr X64Reg MEM_ARG1 = X64Reg::RDI;
    constexpr X64Reg MEM_ARG2 = X64Reg::RSI;
    constexpr X64Reg MEM_ARG3 = X64Reg::RDX;
#endif

    // Scratch registers (caller-saved, freely clobbered inside a block).
    constexpr X64Reg SCR_ADDR = X64Reg::R8;   // guest effective address
    constexpr X64Reg SCR_TMP  = X64Reg::R9;   // transient scratch

    // Thunks bridging JIT-generated code to VirtualMemory. Fetches return u64
    // zero-extended so 32-bit loads store cleanly into a 64-bit guest register.
    u64 JitMemRead64(memory::VirtualMemory* mem, vaddr_t addr) { return mem->Read64(addr); }
    u64 JitMemRead32(memory::VirtualMemory* mem, vaddr_t addr) { return mem->Read32(addr); }
    void JitMemWrite64(memory::VirtualMemory* mem, vaddr_t addr, u64 value) { mem->Write64(addr, value); }
    void JitMemWrite32(memory::VirtualMemory* mem, vaddr_t addr, u64 value) { mem->Write32(addr, static_cast<u32>(value)); }

    // ---------------------------------------------------------------------
    // Slow-path thunks for floating-point, vector/NEON, and atomic/LSE
    // operations that are hard to emit as native x86-64 while preserving
    // bit-exact interpreter parity. Each thunk mirrors the corresponding
    // interpreter case so correctness is preserved by construction on both
    // Linux (System V) and Windows/Xbox (Microsoft x64) ABIs.
    // ---------------------------------------------------------------------

    // FMAX/FMIN/FABS/FNEG/FSQRT scalar.
    void JitFpMaxMin(CpuState* s, u8 rd, u8 rn, u8 rm, u8 is_double, u8 is_max) noexcept {
        if (is_double) {
            s->SetDouble(rd,
                is_max ? std::fmax(s->GetDouble(rn), s->GetDouble(rm))
                       : std::fmin(s->GetDouble(rn), s->GetDouble(rm)));
        } else {
            s->SetSingle(rd,
                is_max ? std::fmax(s->GetSingle(rn), s->GetSingle(rm))
                       : std::fmin(s->GetSingle(rn), s->GetSingle(rm)));
        }
    }

    void JitFpAbsNeg(CpuState* s, u8 rd, u8 rn, u8 is_double, u8 is_neg) noexcept {
        if (is_double) {
            const double v = s->GetDouble(rn);
            s->SetDouble(rd, is_neg ? -v : std::fabs(v));
        } else {
            const float v = s->GetSingle(rn);
            s->SetSingle(rd, is_neg ? -v : std::fabs(v));
        }
    }

    // FCMP_scalar: FCMP(0) sets rm=31 -> ZR (compare against +0.0).
    void JitFpCmp(CpuState* s, u8 rn, u8 rm, u8 is_double) noexcept {
        const double a = is_double ? s->GetDouble(rn)
                                   : static_cast<double>(s->GetSingle(rn));
        const double b = (rm == 31) ? 0.0
            : (is_double ? s->GetDouble(rm)
                         : static_cast<double>(s->GetSingle(rm)));
        s->SetNZCV_FPCmp(a, b);
    }

    // FCSEL_scalar.
    void JitFpCsel(CpuState* s, u8 rd, u8 rn, u8 rm, u8 cond_code, u8 is_double) noexcept {
        const Condition cond = static_cast<Condition>(cond_code);
        if (s->CheckCondition(cond)) {
            if (is_double) s->SetDouble(rd, s->GetDouble(rn));
            else s->SetSingle(rd, s->GetSingle(rn));
        } else {
            if (is_double) s->SetDouble(rd, s->GetDouble(rm));
            else s->SetSingle(rd, s->GetSingle(rm));
        }
    }

    // FMOV_imm.
    void JitFpMovImm(CpuState* s, u8 rd, u8 is_double, double imm) noexcept {
        if (is_double) s->SetDouble(rd, imm);
        else s->SetSingle(rd, static_cast<float>(imm));
    }

    // UCVTF / FCVTZU unsigned conversions (to float/double, or from float/double).
    void JitFpCvtUnsigned(CpuState* s, u8 rd, u8 rn, u8 is_double, u8 is_64bit, u8 to_fp) noexcept {
        if (to_fp) {
            // UCVTF: unsigned integer -> float/double
            if (is_double) {
                const double d = is_64bit ? static_cast<double>(s->GetX(rn))
                                          : static_cast<double>(s->GetW(rn));
                s->SetDouble(rd, d);
            } else {
                const float f = is_64bit ? static_cast<float>(s->GetX(rn))
                                         : static_cast<float>(s->GetW(rn));
                s->SetSingle(rd, f);
            }
        } else {
            // FCVTZU: float/double -> unsigned integer
            if (is_double) {
                const double d = s->GetDouble(rn);
                if (is_64bit) s->SetX(rd, static_cast<u64>(d));
                else s->SetW(rd, static_cast<u32>(d));
            } else {
                const float f = s->GetSingle(rn);
                if (is_64bit) s->SetX(rd, static_cast<u64>(f));
                else s->SetW(rd, static_cast<u32>(f));
            }
        }
    }

    // LDP_fp / STP_fp (scalar pair). is_load selects direction.
    void JitFpLoadStorePair(CpuState* s, memory::VirtualMemory* m,
                            u8 rt, u8 rt2, u8 rn, u8 is_double, u8 is_load, u64 offset) noexcept {
        const vaddr_t base = s->GetRegOrSP(rn) + offset;
        if (is_load) {
            if (is_double) {
                s->v[rt].low = m->Read64(base);   s->v[rt].high = 0;
                s->v[rt2].low = m->Read64(base + 8); s->v[rt2].high = 0;
            } else {
                s->v[rt].low = m->Read32(base);   s->v[rt].high = 0;
                s->v[rt2].low = m->Read32(base + 4); s->v[rt2].high = 0;
            }
        } else {
            if (is_double) {
                m->Write64(base, s->v[rt].low);
                m->Write64(base + 8, s->v[rt2].low);
            } else {
                m->Write32(base, static_cast<u32>(s->v[rt].low));
                m->Write32(base + 4, static_cast<u32>(s->v[rt2].low));
            }
        }
    }

    // Integer vector add/sub (ADD_vec / SUB_vec), lane size 4H (0) or 2S/2D.
    void JitVecIntAddSub(CpuState* s, u8 rd, u8 rn, u8 rm, u8 vec_size, u8 is_sub) noexcept {
        if (vec_size == 2) { // 4S: 32-bit lanes
            for (size_t l = 0; l < 4; ++l) {
                const u32 a = s->GetVectorLane32(rn, l);
                const u32 b = s->GetVectorLane32(rm, l);
                s->SetVectorLane32(rd, l, is_sub ? (a - b) : (a + b));
            }
        } else { // 2D (vec_size==3) : 64-bit lanes
            for (size_t l = 0; l < 2; ++l) {
                const u64 a = s->GetVectorLane64(rn, l);
                const u64 b = s->GetVectorLane64(rm, l);
                s->SetVectorLane64(rd, l, is_sub ? (a - b) : (a + b));
            }
        }
    }

    // FP vector add/sub/mul (FADD_vec / FSUB_vec / FMUL_vec).
    void JitVecFpArith(CpuState* s, u8 rd, u8 rn, u8 rm, u8 is_sub, u8 is_mul, u8 is_double) noexcept {
        if (is_double) {
            for (size_t l = 0; l < 2; ++l) {
                double a = 0.0, b = 0.0, r = 0.0;
                u64 ua = s->GetVectorLane64(rn, l);
                u64 ub = s->GetVectorLane64(rm, l);
                std::memcpy(&a, &ua, 8);
                std::memcpy(&b, &ub, 8);
                if (is_mul) r = a * b;
                else if (is_sub) r = a - b;
                else r = a + b;
                u64 ures = 0;
                std::memcpy(&ures, &r, 8);
                s->SetVectorLane64(rd, l, ures);
            }
        } else {
            for (size_t l = 0; l < 4; ++l) {
                float a = 0.0f, b = 0.0f, r = 0.0f;
                u32 ua = s->GetVectorLane32(rn, l);
                u32 ub = s->GetVectorLane32(rm, l);
                std::memcpy(&a, &ua, 4);
                std::memcpy(&b, &ub, 4);
                if (is_mul) r = a * b;
                else if (is_sub) r = a - b;
                else r = a + b;
                u32 ures = 0;
                std::memcpy(&ures, &r, 4);
                s->SetVectorLane32(rd, l, ures);
            }
        }
    }

    // Vector 128-bit AND/ORR/EOR/NOT.
    void JitVecLogical(CpuState* s, u8 rd, u8 rn, u8 rm, u8 op) noexcept {
        const u128 n = s->GetVector(rn);
        const u128 m = s->GetVector(rm);
        u128 r{};
        switch (op) {
            case 0: r.low = n.low & m.low;  r.high = n.high & m.high; break;
            case 1: r.low = n.low | m.low;  r.high = n.high | m.high; break;
            case 2: r.low = n.low ^ m.low;  r.high = n.high ^ m.high; break;
            case 3: r.low = ~n.low;         r.high = ~n.high; break;
            default: break;
        }
        s->SetVector(rd, r);
    }

    // DUP_gen / INS_gen / UMOV / SMOV element moves.
    void JitVecDupGen(CpuState* s, u8 rd, u8 rn, u8 vec_size) noexcept {
        if (vec_size == 3) {
            const u64 val = s->GetX(rn);
            s->SetVectorLane64(rd, 0, val);
            s->SetVectorLane64(rd, 1, val);
        } else if (vec_size == 2) {
            const u32 val = s->GetW(rn);
            for (size_t l = 0; l < 4; ++l) s->SetVectorLane32(rd, l, val);
        }
    }

    void JitVecInsGen(CpuState* s, u8 rd, u8 rn, u8 vec_size, u8 index) noexcept {
        if (vec_size == 3) {
            s->SetVectorLane64(rd, index, s->GetX(rn));
        } else if (vec_size == 2) {
            s->SetVectorLane32(rd, index, s->GetW(rn));
        }
    }

    void JitVecUMov(CpuState* s, u8 rd, u8 rn, u8 vec_size, u8 index) noexcept {
        if (vec_size == 3) {
            s->SetX(rd, s->GetVectorLane64(rn, index));
        } else if (vec_size == 2) {
            s->SetW(rd, s->GetVectorLane32(rn, index));
        }
    }

    void JitVecSMov(CpuState* s, u8 rd, u8 rn, u8 vec_size, u8 index) noexcept {
        // SMOV sign-extends the selected element up to 64 bits.
        if (vec_size == 3) { // 64-bit lane
            const s64 v = static_cast<s64>(s->GetVectorLane64(rn, index));
            s->SetX(rd, static_cast<u64>(v));
        } else {
            u64 v = 0;
            unsigned bits = 0;
            if (vec_size == 0) { v = s->GetVectorLane8(rn, index);  bits = 8; }
            else if (vec_size == 1) { v = s->GetVectorLane16(rn, index); bits = 16; }
            else { v = static_cast<u64>(s->GetVectorLane32(rn, index)); bits = 32; }
            const u64 sign = (v >> (bits - 1)) & 1;
            const u64 ext = sign ? (~u64{0} << bits) : 0;
            s->SetX(rd, v | ext);
        }
    }

    // Atomic/LSE: LDADD (op 0), CAS (op 1), SWP (op 2).
    void JitAtomic(CpuState* s, memory::VirtualMemory* m, u8 rd, u8 rn, u8 rs,
                   u8 is_64bit, u8 op) noexcept {
        const vaddr_t addr = s->GetRegOrSP(rn);
        if (is_64bit) {
            const u64 cur = m->Read64(addr);
            const u64 arg = s->GetX(rs);
            switch (op) {
                case 0: m->Write64(addr, cur + arg); break; // LDADD
                case 1: if (cur == arg) m->Write64(addr, s->GetX(rd)); break; // CAS
                case 2: m->Write64(addr, arg); break; // SWP
                default: break;
            }
            s->SetX(rd, cur);
            if (op == 1) s->SetX(rs, cur); // CAS writes the observed value to Rs
        } else {
            const u32 cur = m->Read32(addr);
            const u32 arg = s->GetW(rs);
            switch (op) {
                case 0: m->Write32(addr, cur + arg); break;
                case 1: if (cur == arg) m->Write32(addr, s->GetW(rd)); break;
                case 2: m->Write32(addr, arg); break;
                default: break;
            }
            s->SetW(rd, cur);
            if (op == 1) s->SetW(rs, cur);
        }
    }

    // SlowOp op codes (mirror the relevant Opcode values so the dispatcher is
    // perfectly in-sync with the interpreter semantics).
    enum : u8 {
        SLOW_FMAX      = 1,
        SLOW_FMIN      = 2,
        SLOW_FABS      = 3,
        SLOW_FNEG      = 4,
        SLOW_FCMP      = 5,
        SLOW_FCSEL     = 6,
        SLOW_FMOV_IMM  = 7,
        SLOW_UCVTF     = 8,
        SLOW_FCVTZU    = 9,
        SLOW_LDP_FP    = 10,
        SLOW_STP_FP    = 11,
        SLOW_ADD_VEC   = 12,
        SLOW_SUB_VEC   = 13,
        SLOW_FADD_VEC  = 14,
        SLOW_FSUB_VEC  = 15,
        SLOW_FMUL_VEC  = 16,
        SLOW_AND_VEC   = 17,
        SLOW_ORR_VEC   = 18,
        SLOW_EOR_VEC   = 19,
        SLOW_NOT_VEC   = 20,
        SLOW_DUP_GEN   = 21,
        SLOW_INS_GEN   = 22,
        SLOW_UMOV      = 23,
        SLOW_SMOV      = 24,
        SLOW_LDADD     = 25,
        SLOW_CAS       = 26,
        SLOW_SWP       = 27,
        SLOW_LDXR      = 28,
        SLOW_STXR      = 29
    };

    // Uniform 6-argument dispatcher called from JIT blocks. All arguments are
    // integer-class so the calling convention is identical on Linux (System V
    // x64: RDI/RSI/RDX/RCX/R8/R9) and Windows/Xbox (Microsoft x64:
    // RCX/RDX/R8/R9 + stack). This guarantees the same ABI on both targets.
    //
    // Packed descriptor:
    //   p0: [7:0]=op | [15:8]=rd | [23:16]=rn | [31:24]=rm | [39:32]=rs | [47:40]=rt2
    //   p1: [7:0]=vec_size | [15:8]=vec_index | [23:16]=is_64bit
    //       [31:24]=is_fp_double | [39:32]=cond | [47:40]=is_load | [55:48]=subop
    //   p2: imm / offset (u64)
    //   p3: fp_imm bit-cast to u64 (for FMOV_imm)
    void JitSlowOp(CpuState* s, memory::VirtualMemory* m, u64 p0, u64 p1, u64 p2, u64 p3) noexcept {
        const u8 op       = static_cast<u8>(p0 & 0xFF);
        const u8 rd       = static_cast<u8>((p0 >> 8)  & 0xFF);
        const u8 rn       = static_cast<u8>((p0 >> 16) & 0xFF);
        const u8 rm       = static_cast<u8>((p0 >> 24) & 0xFF);
        const u8 rs       = static_cast<u8>((p0 >> 32) & 0xFF);
        const u8 rt2      = static_cast<u8>((p0 >> 40) & 0xFF);
        const u8 vec_size = static_cast<u8>(p1 & 0xFF);
        const u8 vec_idx  = static_cast<u8>((p1 >> 8)  & 0xFF);
        const u8 is64     = static_cast<u8>((p1 >> 16) & 0xFF);
        const u8 is_dbl   = static_cast<u8>((p1 >> 24) & 0xFF);
        const u8 cond     = static_cast<u8>((p1 >> 32) & 0xFF);
        const u8 is_load  = static_cast<u8>((p1 >> 40) & 0xFF);
        double fp_imm;
        std::memcpy(&fp_imm, &p3, sizeof(fp_imm));

        switch (op) {
            case SLOW_FMAX: case SLOW_FMIN:
                JitFpMaxMin(s, rd, rn, rm, is_dbl, op == SLOW_FMAX);
                break;
            case SLOW_FABS: case SLOW_FNEG:
                JitFpAbsNeg(s, rd, rn, is_dbl, op == SLOW_FNEG);
                break;
            case SLOW_FCMP:
                JitFpCmp(s, rn, rm, is_dbl);
                break;
            case SLOW_FCSEL:
                JitFpCsel(s, rd, rn, rm, cond, is_dbl);
                break;
            case SLOW_FMOV_IMM:
                JitFpMovImm(s, rd, is_dbl, fp_imm);
                break;
            case SLOW_UCVTF: case SLOW_FCVTZU:
                JitFpCvtUnsigned(s, rd, rn, is_dbl, is64, op == SLOW_UCVTF);
                break;
            case SLOW_LDP_FP: case SLOW_STP_FP:
                JitFpLoadStorePair(s, m, rd, rt2, rn, is_dbl, is_load, p2);
                break;
            case SLOW_ADD_VEC: case SLOW_SUB_VEC:
                JitVecIntAddSub(s, rd, rn, rm, vec_size, op == SLOW_SUB_VEC);
                break;
            case SLOW_FADD_VEC: case SLOW_FSUB_VEC: case SLOW_FMUL_VEC:
                JitVecFpArith(s, rd, rn, rm, op == SLOW_FSUB_VEC, op == SLOW_FMUL_VEC, is_dbl);
                break;
            case SLOW_AND_VEC: case SLOW_ORR_VEC: case SLOW_EOR_VEC: case SLOW_NOT_VEC:
                JitVecLogical(s, rd, rn, rm,
                              op == SLOW_AND_VEC ? 0 : (op == SLOW_ORR_VEC ? 1
                                    : (op == SLOW_EOR_VEC ? 2 : 3)));
                break;
            case SLOW_DUP_GEN:
                JitVecDupGen(s, rd, rn, vec_size);
                break;
            case SLOW_INS_GEN:
                JitVecInsGen(s, rd, rn, vec_size, vec_idx);
                break;
            case SLOW_UMOV:
                JitVecUMov(s, rd, rn, vec_size, vec_idx);
                break;
            case SLOW_SMOV:
                JitVecSMov(s, rd, rn, vec_size, vec_idx);
                break;
            case SLOW_LDADD: case SLOW_CAS: case SLOW_SWP:
                JitAtomic(s, m, rd, rn, rs, is64, op == SLOW_LDADD ? 0
                                    : (op == SLOW_CAS ? 1 : 2));
                break;
            case SLOW_LDXR: {
                const vaddr_t addr = s->GetRegOrSP(rn);
                s->exclusive_addr = addr;
                s->exclusive_active = true;
                if (is64) s->SetX(rd, m->Read64(addr));
                else s->SetW(rd, m->Read32(addr));
                break;
            }
            case SLOW_STXR: {
                const vaddr_t addr = s->GetRegOrSP(rn);
                if (s->exclusive_active && s->exclusive_addr == addr) {
                    if (is64) m->Write64(addr, s->GetX(rd));
                    else m->Write32(addr, s->GetW(rd));
                    s->SetW(rs, 0); // 0 = Success
                    s->exclusive_active = false;
                } else {
                    s->SetW(rs, 1); // 1 = Failure
                }
                break;
            }
            default:
                break;
        }
    }
}

JitCompiler::JitCompiler(size_t cache_size)
    : code_cache_(cache_size) {
}

JitBlockFn JitCompiler::CompileBlock(vaddr_t guest_pc, memory::VirtualMemory& memory) {
    auto it = block_map_.find(guest_pc);
    if (it != block_map_.end()) {
        return it->second;
    }

    // Capture the backing guest memory for this block. Compiled blocks embed
    // this address directly; the cache is valid only while the same memory
    // object backs execution.
    mem_addr_ = reinterpret_cast<u64>(std::addressof(memory));

    emitter_.Clear();

    // 1. Prologue: save callee-saved registers
    emitter_.Push(X64Reg::RBX);
    emitter_.Push(X64Reg::RBP);
    emitter_.Push(X64Reg::R12);
    emitter_.Push(X64Reg::R13);
    emitter_.Push(X64Reg::R14);
    emitter_.Push(X64Reg::R15);

    // Load CpuState* into R15
#ifdef _WIN32
    // Windows x64 ABI: 1st argument in RCX
    emitter_.MovR64R64(X64Reg::R15, X64Reg::RCX);
#else
    // System V x64 ABI: 1st argument in RDI
    emitter_.MovR64R64(X64Reg::R15, X64Reg::RDI);
#endif

    // 2. Decode & Translate guest instructions
    vaddr_t curr_pc = guest_pc;
    bool block_ended = false;
    size_t insn_count = 0;

    while (!block_ended && insn_count < MAX_BLOCK_INSTRUCTIONS) {
        if (!memory.IsValidAddress(curr_pc, 4)) {
            break;
        }

        const u32 raw_insn = memory.Read32(curr_pc);
        const DecodedInstruction inst = Decoder::Decode(raw_insn);
        insn_count++;

        switch (inst.opcode) {
            case Opcode::MOVZ: {
                const u64 val = inst.imm << inst.shift_amount;
                if (inst.rd != 31) {
                    emitter_.MovR64Imm(X64Reg::RAX, val);
                    emitter_.MovMemR64(X64Reg::R15, static_cast<s32>(inst.rd * 8), X64Reg::RAX);
                }
                curr_pc += 4;
                break;
            }

            case Opcode::MOVK: {
                // X[rd] = (X[rd] & ~(0xFFFF << shift)) | (imm << shift)
                if (inst.rd != 31) {
                    if (inst.is_64bit) {
                        const u64 mask = ~(0xFFFFULL << inst.shift_amount);
                        emitter_.MovR64Imm(X64Reg::RAX, mask);
                        emitter_.MovR64Mem(X64Reg::RDX, X64Reg::R15, XSlot(inst.rd));
                        emitter_.AndR64R64(X64Reg::RDX, X64Reg::RAX);
                        emitter_.MovR64Imm(X64Reg::RAX, (inst.imm << inst.shift_amount));
                        emitter_.OrR64R64(X64Reg::RDX, X64Reg::RAX);
                        emitter_.MovMemR64(X64Reg::R15, XSlot(inst.rd), X64Reg::RDX);
                    } else {
                        const u32 mask32 = static_cast<u32>(~(0xFFFFULL << inst.shift_amount));
                        emitter_.MovR32Imm(X64Reg::RAX, mask32);
                        emitter_.MovR64Mem(X64Reg::RDX, X64Reg::R15, XSlot(inst.rd));
                        emitter_.AndR64R64(X64Reg::RDX, X64Reg::RAX);
                        emitter_.MovR32Imm(X64Reg::RAX, static_cast<u32>(inst.imm << inst.shift_amount));
                        emitter_.OrR64R64(X64Reg::RDX, X64Reg::RAX);
                        emitter_.MovMemR32(X64Reg::R15, XSlot(inst.rd), X64Reg::RDX);
                    }
                }
                curr_pc += 4;
                break;
            }

            case Opcode::ADD_imm: {
                emitter_.MovR64Mem(X64Reg::RAX, X64Reg::R15, static_cast<s32>(inst.rn * 8));
                emitter_.AddR64Imm32(X64Reg::RAX, static_cast<s32>(inst.imm));
                if (inst.rd != 31) {
                    emitter_.MovMemR64(X64Reg::R15, static_cast<s32>(inst.rd * 8), X64Reg::RAX);
                }
                curr_pc += 4;
                break;
            }

            case Opcode::ADD_reg: {
                emitter_.MovR64Mem(X64Reg::RAX, X64Reg::R15, static_cast<s32>(inst.rn * 8));
                emitter_.MovR64Mem(X64Reg::RDX, X64Reg::R15, static_cast<s32>(inst.rm * 8));
                emitter_.AddR64R64(X64Reg::RAX, X64Reg::RDX);
                if (inst.rd != 31) {
                    emitter_.MovMemR64(X64Reg::R15, static_cast<s32>(inst.rd * 8), X64Reg::RAX);
                }
                curr_pc += 4;
                break;
            }

            case Opcode::SUB_reg: {
                emitter_.MovR64Mem(X64Reg::RAX, X64Reg::R15, static_cast<s32>(inst.rn * 8));
                emitter_.MovR64Mem(X64Reg::RDX, X64Reg::R15, static_cast<s32>(inst.rm * 8));
                emitter_.SubR64R64(X64Reg::RAX, X64Reg::RDX);
                if (inst.rd != 31) {
                    emitter_.MovMemR64(X64Reg::R15, static_cast<s32>(inst.rd * 8), X64Reg::RAX);
                }
                curr_pc += 4;
                break;
            }

            case Opcode::AND_reg: {
                emitter_.MovR64Mem(X64Reg::RAX, X64Reg::R15, static_cast<s32>(inst.rn * 8));
                emitter_.MovR64Mem(X64Reg::RDX, X64Reg::R15, static_cast<s32>(inst.rm * 8));
                emitter_.AndR64R64(X64Reg::RAX, X64Reg::RDX);
                if (inst.rd != 31) {
                    emitter_.MovMemR64(X64Reg::R15, static_cast<s32>(inst.rd * 8), X64Reg::RAX);
                }
                curr_pc += 4;
                break;
            }

            case Opcode::ORR_reg: {
                emitter_.MovR64Mem(X64Reg::RAX, X64Reg::R15, static_cast<s32>(inst.rn * 8));
                emitter_.MovR64Mem(X64Reg::RDX, X64Reg::R15, static_cast<s32>(inst.rm * 8));
                emitter_.OrR64R64(X64Reg::RAX, X64Reg::RDX);
                if (inst.rd != 31) {
                    emitter_.MovMemR64(X64Reg::R15, static_cast<s32>(inst.rd * 8), X64Reg::RAX);
                }
                curr_pc += 4;
                break;
            }

            case Opcode::EOR_reg: {
                emitter_.MovR64Mem(X64Reg::RAX, X64Reg::R15, static_cast<s32>(inst.rn * 8));
                emitter_.MovR64Mem(X64Reg::RDX, X64Reg::R15, static_cast<s32>(inst.rm * 8));
                emitter_.XorR64R64(X64Reg::RAX, X64Reg::RDX);
                if (inst.rd != 31) {
                    emitter_.MovMemR64(X64Reg::R15, static_cast<s32>(inst.rd * 8), X64Reg::RAX);
                }
                curr_pc += 4;
                break;
            }

            case Opcode::NOP: {
                curr_pc += 4;
                break;
            }

            case Opcode::B: {
                const vaddr_t target = curr_pc + inst.imm;
                emitter_.MovR64Imm(X64Reg::RAX, target);
                emitter_.MovMemR64(X64Reg::R15, OFFSET_PC, X64Reg::RAX);
                block_ended = true;
                break;
            }

            case Opcode::RET: {
                emitter_.MovR64Mem(X64Reg::RAX, X64Reg::R15, static_cast<s32>(inst.rn * 8));
                emitter_.MovMemR64(X64Reg::R15, OFFSET_PC, X64Reg::RAX);
                block_ended = true;
                break;
            }

            case Opcode::BL: {
                // X30 = PC + 4; PC = PC + imm (target). End the block.
                emitter_.MovR64Imm(X64Reg::RAX, curr_pc + 4);
                emitter_.MovMemR64(X64Reg::R15, XSlot(30), X64Reg::RAX);
                emitter_.MovR64Imm(X64Reg::RAX, curr_pc + inst.imm);
                emitter_.MovMemR64(X64Reg::R15, OFFSET_PC, X64Reg::RAX);
                block_ended = true;
                break;
            }

            case Opcode::BLR: {
                // target = X[rn] (read BEFORE overwriting X30), then X30 = PC + 4,
                // then PC = target. Mnist: when rn == 30 the target is the OLD X30.
                emitter_.MovR64Mem(SCR_TMP, X64Reg::R15, XSlot(inst.rn));
                emitter_.MovR64Imm(X64Reg::RAX, curr_pc + 4);
                emitter_.MovMemR64(X64Reg::R15, XSlot(30), X64Reg::RAX);
                emitter_.MovR64R64(X64Reg::RAX, SCR_TMP);
                emitter_.MovMemR64(X64Reg::R15, OFFSET_PC, X64Reg::RAX);
                block_ended = true;
                break;
            }

            case Opcode::B_cond: {
                // ZF = 1 iff the ARM condition holds, then:
                //   PC = taken ? (PC + imm) : (PC + 4). End the block either way.
                EmitConditionToZF(inst.condition);
                emitter_.MovR64Imm(X64Reg::RAX, curr_pc + 4);   // fall-through
                emitter_.MovR64Imm(X64Reg::RDX, curr_pc + inst.imm);
                emitter_.CmovccR64R64(X64Reg::RAX, X64Reg::RDX, Cc::E); // ZF==1 -> taken
                emitter_.MovMemR64(X64Reg::R15, OFFSET_PC, X64Reg::RAX);
                block_ended = true;
                break;
            }

            case Opcode::SUBS_imm: {
                // Load base (SP-aware) into RAX, subtract the immediate, set NZCV,
                // and store to Rd unless it is XZR (31) -- i.e. CMP Xn, #imm.
                emitter_.MovR64Mem(X64Reg::RAX, X64Reg::R15, RegOrSpSlot(inst.rn));
                emitter_.MovR64Imm(X64Reg::RDX, inst.imm);
                emitter_.SubR64R64(X64Reg::RAX, X64Reg::RDX);
                EmitSetNZCVFromSub();
                EmitSetX(inst.rd, X64Reg::RAX);
                curr_pc += 4;
                break;
            }

            case Opcode::SUBS_reg: {
                // Load base (SP-aware) into RAX, apply any shift to the RM value in
                // RDX, subtract, set NZCV, and store to Rd unless it is XZR.
                emitter_.MovR64Mem(X64Reg::RAX, X64Reg::R15, RegOrSpSlot(inst.rn));
                emitter_.MovR64Mem(X64Reg::RDX, X64Reg::R15, XSlot(inst.rm));
                EmitShiftRegToRdx(inst.is_64bit, inst.shift_type, inst.shift_amount);
                emitter_.SubR64R64(X64Reg::RAX, X64Reg::RDX);
                EmitSetNZCVFromSub();
                EmitSetX(inst.rd, X64Reg::RAX);
                curr_pc += 4;
                break;
            }

            case Opcode::LDR_imm: {
                EmitMemAccess(true, inst.is_64bit, inst.rn, inst.imm, inst.rd);
                curr_pc += 4;
                break;
            }

            case Opcode::STR_imm: {
                EmitMemAccess(false, inst.is_64bit, inst.rn, inst.imm, inst.rd);
                curr_pc += 4;
                break;
            }

            case Opcode::CSEL: {
                // Rd = CheckCondition(cond) ? Rn : Rm. Zero-extend for 32-bit op.
                // Evaluate the condition first (clobbers RAX/RDX/R9), then load
                // Rn/Rm -- MOVs preserve the ZF set above for the CMOV.
                EmitConditionToZF(inst.condition);
                if (inst.is_64bit) {
                    emitter_.MovR64Mem(X64Reg::RAX, X64Reg::R15, XSlot(inst.rm)); // default: Rm
                    emitter_.MovR64Mem(X64Reg::RDX, X64Reg::R15, XSlot(inst.rn));
                } else {
                    emitter_.MovR32Mem(X64Reg::RAX, X64Reg::R15, XSlot(inst.rm));
                    emitter_.MovR32Mem(X64Reg::RDX, X64Reg::R15, XSlot(inst.rn));
                }
                emitter_.CmovccR64R64(X64Reg::RAX, X64Reg::RDX, Cc::E); // ZF==1 -> Rn
                if (inst.rd != 31) {
                    if (inst.is_64bit) {
                        emitter_.MovMemR64(X64Reg::R15, XSlot(inst.rd), X64Reg::RAX);
                    } else {
                        emitter_.MovMemR32(X64Reg::R15, XSlot(inst.rd), X64Reg::RAX);
                    }
                }
                curr_pc += 4;
                break;
            }

            case Opcode::SVC: {
                // Advance PC past the SVC so execution resumes on the next block.
                emitter_.MovR64Imm(X64Reg::RAX, curr_pc + 4);
                emitter_.MovMemR64(X64Reg::R15, OFFSET_PC, X64Reg::RAX);

                // Arg 1: CpuState* (already held in R15).
                emitter_.MovR64R64(REG_STATE_ARG, X64Reg::R15);
                // Arg 2: svc_id (the SVC immediate).
                emitter_.MovR64Imm(REG_SVC_ARG, static_cast<u64>(inst.imm));

                // Load the native thunk target and call it. InvokeSvcHandler halts
                // the guest if no handler is registered, so a stale block without a
                // handler is safe to execute.
                emitter_.MovR64Imm(REG_THUNK, reinterpret_cast<u64>(&JitCompiler::InvokeSvcHandler));

#ifdef _WIN32
                // Windows x64 ABI requires 32-byte shadow space + 16-byte alignment.
                // RSP inside block is (16k + 8); subtracting 40 aligns to 16 bytes.
                emitter_.SubR64Imm32(X64Reg::RSP, 40);
                emitter_.CallR64(REG_THUNK);
                emitter_.AddR64Imm32(X64Reg::RSP, 40);
#else
                // System V x64 ABI requires 16-byte stack alignment before call.
                // RSP inside block is (16k + 8); subtracting 8 aligns to 16 bytes.
                emitter_.SubR64Imm32(X64Reg::RSP, 8);
                emitter_.CallR64(REG_THUNK);
                emitter_.AddR64Imm32(X64Reg::RSP, 8);
#endif
                block_ended = true;
                break;
            }

            case Opcode::FADD_scalar: {
                if (inst.is_fp_double) {
                    emitter_.MovsdXmmMem(XmmReg::XMM0, X64Reg::R15, VSlot(inst.rn));
                    emitter_.MovsdXmmMem(XmmReg::XMM1, X64Reg::R15, VSlot(inst.rm));
                    emitter_.Addsd(XmmReg::XMM0, XmmReg::XMM1);
                    emitter_.MovsdMemXmm(X64Reg::R15, VSlot(inst.rd), XmmReg::XMM0);
                    emitter_.MovR64Imm(X64Reg::RAX, 0);
                    emitter_.MovMemR64(X64Reg::R15, VSlot(inst.rd) + 8, X64Reg::RAX);
                } else {
                    emitter_.MovssXmmMem(XmmReg::XMM0, X64Reg::R15, VSlot(inst.rn));
                    emitter_.MovssXmmMem(XmmReg::XMM1, X64Reg::R15, VSlot(inst.rm));
                    emitter_.Addss(XmmReg::XMM0, XmmReg::XMM1);
                    emitter_.MovssMemXmm(X64Reg::R15, VSlot(inst.rd), XmmReg::XMM0);
                    emitter_.MovR64Imm(X64Reg::RAX, 0);
                    emitter_.MovMemR64(X64Reg::R15, VSlot(inst.rd) + 8, X64Reg::RAX);
                }
                curr_pc += 4;
                break;
            }

            case Opcode::FSUB_scalar: {
                if (inst.is_fp_double) {
                    emitter_.MovsdXmmMem(XmmReg::XMM0, X64Reg::R15, VSlot(inst.rn));
                    emitter_.MovsdXmmMem(XmmReg::XMM1, X64Reg::R15, VSlot(inst.rm));
                    emitter_.Subsd(XmmReg::XMM0, XmmReg::XMM1);
                    emitter_.MovsdMemXmm(X64Reg::R15, VSlot(inst.rd), XmmReg::XMM0);
                    emitter_.MovR64Imm(X64Reg::RAX, 0);
                    emitter_.MovMemR64(X64Reg::R15, VSlot(inst.rd) + 8, X64Reg::RAX);
                } else {
                    emitter_.MovssXmmMem(XmmReg::XMM0, X64Reg::R15, VSlot(inst.rn));
                    emitter_.MovssXmmMem(XmmReg::XMM1, X64Reg::R15, VSlot(inst.rm));
                    emitter_.Subss(XmmReg::XMM0, XmmReg::XMM1);
                    emitter_.MovssMemXmm(X64Reg::R15, VSlot(inst.rd), XmmReg::XMM0);
                    emitter_.MovR64Imm(X64Reg::RAX, 0);
                    emitter_.MovMemR64(X64Reg::R15, VSlot(inst.rd) + 8, X64Reg::RAX);
                }
                curr_pc += 4;
                break;
            }

            case Opcode::FMUL_scalar: {
                if (inst.is_fp_double) {
                    emitter_.MovsdXmmMem(XmmReg::XMM0, X64Reg::R15, VSlot(inst.rn));
                    emitter_.MovsdXmmMem(XmmReg::XMM1, X64Reg::R15, VSlot(inst.rm));
                    emitter_.Mulsd(XmmReg::XMM0, XmmReg::XMM1);
                    emitter_.MovsdMemXmm(X64Reg::R15, VSlot(inst.rd), XmmReg::XMM0);
                    emitter_.MovR64Imm(X64Reg::RAX, 0);
                    emitter_.MovMemR64(X64Reg::R15, VSlot(inst.rd) + 8, X64Reg::RAX);
                } else {
                    emitter_.MovssXmmMem(XmmReg::XMM0, X64Reg::R15, VSlot(inst.rn));
                    emitter_.MovssXmmMem(XmmReg::XMM1, X64Reg::R15, VSlot(inst.rm));
                    emitter_.Mulss(XmmReg::XMM0, XmmReg::XMM1);
                    emitter_.MovssMemXmm(X64Reg::R15, VSlot(inst.rd), XmmReg::XMM0);
                    emitter_.MovR64Imm(X64Reg::RAX, 0);
                    emitter_.MovMemR64(X64Reg::R15, VSlot(inst.rd) + 8, X64Reg::RAX);
                }
                curr_pc += 4;
                break;
            }

            case Opcode::FDIV_scalar: {
                if (inst.is_fp_double) {
                    emitter_.MovsdXmmMem(XmmReg::XMM0, X64Reg::R15, VSlot(inst.rn));
                    emitter_.MovsdXmmMem(XmmReg::XMM1, X64Reg::R15, VSlot(inst.rm));
                    emitter_.Divsd(XmmReg::XMM0, XmmReg::XMM1);
                    emitter_.MovsdMemXmm(X64Reg::R15, VSlot(inst.rd), XmmReg::XMM0);
                    emitter_.MovR64Imm(X64Reg::RAX, 0);
                    emitter_.MovMemR64(X64Reg::R15, VSlot(inst.rd) + 8, X64Reg::RAX);
                } else {
                    emitter_.MovssXmmMem(XmmReg::XMM0, X64Reg::R15, VSlot(inst.rn));
                    emitter_.MovssXmmMem(XmmReg::XMM1, X64Reg::R15, VSlot(inst.rm));
                    emitter_.Divss(XmmReg::XMM0, XmmReg::XMM1);
                    emitter_.MovssMemXmm(X64Reg::R15, VSlot(inst.rd), XmmReg::XMM0);
                    emitter_.MovR64Imm(X64Reg::RAX, 0);
                    emitter_.MovMemR64(X64Reg::R15, VSlot(inst.rd) + 8, X64Reg::RAX);
                }
                curr_pc += 4;
                break;
            }

            case Opcode::FSQRT_scalar: {
                if (inst.is_fp_double) {
                    emitter_.MovsdXmmMem(XmmReg::XMM0, X64Reg::R15, VSlot(inst.rn));
                    emitter_.Sqrtsd(XmmReg::XMM0, XmmReg::XMM0);
                    emitter_.MovsdMemXmm(X64Reg::R15, VSlot(inst.rd), XmmReg::XMM0);
                    emitter_.MovR64Imm(X64Reg::RAX, 0);
                    emitter_.MovMemR64(X64Reg::R15, VSlot(inst.rd) + 8, X64Reg::RAX);
                } else {
                    emitter_.MovssXmmMem(XmmReg::XMM0, X64Reg::R15, VSlot(inst.rn));
                    emitter_.Sqrtss(XmmReg::XMM0, XmmReg::XMM0);
                    emitter_.MovssMemXmm(X64Reg::R15, VSlot(inst.rd), XmmReg::XMM0);
                    emitter_.MovR64Imm(X64Reg::RAX, 0);
                    emitter_.MovMemR64(X64Reg::R15, VSlot(inst.rd) + 8, X64Reg::RAX);
                }
                curr_pc += 4;
                break;
            }

            case Opcode::FMOV_reg: {
                emitter_.MovR64Mem(X64Reg::RAX, X64Reg::R15, VSlot(inst.rn));
                emitter_.MovMemR64(X64Reg::R15, VSlot(inst.rd), X64Reg::RAX);
                emitter_.MovR64Imm(X64Reg::RAX, 0);
                emitter_.MovMemR64(X64Reg::R15, VSlot(inst.rd) + 8, X64Reg::RAX);
                curr_pc += 4;
                break;
            }

            case Opcode::FMOV_to_gp: {
                if (inst.is_64bit) {
                    emitter_.MovR64Mem(X64Reg::RAX, X64Reg::R15, VSlot(inst.rn));
                    EmitSetX(inst.rd, X64Reg::RAX);
                } else {
                    emitter_.MovR32Mem(X64Reg::RAX, X64Reg::R15, VSlot(inst.rn));
                    EmitSetW(inst.rd, X64Reg::RAX);
                }
                curr_pc += 4;
                break;
            }

            case Opcode::FMOV_from_gp: {
                if (inst.is_64bit) {
                    emitter_.MovR64Mem(X64Reg::RAX, X64Reg::R15, RegOrSpSlot(inst.rn));
                    emitter_.MovMemR64(X64Reg::R15, VSlot(inst.rd), X64Reg::RAX);
                } else {
                    emitter_.MovR32Mem(X64Reg::RAX, X64Reg::R15, RegOrSpSlot(inst.rn));
                    emitter_.MovMemR32(X64Reg::R15, VSlot(inst.rd), X64Reg::RAX);
                }
                emitter_.MovR64Imm(X64Reg::RAX, 0);
                emitter_.MovMemR64(X64Reg::R15, VSlot(inst.rd) + 8, X64Reg::RAX);
                curr_pc += 4;
                break;
            }

            case Opcode::SCVTF: {
                if (inst.is_64bit) {
                    emitter_.MovR64Mem(X64Reg::RAX, X64Reg::R15, RegOrSpSlot(inst.rn));
                } else {
                    emitter_.MovR32Mem(X64Reg::RAX, X64Reg::R15, RegOrSpSlot(inst.rn));
                }
                if (inst.is_fp_double) {
                    emitter_.Cvtsi2sd(XmmReg::XMM0, X64Reg::RAX, inst.is_64bit);
                    emitter_.MovsdMemXmm(X64Reg::R15, VSlot(inst.rd), XmmReg::XMM0);
                } else {
                    emitter_.Cvtsi2ss(XmmReg::XMM0, X64Reg::RAX, inst.is_64bit);
                    emitter_.MovssMemXmm(X64Reg::R15, VSlot(inst.rd), XmmReg::XMM0);
                }
                emitter_.MovR64Imm(X64Reg::RAX, 0);
                emitter_.MovMemR64(X64Reg::R15, VSlot(inst.rd) + 8, X64Reg::RAX);
                curr_pc += 4;
                break;
            }

            case Opcode::FCVTZS: {
                if (inst.is_fp_double) {
                    emitter_.MovsdXmmMem(XmmReg::XMM0, X64Reg::R15, VSlot(inst.rn));
                    emitter_.Cvttsd2si(X64Reg::RAX, XmmReg::XMM0, inst.is_64bit);
                } else {
                    emitter_.MovssXmmMem(XmmReg::XMM0, X64Reg::R15, VSlot(inst.rn));
                    emitter_.Cvttss2si(X64Reg::RAX, XmmReg::XMM0, inst.is_64bit);
                }
                if (inst.is_64bit) EmitSetX(inst.rd, X64Reg::RAX);
                else EmitSetW(inst.rd, X64Reg::RAX);
                curr_pc += 4;
                break;
            }

            case Opcode::FCVT: {
                if (inst.is_fp_double) {
                    emitter_.MovssXmmMem(XmmReg::XMM0, X64Reg::R15, VSlot(inst.rn));
                    emitter_.Cvtss2sd(XmmReg::XMM0, XmmReg::XMM0);
                    emitter_.MovsdMemXmm(X64Reg::R15, VSlot(inst.rd), XmmReg::XMM0);
                } else {
                    emitter_.MovsdXmmMem(XmmReg::XMM0, X64Reg::R15, VSlot(inst.rn));
                    emitter_.Cvtsd2ss(XmmReg::XMM0, XmmReg::XMM0);
                    emitter_.MovssMemXmm(X64Reg::R15, VSlot(inst.rd), XmmReg::XMM0);
                }
                emitter_.MovR64Imm(X64Reg::RAX, 0);
                emitter_.MovMemR64(X64Reg::R15, VSlot(inst.rd) + 8, X64Reg::RAX);
                curr_pc += 4;
                break;
            }

            case Opcode::LDR_fp_imm: {
                emitter_.MovR64Mem(SCR_ADDR, X64Reg::R15, RegOrSpSlot(inst.rn));
                emitter_.AddR64Imm32(SCR_ADDR, static_cast<s32>(inst.imm));
                const u64 thunk = reinterpret_cast<u64>(inst.is_fp_double ? &JitMemRead64 : &JitMemRead32);
                emitter_.MovR64Imm(MEM_ARG1, mem_addr_);
                emitter_.MovR64R64(MEM_ARG2, SCR_ADDR);
                emitter_.MovR64Imm(REG_THUNK, thunk);
#ifdef _WIN32
                emitter_.SubR64Imm32(X64Reg::RSP, 40);
                emitter_.CallR64(REG_THUNK);
                emitter_.AddR64Imm32(X64Reg::RSP, 40);
#else
                emitter_.SubR64Imm32(X64Reg::RSP, 8);
                emitter_.CallR64(REG_THUNK);
                emitter_.AddR64Imm32(X64Reg::RSP, 8);
#endif
                if (inst.is_fp_double) {
                    emitter_.MovMemR64(X64Reg::R15, VSlot(inst.rd), X64Reg::RAX);
                } else {
                    emitter_.MovMemR32(X64Reg::R15, VSlot(inst.rd), X64Reg::RAX);
                }
                emitter_.MovR64Imm(X64Reg::RAX, 0);
                emitter_.MovMemR64(X64Reg::R15, VSlot(inst.rd) + 8, X64Reg::RAX);
                curr_pc += 4;
                break;
            }

            case Opcode::STR_fp_imm: {
                emitter_.MovR64Mem(SCR_ADDR, X64Reg::R15, RegOrSpSlot(inst.rn));
                emitter_.AddR64Imm32(SCR_ADDR, static_cast<s32>(inst.imm));
                const u64 thunk = reinterpret_cast<u64>(inst.is_fp_double ? &JitMemWrite64 : &JitMemWrite32);
                emitter_.MovR64Imm(MEM_ARG1, mem_addr_);
                emitter_.MovR64R64(MEM_ARG2, SCR_ADDR);
                emitter_.MovR64Mem(MEM_ARG3, X64Reg::R15, VSlot(inst.rd));
                emitter_.MovR64Imm(REG_THUNK, thunk);
#ifdef _WIN32
                emitter_.SubR64Imm32(X64Reg::RSP, 40);
                emitter_.CallR64(REG_THUNK);
                emitter_.AddR64Imm32(X64Reg::RSP, 40);
#else
                emitter_.SubR64Imm32(X64Reg::RSP, 8);
                emitter_.CallR64(REG_THUNK);
                emitter_.AddR64Imm32(X64Reg::RSP, 8);
#endif
                curr_pc += 4;
                break;
            }

            case Opcode::LDXR:
                EmitSlowCall(SLOW_LDXR, inst.rd, inst.rn, 0, 0, 0,
                             0, 0, inst.is_64bit, 0, 0, 1, 0, 0);
                curr_pc += 4;
                break;

            case Opcode::STXR:
                EmitSlowCall(SLOW_STXR, inst.rd, inst.rn, 0, inst.rs, 0,
                             0, 0, inst.is_64bit, 0, 0, 0, 0, 0);
                curr_pc += 4;
                break;

            case Opcode::CLREX: {
                emitter_.MovR32Imm(X64Reg::RAX, 0);
                emitter_.MovMemR32(X64Reg::R15, ExclActSlot(), X64Reg::RAX);
                emitter_.MovR64Imm(X64Reg::RAX, 0);
                emitter_.MovMemR64(X64Reg::R15, ExclAddrSlot(), X64Reg::RAX);
                curr_pc += 4;
                break;
            }

            case Opcode::FMAX_scalar:
                EmitSlowCall(SLOW_FMAX, inst.rd, inst.rn, inst.rm, 0, 0,
                             0, 0, inst.is_64bit, inst.is_fp_double, 0, 0, 0, 0);
                curr_pc += 4;
                break;

            case Opcode::FMIN_scalar:
                EmitSlowCall(SLOW_FMIN, inst.rd, inst.rn, inst.rm, 0, 0,
                             0, 0, inst.is_64bit, inst.is_fp_double, 0, 0, 0, 0);
                curr_pc += 4;
                break;

            case Opcode::FABS_scalar:
                EmitSlowCall(SLOW_FABS, inst.rd, inst.rn, 0, 0, 0,
                             0, 0, inst.is_64bit, inst.is_fp_double, 0, 0, 0, 0);
                curr_pc += 4;
                break;

            case Opcode::FNEG_scalar:
                EmitSlowCall(SLOW_FNEG, inst.rd, inst.rn, 0, 0, 0,
                             0, 0, inst.is_64bit, inst.is_fp_double, 0, 0, 0, 0);
                curr_pc += 4;
                break;

            case Opcode::FCMP_scalar:
                EmitSlowCall(SLOW_FCMP, inst.rd, inst.rn, inst.rm, 0, 0,
                             0, 0, inst.is_64bit, inst.is_fp_double, 0, 0, 0, 0);
                curr_pc += 4;
                break;

            case Opcode::FCSEL_scalar:
                EmitSlowCall(SLOW_FCSEL, inst.rd, inst.rn, inst.rm, 0, 0,
                             0, 0, inst.is_64bit, inst.is_fp_double,
                             static_cast<u64>(inst.condition), 0, 0, 0);
                curr_pc += 4;
                break;

            case Opcode::FMOV_imm: {
                u64 bits = 0;
                std::memcpy(&bits, &inst.fp_imm, sizeof(bits));
                EmitSlowCall(SLOW_FMOV_IMM, inst.rd, 0, 0, 0, 0,
                             0, 0, inst.is_64bit, inst.is_fp_double, 0, 0, 0, bits);
                curr_pc += 4;
                break;
            }

            case Opcode::UCVTF:
                EmitSlowCall(SLOW_UCVTF, inst.rd, inst.rn, 0, 0, 0,
                             0, 0, inst.is_64bit, inst.is_fp_double, 0, 0, 0, 0);
                curr_pc += 4;
                break;

            case Opcode::FCVTZU:
                EmitSlowCall(SLOW_FCVTZU, inst.rd, inst.rn, 0, 0, 0,
                             0, 0, inst.is_64bit, inst.is_fp_double, 0, 0, 0, 0);
                curr_pc += 4;
                break;

            case Opcode::LDP_fp:
                EmitSlowCall(SLOW_LDP_FP, inst.rd, inst.rn, 0, 0, inst.rt2,
                             0, 0, inst.is_64bit, inst.is_fp_double, 0, 1, inst.imm, 0);
                curr_pc += 4;
                break;

            case Opcode::STP_fp:
                EmitSlowCall(SLOW_STP_FP, inst.rd, inst.rn, 0, 0, inst.rt2,
                             0, 0, inst.is_64bit, inst.is_fp_double, 0, 0, inst.imm, 0);
                curr_pc += 4;
                break;

            case Opcode::ADD_vec:
                EmitSlowCall(SLOW_ADD_VEC, inst.rd, inst.rn, inst.rm, 0, 0,
                             inst.vec_size, 0, 0, 0, 0, 0, 0, 0);
                curr_pc += 4;
                break;

            case Opcode::SUB_vec:
                EmitSlowCall(SLOW_SUB_VEC, inst.rd, inst.rn, inst.rm, 0, 0,
                             inst.vec_size, 0, 0, 0, 0, 0, 0, 0);
                curr_pc += 4;
                break;

            case Opcode::FADD_vec:
                EmitSlowCall(SLOW_FADD_VEC, inst.rd, inst.rn, inst.rm, 0, 0,
                             inst.vec_size, 0, 0, inst.is_fp_double, 0, 0, 0, 0);
                curr_pc += 4;
                break;

            case Opcode::FSUB_vec:
                EmitSlowCall(SLOW_FSUB_VEC, inst.rd, inst.rn, inst.rm, 0, 0,
                             inst.vec_size, 0, 0, inst.is_fp_double, 0, 0, 0, 0);
                curr_pc += 4;
                break;

            case Opcode::FMUL_vec:
                EmitSlowCall(SLOW_FMUL_VEC, inst.rd, inst.rn, inst.rm, 0, 0,
                             inst.vec_size, 0, 0, inst.is_fp_double, 0, 0, 0, 0);
                curr_pc += 4;
                break;

            case Opcode::AND_vec:
                EmitSlowCall(SLOW_AND_VEC, inst.rd, inst.rn, inst.rm, 0, 0,
                             0, 0, 0, 0, 0, 0, 0, 0);
                curr_pc += 4;
                break;

            case Opcode::ORR_vec:
                EmitSlowCall(SLOW_ORR_VEC, inst.rd, inst.rn, inst.rm, 0, 0,
                             0, 0, 0, 0, 0, 0, 0, 0);
                curr_pc += 4;
                break;

            case Opcode::EOR_vec:
                EmitSlowCall(SLOW_EOR_VEC, inst.rd, inst.rn, inst.rm, 0, 0,
                             0, 0, 0, 0, 0, 0, 0, 0);
                curr_pc += 4;
                break;

            case Opcode::DUP_gen:
                EmitSlowCall(SLOW_DUP_GEN, inst.rd, inst.rn, 0, 0, 0,
                             inst.vec_size, inst.vec_index, 0, 0, 0, 0, 0, 0);
                curr_pc += 4;
                break;

            case Opcode::INS_gen:
                EmitSlowCall(SLOW_INS_GEN, inst.rd, inst.rn, 0, 0, 0,
                             inst.vec_size, inst.vec_index, 0, 0, 0, 0, 0, 0);
                curr_pc += 4;
                break;

            case Opcode::UMOV:
                EmitSlowCall(SLOW_UMOV, inst.rd, inst.rn, 0, 0, 0,
                             inst.vec_size, inst.vec_index, inst.is_64bit, 0,
                             0, 0, 0, 0);
                curr_pc += 4;
                break;

            case Opcode::SMOV:
                EmitSlowCall(SLOW_SMOV, inst.rd, inst.rn, 0, 0, 0,
                             inst.vec_size, inst.vec_index, inst.is_64bit, 0,
                             0, 0, 0, 0);
                curr_pc += 4;
                break;

            case Opcode::LDADD:
                EmitSlowCall(SLOW_LDADD, inst.rd, inst.rn, 0, inst.rs, 0,
                             0, 0, inst.is_64bit, 0, 0, 0, 0, 0);
                curr_pc += 4;
                break;

            case Opcode::CAS:
                EmitSlowCall(SLOW_CAS, inst.rd, inst.rn, 0, inst.rs, 0,
                             0, 0, inst.is_64bit, 0, 0, 0, 0, 0);
                curr_pc += 4;
                break;

            case Opcode::SWP:
                EmitSlowCall(SLOW_SWP, inst.rd, inst.rn, 0, inst.rs, 0,
                             0, 0, inst.is_64bit, 0, 0, 0, 0, 0);
                curr_pc += 4;
                break;

            default:
                // Unsupported opcode terminates block
                emitter_.MovR64Imm(X64Reg::RAX, curr_pc);
                emitter_.MovMemR64(X64Reg::R15, OFFSET_PC, X64Reg::RAX);
                block_ended = true;
                break;
        }
    }

    if (!block_ended) {
        emitter_.MovR64Imm(X64Reg::RAX, curr_pc);
        emitter_.MovMemR64(X64Reg::R15, OFFSET_PC, X64Reg::RAX);
    }

    // 3. Epilogue: restore callee-saved registers and return
    emitter_.Pop(X64Reg::R15);
    emitter_.Pop(X64Reg::R14);
    emitter_.Pop(X64Reg::R13);
    emitter_.Pop(X64Reg::R12);
    emitter_.Pop(X64Reg::RBP);
    emitter_.Pop(X64Reg::RBX);
    emitter_.Ret();

    // 4. Allocate into code cache
    u8* exec_ptr = code_cache_.Allocate(emitter_.GetSize());
    if (!exec_ptr) {
        NEMU_LOG_ERROR("JIT", "Failed to allocate code cache buffer for block 0x{:016X}", guest_pc);
        return nullptr;
    }

    std::memcpy(exec_ptr, emitter_.GetCode().data(), emitter_.GetSize());
    code_cache_.Flush(exec_ptr, emitter_.GetSize());

    auto fn = reinterpret_cast<JitBlockFn>(exec_ptr);
    block_map_[guest_pc] = fn;
    stats_.blocks_compiled++;
    stats_.instructions_recompiled += insn_count;

    NEMU_LOG_DEBUG("JIT", "Compiled basic block at 0x{:016X} ({} insns, {} bytes x86-64)",
        guest_pc, insn_count, emitter_.GetSize());

    return fn;
}

bool JitCompiler::Execute(CpuState& state, memory::VirtualMemory& memory) {
    JitBlockFn fn = CompileBlock(state.pc, memory);
    if (!fn) return false;

    fn(&state);
    stats_.blocks_executed++;
    return true;
}

JitCompiler::SvcHandler JitCompiler::svc_handler_;

void JitCompiler::InvokeSvcHandler(CpuState* state, u32 svc_id) {
    if (state == nullptr || !svc_handler_) {
        NEMU_LOG_ERROR("JIT", "SVC thunk invoked without a registered SVC handler");
        state->halted = true;
        return;
    }
    svc_handler_(*state, svc_id);
}

void JitCompiler::InvalidateBlock(vaddr_t guest_pc) {
    block_map_.erase(guest_pc);
}

void JitCompiler::Clear() {
    block_map_.clear();
    code_cache_.Reset();
}

s32 JitCompiler::RegOrSpSlot(u8 reg) const noexcept {
    return reg == 31 ? OFF_SP : XSlot(reg);
}

void JitCompiler::EmitSetX(u8 rd, X64Reg val) {
    if (rd != 31) { // XZR discards writes
        emitter_.MovMemR64(X64Reg::R15, XSlot(rd), val);
    }
}

void JitCompiler::EmitSetW(u8 rd, X64Reg val) {
    if (rd != 31) {
        emitter_.MovMemR32(X64Reg::R15, XSlot(rd), val);
    }
}

void JitCompiler::EmitSetNZCVFromSub() {
    // After `sub` on 64-bit operands, x86 flags map to ARM NZCV as:
    //   ARM N = SF, Z = ZF, C = !CF, V = OF.
    emitter_.SetccMem(X64Reg::R15, OFF_N, Cc::S);   // N  = sign flag
    emitter_.SetccMem(X64Reg::R15, OFF_Z, Cc::E);   // Z  = zero flag
    emitter_.SetccMem(X64Reg::R15, OFF_C, Cc::AE);  // C  = !carry (no borrow)
    emitter_.SetccMem(X64Reg::R15, OFF_V, Cc::O);   // V  = overflow flag
}

void JitCompiler::EmitShiftRegToRdx(bool is_64bit, u8 shift_type, u8 shift_amount) {
    if (shift_amount == 0) {
        return; // LSL/ASR/LSR/ROR by zero are identity in the interpreter's ApplyShift
    }
    if (is_64bit) {
        switch (shift_type) {
            case 0: emitter_.ShlR64Imm(X64Reg::RDX, shift_amount); break; // LSL
            case 1: emitter_.ShrR64Imm(X64Reg::RDX, shift_amount); break; // LSR
            case 2: emitter_.SarR64Imm(X64Reg::RDX, shift_amount); break; // ASR
            case 3: emitter_.RorR64Imm(X64Reg::RDX, shift_amount); break; // ROR
            default: break;
        }
    } else {
        switch (shift_type) {
            case 0: emitter_.ShlR32Imm(X64Reg::RDX, shift_amount); break;
            case 1: emitter_.ShrR32Imm(X64Reg::RDX, shift_amount); break;
            case 2: emitter_.SarR32Imm(X64Reg::RDX, shift_amount); break;
            case 3: emitter_.RorR32Imm(X64Reg::RDX, shift_amount); break;
            default: break;
        }
    }
}

void JitCompiler::EmitConditionToZF(Condition cond) {
    // Produces RAX = 0/1 (1 = condition true) then `cmp rax, 1`, so ZF == 1 iff
    // the ARM condition holds against the four flag bytes in CpuState.pstate.
    // Clobbers RAX, RDX and SCR_TMP (R9); MOV instructions emitted by callers
    // after this do not modify ZF, so the result survives into a following CMOV.
    const s32 nOff = OFF_N, zOff = OFF_Z, cOff = OFF_C, vOff = OFF_V;

#define LDZ(dst, off) emitter_.MovzxR64Mem8((dst), X64Reg::R15, (off))

    switch (cond) {
        case Condition::EQ: LDZ(X64Reg::RAX, zOff); break;
        case Condition::NE: LDZ(X64Reg::RAX, zOff); emitter_.XorR32Imm(X64Reg::RAX, 1); break;
        case Condition::CS: LDZ(X64Reg::RAX, cOff); break;
        case Condition::CC: LDZ(X64Reg::RAX, cOff); emitter_.XorR32Imm(X64Reg::RAX, 1); break;
        case Condition::MI: LDZ(X64Reg::RAX, nOff); break;
        case Condition::PL: LDZ(X64Reg::RAX, nOff); emitter_.XorR32Imm(X64Reg::RAX, 1); break;
        case Condition::VS: LDZ(X64Reg::RAX, vOff); break;
        case Condition::VC: LDZ(X64Reg::RAX, vOff); emitter_.XorR32Imm(X64Reg::RAX, 1); break;
        case Condition::HI: { // C && !Z
            LDZ(X64Reg::RAX, cOff);
            LDZ(X64Reg::RDX, zOff); emitter_.XorR32Imm(X64Reg::RDX, 1);
            emitter_.AndR32R32(X64Reg::RAX, X64Reg::RDX);
            break;
        }
        case Condition::LS: { // !C || Z
            LDZ(X64Reg::RAX, cOff); emitter_.XorR32Imm(X64Reg::RAX, 1);
            LDZ(X64Reg::RDX, zOff);
            emitter_.OrR32R32(X64Reg::RAX, X64Reg::RDX);
            break;
        }
        case Condition::GE: { // N == V
            LDZ(X64Reg::RAX, nOff);
            LDZ(X64Reg::RDX, vOff);
            emitter_.XorR32R32(X64Reg::RAX, X64Reg::RDX); // N ^ V
            emitter_.XorR32Imm(X64Reg::RAX, 1);           // ==
            break;
        }
        case Condition::LT: { // N != V
            LDZ(X64Reg::RAX, nOff);
            LDZ(X64Reg::RDX, vOff);
            emitter_.XorR32R32(X64Reg::RAX, X64Reg::RDX);
            break;
        }
        case Condition::GT: { // !Z && (N == V)
            LDZ(X64Reg::RAX, nOff);
            LDZ(X64Reg::RDX, vOff);
            emitter_.XorR32R32(X64Reg::RAX, X64Reg::RDX); // N ^ V
            emitter_.XorR32Imm(X64Reg::RAX, 1);           // N == V
            LDZ(X64Reg::RDX, zOff); emitter_.XorR32Imm(X64Reg::RDX, 1); // !Z
            emitter_.AndR32R32(X64Reg::RAX, X64Reg::RDX);
            break;
        }
        case Condition::LE: { // Z || (N != V)
            LDZ(X64Reg::RAX, zOff);
            LDZ(SCR_TMP, nOff);
            LDZ(X64Reg::RDX, vOff);
            emitter_.XorR32R32(SCR_TMP, X64Reg::RDX); // N ^ V
            emitter_.OrR32R32(X64Reg::RAX, SCR_TMP);
            break;
        }
        case Condition::AL:
        case Condition::NV:
        default:
            emitter_.MovR32Imm(X64Reg::RAX, 1);
            break;
    }

#undef LDZ

    // ZF = (RAX == 1) == condition true. Passed through the caller's MOVs
    // (MOV never modifies flags), so a following CMOVcc reads the right ZF.
    emitter_.CmpR64Imm(X64Reg::RAX, 1);
    (void)nOff; (void)zOff; (void)cOff; (void)vOff;
}

void JitCompiler::EmitSlowCall(u64 op, u64 rd, u64 rn, u64 rm, u64 rs, u64 rt2,
                               u64 vec_size, u64 vec_index, u64 is64, u64 is_dbl,
                               u64 cond, u64 is_load, u64 imm, u64 fp_imm_bits) {
    // Build the packed descriptor (see JitSlowOp for the layout).
    const u64 p0 = (op & 0xFF) | ((rd & 0xFF) << 8) | ((rn & 0xFF) << 16) |
                   ((rm & 0xFF) << 24) | ((rs & 0xFF) << 32) | ((rt2 & 0xFF) << 40);
    const u64 p1 = (vec_size & 0xFF) | ((vec_index & 0xFF) << 8) |
                   ((is64 & 0xFF) << 16) | ((is_dbl & 0xFF) << 24) |
                   ((cond & 0xFF) << 32) | ((is_load & 0xFF) << 40);
    const u64 thunk = reinterpret_cast<u64>(&JitSlowOp);

    // R15 holds CpuState* throughout the block (callee-saved).
#ifdef _WIN32
    // Microsoft x64 ABI: RCX=s, RDX=m, R8=p0, R9=p1;
    // 5th (p2) and 6th (p3) args at [RSP+0x20] and [RSP+0x28] after reserving
    // 32-byte shadow space. Allocate 56 bytes = 32 shadow + 16 stack args
    // (56 mod 16 == 8, which restores 16-byte call alignment from the block's
    // RSP%16==8 state). Stack slots written after the subtract.
    emitter_.SubR64Imm32(X64Reg::RSP, 56);
    emitter_.MovR64Imm(X64Reg::RAX, imm);
    emitter_.MovMemR64(X64Reg::RSP, 0x20, X64Reg::RAX); // 5th arg: p2
    emitter_.MovR64Imm(X64Reg::RAX, fp_imm_bits);
    emitter_.MovMemR64(X64Reg::RSP, 0x28, X64Reg::RAX); // 6th arg: p3
    emitter_.MovR64R64(X64Reg::RCX, X64Reg::R15);       // arg 1: CpuState*
    emitter_.MovR64Imm(X64Reg::RDX, mem_addr_);          // arg 2: VirtualMemory*
    emitter_.MovR64Imm(X64Reg::R8, p0);                  // arg 3
    emitter_.MovR64Imm(X64Reg::R9, p1);                  // arg 4
#else
    // System V x64 ABI: RDI=s, RSI=m, RDX=p0, RCX=p1, R8=p2, R9=p3 (6 reg args).
    // RSP%16==8 in-block; subtract 8 to align the stack relative to the call.
    emitter_.SubR64Imm32(X64Reg::RSP, 8);
    emitter_.MovR64R64(X64Reg::RDI, X64Reg::R15);       // arg 1: CpuState*
    emitter_.MovR64Imm(X64Reg::RSI, mem_addr_);          // arg 2: VirtualMemory*
    emitter_.MovR64Imm(X64Reg::RDX, p0);                 // arg 3
    emitter_.MovR64Imm(X64Reg::RCX, p1);                 // arg 4
    emitter_.MovR64Imm(X64Reg::R8, imm);                 // arg 5: p2
    emitter_.MovR64Imm(X64Reg::R9, fp_imm_bits);         // arg 6: p3
#endif

    emitter_.MovR64Imm(REG_THUNK, thunk);
    emitter_.CallR64(REG_THUNK);

#ifdef _WIN32
    emitter_.AddR64Imm32(X64Reg::RSP, 56);
#else
    emitter_.AddR64Imm32(X64Reg::RSP, 8);
#endif
}

void JitCompiler::EmitMemAccess(bool is_load, bool is_64bit, u8 rn, u64 offset, u8 rt) {
    // Effective address = GetRegOrSP(rn) + offset, held in SCR_ADDR (R8).
    emitter_.MovR64Mem(SCR_ADDR, X64Reg::R15, RegOrSpSlot(rn));
    emitter_.AddR64Imm32(SCR_ADDR, static_cast<s32>(offset));

    if (is_load) {
        const u64 thunk = reinterpret_cast<u64>(is_64bit ? &JitMemRead64 : &JitMemRead32);
        // Arg1 = &memory, Arg2 = effective address.
        emitter_.MovR64Imm(MEM_ARG1, mem_addr_);
        emitter_.MovR64R64(MEM_ARG2, SCR_ADDR);
        emitter_.MovR64Imm(REG_THUNK, thunk);
#ifdef _WIN32
        emitter_.SubR64Imm32(X64Reg::RSP, 40);
        emitter_.CallR64(REG_THUNK);
        emitter_.AddR64Imm32(X64Reg::RSP, 40);
#else
        emitter_.SubR64Imm32(X64Reg::RSP, 8);
        emitter_.CallR64(REG_THUNK);
        emitter_.AddR64Imm32(X64Reg::RSP, 8);
#endif
        // Result (zero-extended u64) lands in RAX. Store to Xt/Wt.
        if (is_64bit) {
            EmitSetX(rt, X64Reg::RAX);
        } else {
            EmitSetW(rt, X64Reg::RAX);
        }
    } else {
        const u64 thunk = reinterpret_cast<u64>(is_64bit ? &JitMemWrite64 : &JitMemWrite32);
        // Source value = GetX(rt) (XZR -> 0); compute it before clobbering arg regs.
        if (rt == 31) {
            emitter_.MovR64Imm(X64Reg::RAX, 0); // STR XZR stores zero
        } else {
            emitter_.MovR64Mem(X64Reg::RAX, X64Reg::R15, XSlot(rt));
        }
        emitter_.MovR64Imm(MEM_ARG1, mem_addr_);
        emitter_.MovR64R64(MEM_ARG2, SCR_ADDR);
        emitter_.MovR64R64(MEM_ARG3, X64Reg::RAX);
        emitter_.MovR64Imm(REG_THUNK, thunk);
#ifdef _WIN32
        emitter_.SubR64Imm32(X64Reg::RSP, 40);
        emitter_.CallR64(REG_THUNK);
        emitter_.AddR64Imm32(X64Reg::RSP, 40);
#else
        emitter_.SubR64Imm32(X64Reg::RSP, 8);
        emitter_.CallR64(REG_THUNK);
        emitter_.AddR64Imm32(X64Reg::RSP, 8);
#endif
    }
}

} // namespace nemu::core::cpu::jit
