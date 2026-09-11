#include "core/cpu/cpu_state.hpp"
#include "core/cpu/instruction.hpp"
#include "core/cpu/decoder.hpp"
#include "core/cpu/interpreter.hpp"
#include "core/memory/virtual_memory.hpp"
#include <iostream>
#include <cstdlib>

using namespace nemu;
using namespace nemu::core;

#define NEMU_TEST_ASSERT(cond) do { \
    if (!(cond)) { \
        std::cerr << "[FAIL] Assertion failed: " #cond " at " << __FILE__ << ":" << __LINE__ << std::endl; \
        std::abort(); \
    } \
} while (0)

void TestAddImmediate() {
    std::cout << "[TEST] Running TestAddImmediate...\n";
    memory::VirtualMemory mem;
    NEMU_TEST_ASSERT(mem.Map(0x1000, 0x1000, memory::MemoryPermission::All));

    // ADD X0, XZR, #100   (0x910193E0)
    // ADDS X1, X0, #200   (0xB1032001)
    // ADDS X2, XZR, #0    (0xB10003E2) -> Z=1
    const u32 code[] = {
        0x910193E0,
        0xB1032001,
        0xB10003E2
    };
    NEMU_TEST_ASSERT(mem.WriteBlock(0x1000, code, sizeof(code)));

    cpu::CpuState state;
    state.Reset();
    state.pc = 0x1000;

    cpu::Interpreter interp(state, mem);

    // Step 1: ADD X0, XZR, #100
    NEMU_TEST_ASSERT(interp.Step() == cpu::StepResult::Ok);
    NEMU_TEST_ASSERT(state.GetX(0) == 100);
    NEMU_TEST_ASSERT(state.pc == 0x1004);

    // Step 2: ADDS X1, X0, #200
    NEMU_TEST_ASSERT(interp.Step() == cpu::StepResult::Ok);
    NEMU_TEST_ASSERT(state.GetX(1) == 300);
    NEMU_TEST_ASSERT(!state.pstate.z);
    NEMU_TEST_ASSERT(!state.pstate.n);
    NEMU_TEST_ASSERT(!state.pstate.c);
    NEMU_TEST_ASSERT(!state.pstate.v);

    // Step 3: ADDS X2, XZR, #0
    NEMU_TEST_ASSERT(interp.Step() == cpu::StepResult::Ok);
    NEMU_TEST_ASSERT(state.GetX(2) == 0);
    NEMU_TEST_ASSERT(state.pstate.z);
    NEMU_TEST_ASSERT(!state.pstate.n);

    std::cout << "  PASSED.\n";
}

void TestSubImmediateAndCmp() {
    std::cout << "[TEST] Running TestSubImmediateAndCmp...\n";
    memory::VirtualMemory mem;
    NEMU_TEST_ASSERT(mem.Map(0x2000, 0x1000, memory::MemoryPermission::All));

    // 1. ADD X0, XZR, #200      (0x910323E0)
    // 2. SUBS XZR, X0, #200     (0xF103201F) -> CMP X0, #200 (Z=1, C=1)
    // 3. SUBS X1, X0, #300      (0xF104B001) -> X1 = -100, N=1, C=0 (borrow)
    const u32 code[] = {
        0x910323E0,
        0xF103201F,
        0xF104B001
    };
    NEMU_TEST_ASSERT(mem.WriteBlock(0x2000, code, sizeof(code)));

    cpu::CpuState state;
    state.Reset();
    state.pc = 0x2000;

    cpu::Interpreter interp(state, mem);

    NEMU_TEST_ASSERT(interp.Step() == cpu::StepResult::Ok);
    NEMU_TEST_ASSERT(state.GetX(0) == 200);

    // CMP X0, #200
    NEMU_TEST_ASSERT(interp.Step() == cpu::StepResult::Ok);
    NEMU_TEST_ASSERT(state.pstate.z == true);
    NEMU_TEST_ASSERT(state.pstate.c == true); // 200 >= 200 -> C=1 (no borrow)
    NEMU_TEST_ASSERT(state.pstate.n == false);

    // SUBS X1, X0, #300
    NEMU_TEST_ASSERT(interp.Step() == cpu::StepResult::Ok);
    NEMU_TEST_ASSERT(state.GetX(1) == static_cast<u64>(-100LL));
    NEMU_TEST_ASSERT(state.pstate.n == true);
    NEMU_TEST_ASSERT(state.pstate.c == false); // 200 < 300 -> C=0 (borrow)
    NEMU_TEST_ASSERT(state.pstate.z == false);

    std::cout << "  PASSED.\n";
}

