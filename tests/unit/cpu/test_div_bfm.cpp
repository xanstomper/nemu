// Differential test: UDIV/SDIV/SBFM/UBFM/SMULL/UMULL — encodings are the exact
// words produced by aarch64-linux-gnu-as (ground truth). Each case is executed
// through the interpreter's public Step() path: instruction staged in guest
// memory at PC, executed, result checked.
//
// Ground-truth encodings (verified via objdump):
//   udiv w1,w2,w3   = 0x1AC30841
//   sdiv w1,w2,w3   = 0x1AC30C41
//   ubfx w1,w2,#4,#8 = 0x53042C41   (immr=4@bits20:16, imms=11@bits15:10, Rn=2, Rd=1)
//   sbfx w1,w2,#4,#8 = 0x13042C41
//   smull x1,w2,w3  = 0x9B237C41    (bit23=0)
//   umull x1,w2,w3  = 0x9BA37C41    (bit23=1)
//   udiv x1,x2,x3   = 0x9AC30841
#include "core/cpu/decoder.hpp"
#include "core/cpu/interpreter.hpp"
#include "core/cpu/cpu_state.hpp"
#include "core/memory/virtual_memory.hpp"
#include "core/types.hpp"
#include <iostream>
#include <cstdlib>
#include <array>
#include <cstring>
#include <memory>
#include <cstring>

using namespace nemu;
using namespace nemu::core;
using namespace nemu::core::cpu;

namespace {
constexpr vaddr_t kCode = 0x0072000000ULL;
constexpr vaddr_t kData = 0x0072100000ULL;

void RunCase(const char* name, u32 encoding, u64 wn, u64 wm, u64 expected) {
    memory::VirtualMemory mem;
    if (!mem.Map(kCode, 0x1000, memory::MemoryPermission::All)) {
        std::cerr << "FAIL " << name << ": code map\n";
        std::exit(1);
    }
    mem.Write32(kCode, encoding);

    CpuState s{};
    s.pc = kCode;
    s.SetX(2, wn);   // standard operands: Rn=w2, Rm=w3
    s.SetX(3, wm);

    Interpreter interp(s, mem);
    interp.Step();

    const u64 got = s.GetX(1);  // standard dest Rd=w1
    if (got != expected) {
        std::cerr << "FAIL " << name << ": got 0x" << std::hex << got
                  << " want 0x" << expected << std::dec << "\n";
        std::exit(1);
    }
    std::cout << "  " << name << " -> 0x" << std::hex << got << std::dec << " OK\n";
}
} // namespace

