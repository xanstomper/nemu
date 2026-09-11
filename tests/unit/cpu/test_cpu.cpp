#include "core/cpu/cpu_state.hpp"
#include "core/cpu/instruction.hpp"
#include "core/cpu/decoder.hpp"
#include "core/cpu/interpreter.hpp"
#include "core/memory/virtual_memory.hpp"
#include <iostream>
#include <cstdlib>

using namespace nemu;
using namespace nemu::core;

#define NEMU_TEST_ASSERT(cond, ...) do { \
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

void TestFpScalarArithmetic() {
    std::cout << "[TEST] Running TestFpScalarArithmetic...\n";
    memory::VirtualMemory mem;
    NEMU_TEST_ASSERT(mem.Map(0x6000, 0x1000, memory::MemoryPermission::All));

    // Test program:
    // 1. FADD S2, S0, S1   (0x1E212802): S2 = 1.5 + 2.5 = 4.0
    // 2. FSUB S3, S2, S0   (0x1E203843): S3 = 4.0 - 1.5 = 2.5
    // 3. FMUL S4, S2, S0   (0x1E200844): S4 = 4.0 * 1.5 = 6.0
    // 4. FDIV S5, S4, S2   (0x1E221885): S5 = 6.0 / 4.0 = 1.5
    // 5. FABS S6, S3       (0x1E20C066): S6 = |2.5| = 2.5
    // 6. FNEG S7, S3       (0x1E214067): S7 = -2.5
    // 7. FSQRT S8, S2      (0x1E21C048): S8 = sqrt(4.0) = 2.0
    // 8. FCMP S0, S1       (0x1E212000): 1.5 vs 2.5 -> C=0, Z=0, N=1
    // 9. SCVTF S10, W0     (0x1E22000A): W0 (42) -> S10 (42.0f)
    // 10. FCVTZS W1, S10   (0x1E380141): S10 (42.0f) -> W1 (42)
    const u32 code[] = {
        0x1E212802,
        0x1E203843,
        0x1E200844,
        0x1E221885,
        0x1E20C066,
        0x1E214067,
        0x1E21C048,
        0x1E212000,
        0x1E22000A,
        0x1E380141
    };
    NEMU_TEST_ASSERT(mem.WriteBlock(0x6000, code, sizeof(code)));

    cpu::CpuState state;
    state.Reset();
    state.pc = 0x6000;
    state.SetSingle(0, 1.5f);
    state.SetSingle(1, 2.5f);
    state.SetX(0, 42);

    cpu::Interpreter interp(state, mem);

    // 1. FADD
    NEMU_TEST_ASSERT(interp.Step() == cpu::StepResult::Ok);
    NEMU_TEST_ASSERT(state.GetSingle(2) == 4.0f);

    // 2. FSUB
    NEMU_TEST_ASSERT(interp.Step() == cpu::StepResult::Ok);
    NEMU_TEST_ASSERT(state.GetSingle(3) == 2.5f);

    // 3. FMUL
    NEMU_TEST_ASSERT(interp.Step() == cpu::StepResult::Ok);
    NEMU_TEST_ASSERT(state.GetSingle(4) == 6.0f);

    // 4. FDIV
    NEMU_TEST_ASSERT(interp.Step() == cpu::StepResult::Ok);
    NEMU_TEST_ASSERT(state.GetSingle(5) == 1.5f);

    // 5. FABS
    NEMU_TEST_ASSERT(interp.Step() == cpu::StepResult::Ok);
    NEMU_TEST_ASSERT(state.GetSingle(6) == 2.5f);

    // 6. FNEG
    NEMU_TEST_ASSERT(interp.Step() == cpu::StepResult::Ok);
    NEMU_TEST_ASSERT(state.GetSingle(7) == -2.5f);

    // 7. FSQRT
    NEMU_TEST_ASSERT(interp.Step() == cpu::StepResult::Ok);
    NEMU_TEST_ASSERT(state.GetSingle(8) == 2.0f);

    // 8. FCMP
    NEMU_TEST_ASSERT(interp.Step() == cpu::StepResult::Ok);
    NEMU_TEST_ASSERT(state.pstate.n == true); // 1.5 < 2.5
    NEMU_TEST_ASSERT(state.pstate.z == false);

    // 9. SCVTF
    NEMU_TEST_ASSERT(interp.Step() == cpu::StepResult::Ok);
    NEMU_TEST_ASSERT(state.GetSingle(10) == 42.0f);

    // 10. FCVTZS
    NEMU_TEST_ASSERT(interp.Step() == cpu::StepResult::Ok);
    NEMU_TEST_ASSERT(state.GetX(1) == 42);

    std::cout << "  PASSED.\n";
}

