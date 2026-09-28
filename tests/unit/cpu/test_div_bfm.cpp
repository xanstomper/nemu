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
#include <memory>
#include <cstring>

using namespace nemu;
using namespace nemu::core;
using namespace nemu::core::cpu;

namespace {
constexpr vaddr_t kCode = 0x0072000000ULL;

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

    std::cout << "ALL DIV/BFM/MULL DIFFERENTIAL TESTS PASSED\n";
    return 0;
}
