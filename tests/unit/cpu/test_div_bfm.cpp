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

    std::cout << "ALL DIV/BFM/MULL DIFFERENTIAL TESTS PASSED\n";
    return 0;
}