void TestFpLoadStore() {
    std::cout << "[TEST] Running TestFpLoadStore...\n";
    memory::VirtualMemory mem;
    NEMU_TEST_ASSERT(mem.Map(0x7000, 0x2000, memory::MemoryPermission::All));

    // 1. STR S0, [SP, #16]    (0xBD0013E0)
    // 2. LDR S1, [SP, #16]    (0xBD4013E1)
    const u32 code[] = {
        0xBD0013E0,
        0xBD4013E1
    };
    NEMU_TEST_ASSERT(mem.WriteBlock(0x7000, code, sizeof(code)));

    cpu::CpuState state;
    state.Reset();
    state.pc = 0x7000;
    state.sp = 0x8000;
    state.SetSingle(0, 123.456f);

    cpu::Interpreter interp(state, mem);

    // 1. STR S0, [SP, #16]
    NEMU_TEST_ASSERT(interp.Step() == cpu::StepResult::Ok);
    NEMU_TEST_ASSERT(mem.Read32(0x8010) == *reinterpret_cast<const u32*>(&state.v[0]));

    // 2. LDR S1, [SP, #16]
    NEMU_TEST_ASSERT(interp.Step() == cpu::StepResult::Ok);
    NEMU_TEST_ASSERT(state.GetSingle(1) == 123.456f);

    std::cout << "  PASSED.\n";
}

void TestNeonVectorOps() {
    std::cout << "[TEST] Running TestNeonVectorOps...\n";
    memory::VirtualMemory mem;
    NEMU_TEST_ASSERT(mem.Map(0x9000, 0x1000, memory::MemoryPermission::All));

    // 1. ADD_vec V2.4S, V0.4S, V1.4S (0x4EA18002)
    // 2. SUB_vec V3.4S, V2.4S, V0.4S (0x6EA08443) -> u=1, opcode=0b10000, Rn=2, Rm=0, Rd=3
    const u32 code[] = {
        0x4EA18002,
        0x6EA08443
    };
    NEMU_TEST_ASSERT(mem.WriteBlock(0x9000, code, sizeof(code)));

    cpu::CpuState state;
    state.Reset();
    state.pc = 0x9000;
    for (u32 i = 0; i < 4; ++i) {
        state.SetVectorLane32(0, i, (i + 1) * 10);
        state.SetVectorLane32(1, i, 5);
    }

    cpu::Interpreter interp(state, mem);

    // 1. ADD_vec: [10, 20, 30, 40] + [5, 5, 5, 5] = [15, 25, 35, 45]
    NEMU_TEST_ASSERT(interp.Step() == cpu::StepResult::Ok);
    for (u32 i = 0; i < 4; ++i) {
        NEMU_TEST_ASSERT(state.GetVectorLane32(2, i) == ((i + 1) * 10 + 5));
    }

    // 2. SUB_vec: [15, 25, 35, 45] - [10, 20, 30, 40] = [5, 5, 5, 5]
    NEMU_TEST_ASSERT(interp.Step() == cpu::StepResult::Ok);
    for (u32 i = 0; i < 4; ++i) {
        NEMU_TEST_ASSERT(state.GetVectorLane32(3, i) == 5);
    }

    // Decode-regression: FADD/FSUB/FMUL (vector) must decode to distinct opcodes.
    // FSUB shares opcode 0b11010 and u==0 with FADD, so it must be told apart
    // by bit 23. FMUL uses opcode 0b11011. (Verified against aarch64-linux-gnu-as.)
    {
        const auto d_fadd = cpu::Decoder::Decode(0x4E62D420u); // FADD v0.2d, v1.2d, v2.2d
        const auto d_fsub = cpu::Decoder::Decode(0x4EE5D483u); // FSUB v3.2d, v4.2d, v5.2d
        const auto d_fmul = cpu::Decoder::Decode(0x6E68DCE6u); // FMUL v6.2d, v7.2d, v8.2d
        NEMU_TEST_ASSERT(d_fadd.opcode == cpu::Opcode::FADD_vec);
        NEMU_TEST_ASSERT(d_fsub.opcode == cpu::Opcode::FSUB_vec);
        NEMU_TEST_ASSERT(d_fmul.opcode == cpu::Opcode::FMUL_vec);
    }

    std::cout << "  PASSED.\n";
}