void TestMovesAndLogic() {
    std::cout << "[TEST] Running TestMovesAndLogic...\n";
    memory::VirtualMemory mem;
    NEMU_TEST_ASSERT(mem.Map(0x3000, 0x1000, memory::MemoryPermission::All));

    // 1. MOVZ X0, #0x1234, LSL #16 (0xD2A24680)
    // 2. MOVK X0, #0x5678, LSL #0  (0xF28ACF00)
    // 3. ORR  X1, XZR, X0          (0xAA0003E1) -> MOV X1, X0
    // 4. EOR  X2, X1, X0           (0xCA000022) -> X2 = 0
    const u32 code[] = {
        0xD2A24680,
        0xF28ACF00,
        0xAA0003E1,
        0xCA000022
    };
    NEMU_TEST_ASSERT(mem.WriteBlock(0x3000, code, sizeof(code)));

    cpu::CpuState state;
    state.Reset();
    state.pc = 0x3000;

    cpu::Interpreter interp(state, mem);

    NEMU_TEST_ASSERT(interp.Step() == cpu::StepResult::Ok);
    NEMU_TEST_ASSERT(state.GetX(0) == 0x12340000);

    NEMU_TEST_ASSERT(interp.Step() == cpu::StepResult::Ok);
    NEMU_TEST_ASSERT(state.GetX(0) == 0x12345678);

    NEMU_TEST_ASSERT(interp.Step() == cpu::StepResult::Ok);
    NEMU_TEST_ASSERT(state.GetX(1) == 0x12345678);

    NEMU_TEST_ASSERT(interp.Step() == cpu::StepResult::Ok);
    NEMU_TEST_ASSERT(state.GetX(2) == 0);

    std::cout << "  PASSED.\n";
}

void TestBranchAndLink() {
    std::cout << "[TEST] Running TestBranchAndLink...\n";
    memory::VirtualMemory mem;
    NEMU_TEST_ASSERT(mem.Map(0x4000, 0x1000, memory::MemoryPermission::All));

    // 0x4000: BL +8     (0x94000002) -> jumps to 0x4008, LR=0x4004
    // 0x4004: NOP       (0xD503201F)
    // 0x4008: RET       (0xD65F03C0) -> jumps to LR (0x4004)
    const u32 code[] = {
        0x94000002,
        0xD503201F,
        0xD65F03C0
    };
    NEMU_TEST_ASSERT(mem.WriteBlock(0x4000, code, sizeof(code)));

    cpu::CpuState state;
    state.Reset();
    state.pc = 0x4000;

    cpu::Interpreter interp(state, mem);

    // BL +8
    NEMU_TEST_ASSERT(interp.Step() == cpu::StepResult::Ok);
    NEMU_TEST_ASSERT(state.GetX(30) == 0x4004);
    NEMU_TEST_ASSERT(state.pc == 0x4008);

    // RET (jumps to X30)
    NEMU_TEST_ASSERT(interp.Step() == cpu::StepResult::Ok);
    NEMU_TEST_ASSERT(state.pc == 0x4004);

    // NOP at 0x4004
    NEMU_TEST_ASSERT(interp.Step() == cpu::StepResult::Ok);
    NEMU_TEST_ASSERT(state.pc == 0x4008);

    std::cout << "  PASSED.\n";
}

void TestLoadStore() {
    std::cout << "[TEST] Running TestLoadStore...\n";
    memory::VirtualMemory mem;
    NEMU_TEST_ASSERT(mem.Map(0x5000, 0x2000, memory::MemoryPermission::All));

    // Setup stack at 0x6000
    // 0x5000: MOVZ X0, #0xABCD      (0xD29579A0)
    // 0x5004: STR X0, [SP, #16]     (0xF9000BE0)
    // 0x5008: LDR X1, [SP, #16]     (0xF9400BE1)
    const u32 code[] = {
        0xD29579A0,
        0xF9000BE0,
        0xF9400BE1
    };
    NEMU_TEST_ASSERT(mem.WriteBlock(0x5000, code, sizeof(code)));

    cpu::CpuState state;
    state.Reset();
    state.pc = 0x5000;
    state.sp = 0x6000;

    cpu::Interpreter interp(state, mem);

    NEMU_TEST_ASSERT(interp.Step() == cpu::StepResult::Ok);
    NEMU_TEST_ASSERT(state.GetX(0) == 0xABCD);

    NEMU_TEST_ASSERT(interp.Step() == cpu::StepResult::Ok);
    NEMU_TEST_ASSERT(mem.Read64(0x6010) == 0xABCD);

    NEMU_TEST_ASSERT(interp.Step() == cpu::StepResult::Ok);
    NEMU_TEST_ASSERT(state.GetX(1) == 0xABCD);

    std::cout << "  PASSED.\n";
}

int main() {
    std::cout << "========================================\n";
    std::cout << "    NEMU CPU INSTRUCTION UNIT TESTS     \n";
    std::cout << "========================================\n";

    TestAddImmediate();
    TestSubImmediateAndCmp();
    TestMovesAndLogic();
    TestBranchAndLink();
    TestLoadStore();

    std::cout << "ALL CPU UNIT TESTS PASSED SUCCESSFULLY!\n";
    return 0;
}