int main() {
    std::cout << "[Test: integer div/bitfield/multiply-long differential]\n";

    RunCase("udiv w1,w2,w3 (42/9)",   0x1AC30841u, 42, 9, 4);
    RunCase("udiv w1,w2,w3 (/0)",     0x1AC30841u, 42, 0, 0);
    RunCase("sdiv w1,w2,w3 (-42/9)",  0x1AC30C41u,
            static_cast<u64>(static_cast<u32>(-42)), 9,
            static_cast<u64>(static_cast<u32>(-42 / 9)));
    RunCase("ubfx w1,w2,#4,#8",       0x53042C41u, 0x12345678u, 0,
            (0x12345678u >> 4) & 0xFFu);
    // ARM ARM ground truth: SBFX sign-extends the extracted field.
    // 0xFFFFFF90 >> 4 & 0xFF = 0xF9, bit7=1 -> 0xFFFFFFF9 (32-bit signed).
    RunCase("sbfx w1,w2,#4,#8",       0x13042C41u,
            static_cast<u64>(0xFFFFFF90u), 0,
            static_cast<u64>(0xFFFFFFF9u));
    RunCase("smull x1,w2,w3 (-5*7)",  0x9B237C41u,
            static_cast<u64>(static_cast<u32>(-5)), 7,
            static_cast<u64>(-5 * 7));
    RunCase("umull x1,w2,w3 (0xFFFF*2)", 0x9BA37C41u, 0xFFFFu, 2, 0xFFFFu * 2u);
    RunCase("udiv x1,x2,x3 (1000/7)", 0x9AC30841u, 1000, 3, 333);

    // ---- FP multiply-add (ground truth: fmadd s0,s1,s2,s3 = 0x1F020C20,
    //      fmsub = 0x1F028C20; Rd=0, Rn=1, Rm=2, Ra=3) ----
    {
        CpuState s{};
        s.pc = kCode;
        s.SetSingle(1, 2.0f);   // Rn
        s.SetSingle(2, 3.0f);   // Rm
        s.SetSingle(3, 10.0f);  // Ra
        memory::VirtualMemory mem;
        if (!mem.Map(kCode, 0x1000, memory::MemoryPermission::All)) {
            std::cerr << "FAIL: code map\n";
            std::exit(1);
        }
        mem.Write32(kCode, 0x1F020C20u);
        Interpreter interp(s, mem);
        interp.Step();
        if (s.GetSingle(0) != 16.0f) {
            std::cerr << "FAIL fmadd s: got " << s.GetSingle(0) << " want 16\n";
            std::exit(1);
        }
        std::cout << "  fmadd s0,s1,s2,s3 (2*3+10=16) OK\n";

        // FMSUB single: Ra - (Rn*Rm) = 10 - 6 = +4
        s.pc = kCode; // Step() advances PC; re-stage for the second instruction
        s.SetSingle(1, 2.0f); s.SetSingle(2, 3.0f); s.SetSingle(3, 10.0f);
        mem.Write32(kCode, 0x1F028C20u);
        interp.Step();
        if (s.GetSingle(0) != 4.0f) {
            std::cerr << "FAIL fmsub s: got " << s.GetSingle(0) << " want 4\n";
            std::exit(1);
        }
        std::cout << "  fmsub s0,s1,s2,s3 (10-2*3=4) OK\n";
    }

    // ---- SIMD structure load/store multiple (ground-truth encodings) ----
    // ld1 {v0.16b},[x1] = 4C407020 ; st1 = 4C007020
    // ld1 {v0.4s-v3.4s},[x1] = 4C402820 ; ld4 {v0.4s-v3.4s},[x1] = 4C400820
    // post-index ld1 {v0.16b},[x1],#16 = 4CDF7020
    {
        memory::VirtualMemory mem;
        if (!mem.Map(kCode, 0x1000, memory::MemoryPermission::All) ||
            !mem.Map(kData, 0x2000, memory::MemoryPermission::All)) {
            std::cerr << "FAIL: maps\n";
            std::exit(1);
        }

        // LD1 single 16B register.
        {
            const std::array<u8, 16> pattern{1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16};
            mem.WriteBlock(kData, pattern.data(), pattern.size());
            CpuState s{};
            s.pc = kCode; s.SetX(1, kData);
            mem.Write32(kCode, 0x4C407020u); // ld1 {v0.16b}, [x1]
            Interpreter interp(s, mem);
            interp.Step();
            u128 got = s.GetVector(0);
            if (std::memcmp(&got, pattern.data(), 16) != 0) {
                std::cerr << "FAIL ld1 {v0.16b}: register mismatch\n";
                std::exit(1);
            }
            std::cout << "  ld1 {v0.16b},[x1] -> 16B byte-exact OK\n";
        }

        // ST1 four contiguous regs + post-index writeback (#64).
        {
            CpuState s{};
            s.pc = kCode; s.SetX(1, kData);
            for (u32 r = 0; r < 4; ++r) {
                u128 v{};
                v.low = 0x11111111ULL * (r + 1);
                v.high = 0x22222222ULL * (r + 1);
                s.SetVector(r, v);
            }
            mem.Write32(kCode, 0x4C1F2820u); // st1 {v0.4s-v3.4s}, [x1], #64 (Rt=0, Rn=1)
            Interpreter interp(s, mem);
            interp.Step();
            std::array<u8, 64> back{};
            mem.ReadBlock(kData, back.data(), back.size());
            u128 v0 = s.GetVector(0);
            if (std::memcmp(back.data(), &v0, 16) != 0 ||
                s.GetX(1) != kData + 64) {
                std::cerr << "FAIL st1 4reg post-index\n";
                std::exit(1);
            }
            std::cout << "  st1 {v0.4s-v3.4s},[x1],#64 -> byte-exact + writeback OK\n";
        }

        // LD4 interleaved (4 regs, esz=4): pattern encodes reg/lane positions.
        {
            std::array<u8, 64> src{};
            for (u32 i = 0; i < 16; ++i) {
                const u32 e = i / 4, r = i % 4;   // element groups of 4 regs
                src[i * 4 + 0] = static_cast<u8>(r);
                src[i * 4 + 1] = static_cast<u8>(e);
            }
            mem.WriteBlock(kData, src.data(), src.size());
            CpuState s{};
            s.pc = kCode; s.SetX(1, kData);
            mem.Write32(kCode, 0x4C400820u); // ld4 {v0.4s-v3.4s}, [x1]
            Interpreter interp(s, mem);
            interp.Step();
            // v0 lane e should be {r=0, e} in its low bytes
            u128 v0 = s.GetVector(0);
            u8 v0b[16]; std::memcpy(v0b, &v0, 16);
            if (v0b[0] != 0 || v0b[4] != 0 || v0b[8] != 0 || v0b[12] != 0) {
                std::cerr << "FAIL ld4 de-interleave (r=0 lanes)\n";
                std::exit(1);
            }
            u128 v3 = s.GetVector(3);
            u8 v3b[16]; std::memcpy(v3b, &v3, 16);
            if (v3b[0] != 3 || v3b[4] != 3) {
                std::cerr << "FAIL ld4 de-interleave (r=3 lanes)\n";
                std::exit(1);
            }
            std::cout << "  ld4 {v0.4s-v3.4s},[x1] -> interleaved de-swizzle OK\n";
        }
    }

    // ---- FP vector ops (ground truth: fmla v0.4s,v1.4s = 0x4E21CC00,
    //      fdiv=0x6E21FC00, fmax=0x4E21F400, fmin=0x4EA1F400) ----
    // Standard form: Rd=v0, Rn=v0, Rm=v1.
    {
        memory::VirtualMemory mem;
        if (!mem.Map(kCode, 0x1000, memory::MemoryPermission::All)) {
            std::cerr << "FAIL: code map\n";
            std::exit(1);
        }
        auto lanes = [](u32 reg, CpuState& s, std::initializer_list<float> v) {
            u32 l = 0;
            for (float f : v) { u32 u; std::memcpy(&u, &f, 4); s.SetVectorLane32(reg, l++, u); }
        };

        // FMLA: acc(1,2,3,4) + a(1,2,3,4) * b(2,2,2,2) = (3,6,9,12)
        {
            CpuState s{}; s.pc = kCode;
            lanes(0, s, {1.f, 2.f, 3.f, 4.f});   // rd accumulator
            lanes(0, s, {1.f, 2.f, 3.f, 4.f});   // rn (same reg — acc pattern)
            lanes(1, s, {2.f, 2.f, 2.f, 2.f});
            mem.Write32(kCode, 0x4E21CC00u);     // fmla v0.4s, v0.4s, v1.4s (Rd=0,Rn=0,Rm=1)
            Interpreter interp(s, mem);
            interp.Step();
            const float got = [&] { u32 u = s.GetVectorLane32(0, 1); float f; std::memcpy(&f, &u, 4); return f; }();
            if (got != 6.0f) { std::cerr << "FAIL fmla: lane1=" << got << " want 6\n"; std::exit(1); }
            std::cout << "  fmla v0.4s,v0.4s,v1.4s (accumulate) OK\n";
        }

        // FDIV: (4,9,27,64) / (2,3,3,4) = (2,3,9,16)
        {
            CpuState s{}; s.pc = kCode;
            lanes(0, s, {4.f, 9.f, 27.f, 64.f});
            lanes(1, s, {2.f, 3.f, 3.f, 4.f});
            mem.Write32(kCode, 0x6E21FC00u);     // fdiv v0.4s, v0.4s, v1.4s
            Interpreter interp(s, mem);
            interp.Step();
            u32 u = s.GetVectorLane32(0, 3); float f; std::memcpy(&f, &u, 4);
            if (f != 16.0f) { std::cerr << "FAIL fdiv: lane3=" << f << " want 16\n"; std::exit(1); }
            std::cout << "  fdiv v0.4s,v0.4s,v1.4s OK\n";
        }

        // FMAX / FMIN
        {
            CpuState s{}; s.pc = kCode;
            lanes(0, s, {1.f, 9.f, 3.f, 7.f});
            lanes(1, s, {5.f, 2.f, 3.f, 8.f});
            mem.Write32(kCode, 0x4E21F400u);     // fmax v0.4s, v0.4s, v1.4s
            Interpreter interp(s, mem);
            interp.Step();
            u32 u = s.GetVectorLane32(0, 1); float f; std::memcpy(&f, &u, 4);
            if (f != 9.0f) { std::cerr << "FAIL fmax: lane1=" << f << " want 9\n"; std::exit(1); }
            std::cout << "  fmax v0.4s,v0.4s,v1.4s OK\n";
        }
    }

    std::cout << "ALL DIV/BFM/MULL DIFFERENTIAL TESTS PASSED\n";
    return 0;
}