void TestAtomics() {
    std::cout << "[TEST] Running TestAtomics...\n";
    memory::VirtualMemory mem;
    NEMU_TEST_ASSERT(mem.Map(0xA000, 0x2000, memory::MemoryPermission::All));

    // Initial memory setup: at 0xA800 put value 0x1000
    mem.Write64(0xA800, 0x1000);

    // 1. LDXR X1, [X0]         (0xC85F7C01): Load exclusive from [X0] into X1
    // 2. STXR W2, X3, [X0]     (0xC8027C03): Store exclusive X3 into [X0], status in W2 (0=ok)
    // 3. CLREX                 (0xD5033F5F): Clear exclusive monitor
    // 4. STXR W4, X3, [X0]     (0xC8047C03): Store exclusive should FAIL (status=1) because monitor cleared
    const u32 code[] = {
        0xC85F7C01,
        0xC8027C03,
        0xD5033F5F,
        0xC8047C03
    };
    NEMU_TEST_ASSERT(mem.WriteBlock(0xA000, code, sizeof(code)));

    cpu::CpuState state;
    state.Reset();
    state.pc = 0xA000;
    state.SetX(0, 0xA800);
    state.SetX(3, 0x9999);

    cpu::Interpreter interp(state, mem);

    // 1. LDXR X1, [X0]
    NEMU_TEST_ASSERT(interp.Step() == cpu::StepResult::Ok);
    NEMU_TEST_ASSERT(state.GetX(1) == 0x1000);
    NEMU_TEST_ASSERT(state.exclusive_active == true);
    NEMU_TEST_ASSERT(state.exclusive_addr == 0xA800);

    // 2. STXR W2, X3, [X0] (should succeed)
    NEMU_TEST_ASSERT(interp.Step() == cpu::StepResult::Ok);
    NEMU_TEST_ASSERT(state.GetX(2) == 0); // status 0 = success
    NEMU_TEST_ASSERT(mem.Read64(0xA800) == 0x9999);
    NEMU_TEST_ASSERT(state.exclusive_active == false);

    // 3. CLREX
    state.exclusive_active = true;
    NEMU_TEST_ASSERT(interp.Step() == cpu::StepResult::Ok);
    NEMU_TEST_ASSERT(state.exclusive_active == false);

    // 4. STXR W4, X3, [X0] (monitor not active -> must fail with status 1)
    NEMU_TEST_ASSERT(interp.Step() == cpu::StepResult::Ok);
    NEMU_TEST_ASSERT(state.GetX(4) == 1); // status 1 = failed

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
    TestFpScalarArithmetic();
    TestFpLoadStore();
    TestNeonVectorOps();
    TestAtomics();

    std::cout << "ALL CPU UNIT TESTS PASSED SUCCESSFULLY!\n";
    return 0;
}
