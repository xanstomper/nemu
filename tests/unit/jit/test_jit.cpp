#include "core/cpu/cpu_state.hpp"
#include "core/cpu/interpreter.hpp"
#include "core/cpu/jit/jit_compiler.hpp"
#include "core/memory/virtual_memory.hpp"
#include <iostream>
#include <vector>
#include <cstring>
#include <cstdlib>
#include <cstdint>

#define NEMU_TEST_ASSERT(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "Assertion failed: " #cond " (" << (msg) << ") at " \
                      << __FILE__ << ":" << __LINE__ << std::endl; \
            std::exit(1); \
        } \
    } while (0)

using namespace nemu;
using namespace nemu::core;

// ---------------------------------------------------------------------------
// AArch64 instruction encoders (verified against aarch64-linux-gnu-as).
// ---------------------------------------------------------------------------
u32 MovzX(u8 rd, u16 imm16, u8 hw = 0) { return 0xD2800000u | (hw << 21) | (imm16 << 5) | rd; }
u32 MovkX(u8 rd, u16 imm16, u8 hw) { return 0xF2800000u | (hw << 21) | (imm16 << 5) | rd; }

// Load a full 64-bit immediate using MOVZ + MOVK sequences (1..4 instructions).
std::vector<u32> LoadX(u8 rd, u64 val) {
    std::vector<u32> out;
    bool first = true;
    for (int hw = 0; hw < 4; ++hw) {
        const u16 chunk = static_cast<u16>((val >> (16 * hw)) & 0xFFFF);
        const bool all_upper_zero = ((val >> (16 * (hw + 1))) == 0);
        if (first) {
            out.push_back(MovzX(rd, chunk, static_cast<u8>(hw)));
            first = false;
        } else if (chunk != 0) {
            out.push_back(MovkX(rd, chunk, static_cast<u8>(hw)));
        } else if (all_upper_zero) {
            break; // remaining chunks are zero; nothing more to insert
        }
    }
    if (out.empty()) out.push_back(MovzX(rd, 0));
    return out;
}

u32 AddXi(u8 rd, u8 rn, u16 imm12) { return 0x91000000u | (imm12 << 10) | (rn << 5) | rd; }
u32 CmpXX(u8 rn, u8 rm) { return 0xEB000000u | (rm << 16) | (rn << 5) | 31; }
u32 CmpXi(u8 rn, u16 imm12) { return 0xF1000000u | (imm12 << 10) | (rn << 5) | 31; }
u32 Bcond(u8 cond, s64 delta_bytes) { return 0x54000000u | ((static_cast<u32>(delta_bytes / 4) & 0x7FFFFu) << 5) | cond; }
u32 Blx(u64 from_pc, u64 to_pc) { return 0x94000000u | (static_cast<u32>((to_pc - from_pc) / 4) & 0x3FFFFFFu); }
u32 BlrXn(u8 rn) { return 0xD63F0000u | (rn << 5); }
u32 RetXn(u8 rn) { return 0xD65F0000u | (rn << 5); }
u32 LdrX(u8 rt, u8 rn, u16 imm12) { return 0xF9400000u | (imm12 << 10) | (rn << 5) | rt; }
u32 LdrW(u8 rt, u8 rn, u16 imm12) { return 0xB9400000u | (imm12 << 10) | (rn << 5) | rt; }
u32 StrX(u8 rt, u8 rn, u16 imm12) { return 0xF9000000u | (imm12 << 10) | (rn << 5) | rt; }
u32 StrW(u8 rt, u8 rn, u16 imm12) { return 0xB9000000u | (imm12 << 10) | (rn << 5) | rt; }
u32 CselX(u8 rd, u8 rn, u8 rm, u8 cond) { return 0x9A800000u | (cond << 12) | (rm << 16) | (rn << 5) | rd; }
u32 CselW(u8 rd, u8 rn, u8 rm, u8 cond) { return 0x1A800000u | (cond << 12) | (rm << 16) | (rn << 5) | rd; }
constexpr u32 NOP = 0xD503201Fu;

void AssertCpuStatesMatch(const cpu::CpuState& interp, const cpu::CpuState& jit, const char* test_name) {
    for (u32 i = 0; i < 31; ++i) {
        if (interp.GetX(i) != jit.GetX(i)) {
            std::cerr << "[" << test_name << "] Register mismatch at X" << i
                      << ": Interp=0x" << std::hex << interp.GetX(i)
                      << ", JIT=0x" << jit.GetX(i) << std::dec << std::endl;
            std::exit(1);
        }
    }
    NEMU_TEST_ASSERT(interp.pc == jit.pc, "PC mismatch between Interpreter and JIT");
    NEMU_TEST_ASSERT(interp.sp == jit.sp, "SP mismatch between Interpreter and JIT");
}

// Full differential including the NZCV condition flags.
void AssertCpuStatesMatchFull(const cpu::CpuState& interp, const cpu::CpuState& jit, const char* test_name) {
    AssertCpuStatesMatch(interp, jit, test_name);
    const char* t = test_name;
    NEMU_TEST_ASSERT(interp.pstate.n == jit.pstate.n, std::string(t) + ": N flag mismatch");
    NEMU_TEST_ASSERT(interp.pstate.z == jit.pstate.z, std::string(t) + ": Z flag mismatch");
    NEMU_TEST_ASSERT(interp.pstate.c == jit.pstate.c, std::string(t) + ": C flag mismatch");
    NEMU_TEST_ASSERT(interp.pstate.v == jit.pstate.v, std::string(t) + ": V flag mismatch");
}

// Run the JIT one basic block at a time until the guest PC equals end_pc.
void RunJitTo(cpu::jit::JitCompiler& jit, cpu::CpuState& state, memory::VirtualMemory& memory,
              vaddr_t end_pc, const char* test_name) {
    int guard = 0;
    while (state.pc != end_pc && guard++ < 64) {
        const bool ok = jit.Execute(state, memory);
        NEMU_TEST_ASSERT(ok, std::string(test_name) + ": JIT Execute failed");
    }
    NEMU_TEST_ASSERT(state.pc == end_pc, std::string(test_name) + ": JIT did not reach end_pc");
}

// Run the interpreter instruction-by-instruction until the guest PC equals end_pc.
// `state` is the CpuState bound to the interpreter (tracked via the by-ref state).
void RunInterpTo(cpu::Interpreter& interp, const cpu::CpuState& state, vaddr_t end_pc, const char* test_name) {
    int guard = 0;
    while (state.pc != end_pc && guard++ < 128) {
        const cpu::StepResult r = interp.Step();
        NEMU_TEST_ASSERT(r == cpu::StepResult::Ok, std::string(test_name) + ": Interpreter Step failed");
    }
    NEMU_TEST_ASSERT(state.pc == end_pc, std::string(test_name) + ": Interpreter did not reach end_pc");
}

int main() {
    std::cout << "[Test: JIT Dynamic Recompiler Differential Validation]" << std::endl;

    memory::VirtualMemory memory;
    const vaddr_t CODE_BASE = 0x0071000000ULL;
    NEMU_TEST_ASSERT(memory.Map(CODE_BASE, 0x40000, memory::MemoryPermission::All), "Map code memory");

    cpu::jit::JitCompiler jit;

    // Test 1: MOVZ + ADD + RET
    {
        const vaddr_t pc1 = CODE_BASE;
        // MOVZ X0,#42, MOVZ X1,#58, ADD X2, X0, X1 (register add), RET
        const u32 code1[] = { MovzX(0, 42), MovzX(1, 58), 0x8B010002, 0xD65F03C0 };
        memory.WriteBlock(pc1, code1, sizeof(code1));

        cpu::CpuState cpu_interp;
        cpu_interp.Reset();
        cpu_interp.pc = pc1;
        cpu_interp.SetX(30, 0xDEADBEEFCAFE0000ULL);

        cpu::CpuState cpu_jit = cpu_interp;

        cpu::Interpreter interp(cpu_interp, memory);
        for (int i = 0; i < 4; ++i) {
            interp.Step();
        }

        bool jit_res = jit.Execute(cpu_jit, memory);
        NEMU_TEST_ASSERT(jit_res, "JIT block execution");

        AssertCpuStatesMatch(cpu_interp, cpu_jit, "Test 1: MOVZ + ADD + RET");
        NEMU_TEST_ASSERT(cpu_jit.GetX(2) == 100, "X2 must be 100");
        std::cout << "  - Differential Test 1 (MOVZ + ADD + RET): PASSED" << std::endl;
    }

    // Test 2: MOVZ + SUB + RET
    {
        const vaddr_t pc2 = CODE_BASE + 0x100;
        const u32 code2[] = {
            0xD2800C83, // MOVZ X3, #100
            0xD2800324, // MOVZ X4, #25
            0xCB040065, // SUB X5, X3, X4 (100 - 25 = 75)
            0xD65F03C0  // RET
        };
        memory.WriteBlock(pc2, code2, sizeof(code2));

        cpu::CpuState cpu_interp;
        cpu_interp.Reset();
        cpu_interp.pc = pc2;
        cpu_interp.SetX(30, 0x0071000000ULL);

        cpu::CpuState cpu_jit = cpu_interp;

        cpu::Interpreter interp(cpu_interp, memory);
        for (int i = 0; i < 4; ++i) {
            interp.Step();
        }

        bool jit_res = jit.Execute(cpu_jit, memory);
        NEMU_TEST_ASSERT(jit_res, "JIT block 2 execution");

        AssertCpuStatesMatch(cpu_interp, cpu_jit, "Test 2: MOVZ + SUB + RET");
        NEMU_TEST_ASSERT(cpu_jit.GetX(5) == 75, "X5 must be 75");
        std::cout << "  - Differential Test 2 (MOVZ + SUB + RET): PASSED" << std::endl;
    }

    // Test 3: Bitwise Logic (AND, ORR, EOR)
    {
        const vaddr_t pc3 = CODE_BASE + 0x200;
        const u32 code3[] = {
            0xD2800146, // MOVZ X6, #10 (0b1010)
            0xD28000E7, // MOVZ X7, #7  (0b0111)
            0x8A0700C8, // AND X8, X6, X7  (0b0010 = 2)
            0xAA0700C9, // ORR X9, X6, X7  (0b1111 = 15)
            0xCA0700CA, // EOR X10, X6, X7 (0b1101 = 13)
            0xD65F03C0  // RET
        };
        memory.WriteBlock(pc3, code3, sizeof(code3));

        cpu::CpuState cpu_interp;
        cpu_interp.Reset();
        cpu_interp.pc = pc3;
        cpu_interp.SetX(30, 0x0071000000ULL);

        cpu::CpuState cpu_jit = cpu_interp;

        cpu::Interpreter interp(cpu_interp, memory);
        for (int i = 0; i < 6; ++i) {
            interp.Step();
        }

        bool jit_res = jit.Execute(cpu_jit, memory);
        NEMU_TEST_ASSERT(jit_res, "JIT block 3 execution");

        AssertCpuStatesMatch(cpu_interp, cpu_jit, "Test 3: Bitwise Logic");
        NEMU_TEST_ASSERT(cpu_jit.GetX(8) == 2, "X8 AND result");
        NEMU_TEST_ASSERT(cpu_jit.GetX(9) == 15, "X9 ORR result");
        NEMU_TEST_ASSERT(cpu_jit.GetX(10) == 13, "X10 EOR result");
        std::cout << "  - Differential Test 3 (Bitwise Logic): PASSED" << std::endl;
    }

    // Test 4: BL (write LR = PC+4, branch) + RET (return through LR)
    {
        const vaddr_t pm = CODE_BASE + 0x300; // main
        const vaddr_t ps = CODE_BASE + 0x380; // sub
        const vaddr_t HALT = CODE_BASE + 0x39C; // sentinel the program RETs to

        // main: X28 = HALT (saved LR), X3 = 0, then BL sub.
        std::vector<u32> main_code = LoadX(28, HALT);
        main_code.push_back(MovzX(3, 0));
        const u64 bl_addr = pm + main_code.size() * 4; // address of the BL
        main_code.push_back(Blx(bl_addr, ps));         // X30 = bl_addr+4 = continuation
        memory.WriteBlock(pm, main_code.data(), main_code.size() * 4);

        const u64 continuation = bl_addr + 4; // return address written to LR by BL
        const u32 cont_code[] = { RetXn(28) }; // wait for the saved LR in X28
        memory.WriteBlock(continuation, cont_code, sizeof(cont_code));

        const u32 sub_code[] = {
            AddXi(3, 3, 7), // X3 = 7
            RetXn(30)       // return to X30 (== continuation)
        };
        memory.WriteBlock(ps, sub_code, sizeof(sub_code));

        cpu::CpuState cpu_interp;
        cpu_interp.Reset();
        cpu_interp.pc = pm;
        cpu_interp.SetX(30, 0xDEADBEEF00000000ULL);

        cpu::CpuState cpu_jit = cpu_interp;

        cpu::Interpreter interp(cpu_interp, memory);
        RunInterpTo(interp, cpu_interp, HALT, "Test 4 BL/RET interpreter");
        RunJitTo(jit, cpu_jit, memory, HALT, "Test 4 BL/RET JIT");

        AssertCpuStatesMatchFull(cpu_interp, cpu_jit, "Test 4 BL+RET");
        NEMU_TEST_ASSERT(cpu_jit.GetX(3) == 7, "X3 must be 7");
        NEMU_TEST_ASSERT(cpu_jit.GetX(28) == HALT, "X28 must hold the saved return sentinel");
        NEMU_TEST_ASSERT(cpu_jit.GetX(30) == continuation, "LR must hold the BL return address");
        std::cout << "  - Differential Test 4 (BL + RET): PASSED" << std::endl;
    }

    // Test 5: BLR (indirect call through X0) + RET
    {
        const vaddr_t pm = CODE_BASE + 0x400; // main
        const vaddr_t ps = CODE_BASE + 0x480; // sub
        const vaddr_t HALT = CODE_BASE + 0x49C; // sentinel the program RETs to

        // main: X28 = HALT (saved LR), X0 = ps, then BLR X0.
        std::vector<u32> main_code = LoadX(28, HALT);
        std::vector<u32> x0 = LoadX(0, ps);
        main_code.insert(main_code.end(), x0.begin(), x0.end());
        const u64 blr_addr = pm + main_code.size() * 4; // address of the BLR
        main_code.push_back(BlrXn(0));                  // X30 = blr_addr+4, PC = X0
        memory.WriteBlock(pm, main_code.data(), main_code.size() * 4);

        const u64 continuation = blr_addr + 4; // return address written to LR by BLR
        const u32 cont_code[] = { RetXn(28) };
        memory.WriteBlock(continuation, cont_code, sizeof(cont_code));

        const u32 sub_code[] = {
            AddXi(3, 3, 11), // X3 = 11
            RetXn(30)        // return to X30 (== continuation)
        };
        memory.WriteBlock(ps, sub_code, sizeof(sub_code));

        cpu::CpuState cpu_interp;
        cpu_interp.Reset();
        cpu_interp.pc = pm;
        cpu_interp.SetX(30, 0xDEADBEEF00000000ULL);

        cpu::CpuState cpu_jit = cpu_interp;

        cpu::Interpreter interp(cpu_interp, memory);
        RunInterpTo(interp, cpu_interp, HALT, "Test 5 BLR/RET interpreter");
        RunJitTo(jit, cpu_jit, memory, HALT, "Test 5 BLR/RET JIT");

        AssertCpuStatesMatchFull(cpu_interp, cpu_jit, "Test 5 BLR+RET");
        NEMU_TEST_ASSERT(cpu_jit.GetX(3) == 11, "X3 must be 11");
        NEMU_TEST_ASSERT(cpu_jit.GetX(0) == ps, "X0 (BLR target) preserved");
        NEMU_TEST_ASSERT(cpu_jit.GetX(30) == continuation, "LR must hold the BLR return address");
        std::cout << "  - Differential Test 5 (BLR + RET): PASSED" << std::endl;
    }

    // Test 6: LDR / STR (64-bit and 32-bit) through VirtualMemory
    {
        const vaddr_t pm = CODE_BASE + 0x500;
        const vaddr_t pd = CODE_BASE + 0x580;
        const vaddr_t HALT = CODE_BASE + 0x590;

        std::vector<u32> main_code = LoadX(5, pd);
        main_code.push_back(LdrX(3, 5, 0));        // X3 = [pd+0]
        main_code.push_back(LdrX(4, 5, 1));        // X4 = [pd+8]
        main_code.push_back(LdrW(6, 5, 0));        // W6 = (u32)[pd+0]
        main_code.push_back(StrX(4, 5, 8));        // [pd+64] = X4
        main_code.push_back(StrW(6, 5, 18));       // [pd+72] = W6
        main_code.push_back(StrX(31, 5, 10));      // [pd+80] = 0  (XZR)
        main_code.push_back(RetXn(30));
        memory.WriteBlock(pm, main_code.data(), main_code.size() * 4);

        const u64 v64_0 = 0x1122334455667788ULL;
        const u64 v64_1 = 0xDEADBEEFCAFEBABEULL;

        // Both the interpreter and JIT must observe the SAME initial data, so
        // (re)seed the backing pages before each run.
        auto seed = [&] {
            memory.Write64(pd + 0, v64_0);
            memory.Write64(pd + 8, v64_1);
        };

        cpu::CpuState cpu_interp;
        cpu_interp.Reset();
        cpu_interp.pc = pm;
        cpu_interp.SetX(30, HALT);
        seed();
        cpu::CpuState cpu_jit = cpu_interp;
        seed();

        cpu::Interpreter interp(cpu_interp, memory);
        RunInterpTo(interp, cpu_interp, HALT, "Test 6 LDR/STR interpreter");
        RunJitTo(jit, cpu_jit, memory, HALT, "Test 6 LDR/STR JIT");

        AssertCpuStatesMatchFull(cpu_interp, cpu_jit, "Test 6 LDR/STR");
        NEMU_TEST_ASSERT(cpu_jit.GetX(3) == v64_0, "X3 = 64-bit loaded value");
        NEMU_TEST_ASSERT(cpu_jit.GetX(4) == v64_1, "X4 = 64-bit loaded value");
        NEMU_TEST_ASSERT(cpu_jit.GetX(6) == static_cast<u32>(v64_0), "W6 = 32-bit loaded (zero-extended)");

        // Verify STR actually wrote through VirtualMemory (check the backing pages).
        NEMU_TEST_ASSERT(memory.Read64(pd + 64) == v64_1, "STR X4 wrote [pd+64]");
        NEMU_TEST_ASSERT(memory.Read32(pd + 72) == static_cast<u32>(v64_0), "STR W6 wrote [pd+72]");
        NEMU_TEST_ASSERT(memory.Read64(pd + 80) == 0, "STR XZR wrote 0 to [pd+80]");
        std::cout << "  - Differential Test 6 (LDR/STR through VirtualMemory): PASSED" << std::endl;
    }

    // Test 7: CMP sets NZCV + CSEL conditional select (64-bit and 32-bit)
    {
        const vaddr_t pm = CODE_BASE + 0x600;
        const vaddr_t HALT = CODE_BASE + 0x66c;

        std::vector<u32> code;
        auto load = [&](u8 rd, u64 v) {
            std::vector<u32> seq = LoadX(rd, v);
            code.insert(code.end(), seq.begin(), seq.end());
        };
        load(1, 5);   // X1 = 5
        load(2, 100); // X2 = 100
        load(7, 50);  // X7 = 50
        load(8, 60);  // X8 = 60
        code.push_back(CmpXX(7, 8));   // 50 - 60 -> N=1, Z=0, C=0, V=0
        code.push_back(CselX(3, 1, 2, static_cast<u8>(cpu::Condition::GE))); // GE false -> X3=X2=100
        code.push_back(CselX(4, 1, 2, static_cast<u8>(cpu::Condition::LT))); // LT true  -> X4=X1=5
        code.push_back(CselX(0, 1, 2, static_cast<u8>(cpu::Condition::AL))); // always   -> X0=X1=5
        code.push_back(CselX(5, 1, 2, static_cast<u8>(cpu::Condition::CC))); // CC true  -> X5=X1=5
        code.push_back(CselW(9, 1, 2, static_cast<u8>(cpu::Condition::EQ))); // EQ false -> W9=X2=100 (32-bit)
        code.push_back(CselW(10, 1, 2, static_cast<u8>(cpu::Condition::NE))); // NE true -> W10=X1=5
        code.push_back(RetXn(30));
        memory.WriteBlock(pm, code.data(), code.size() * 4);

        cpu::CpuState cpu_interp;
        cpu_interp.Reset();
        cpu_interp.pc = pm;
        cpu_interp.SetX(30, HALT);

        cpu::CpuState cpu_jit = cpu_interp;

        cpu::Interpreter interp(cpu_interp, memory);
        RunInterpTo(interp, cpu_interp, HALT, "Test 7 CSEL interpreter");
        RunJitTo(jit, cpu_jit, memory, HALT, "Test 7 CSEL JIT");

        AssertCpuStatesMatchFull(cpu_interp, cpu_jit, "Test 7 CMP+CSEL");
        NEMU_TEST_ASSERT(cpu_jit.GetX(3) == 100, "CSEL GE (false) -> X2");
        NEMU_TEST_ASSERT(cpu_jit.GetX(4) == 5, "CSEL LT (true) -> X1");
        NEMU_TEST_ASSERT(cpu_jit.GetX(0) == 5, "CSEL AL -> X1");
        NEMU_TEST_ASSERT(cpu_jit.GetX(5) == 5, "CSEL CC (true) -> X1");
        NEMU_TEST_ASSERT(cpu_jit.GetX(9) == 100, "CSEL W EQ (false) -> W2 zero-extended");
        NEMU_TEST_ASSERT(cpu_jit.GetX(10) == 5, "CSEL W NE (true) -> W1");
        std::cout << "  - Differential Test 7 (CMP + CSEL): PASSED" << std::endl;
    }

    // Test 8: CMP_imm sets flags (CMP Xn, #imm)
    {
        const vaddr_t pm = CODE_BASE + 0x700;
        const vaddr_t HALT = CODE_BASE + 0x728;
        std::vector<u32> code = LoadX(1, 42);
        code.push_back(CmpXi(1, 42)); // 42-42=0 -> Z=1, N=0, C=1, V=0
        code.push_back(MovzX(9, 0xAAAA));
        code.push_back(RetXn(30));
        memory.WriteBlock(pm, code.data(), code.size() * 4);

        cpu::CpuState cpu_interp;
        cpu_interp.Reset();
        cpu_interp.pc = pm;
        cpu_interp.SetX(30, HALT);
        cpu::CpuState cpu_jit = cpu_interp;

        cpu::Interpreter interp(cpu_interp, memory);
        RunInterpTo(interp, cpu_interp, HALT, "Test 8 CMP_imm interpreter");
        RunJitTo(jit, cpu_jit, memory, HALT, "Test 8 CMP_imm JIT");

        AssertCpuStatesMatchFull(cpu_interp, cpu_jit, "Test 8 CMP_imm");
        NEMU_TEST_ASSERT(cpu_jit.pstate.z == true, "CMP_eq Z flag");
        NEMU_TEST_ASSERT(cpu_jit.pstate.c == true, "CMP_eq C flag");
        NEMU_TEST_ASSERT(cpu_jit.pstate.n == false, "CMP_eq N flag");
        std::cout << "  - Differential Test 8 (CMP immediate flags): PASSED" << std::endl;
    }

    // Test 9: Conditional branches (B.cond) against CMP-produced flags.
    {
        struct Vec { const char* name; cpu::Condition cond; u64 a; u64 b; bool expect_taken; };
        // a,b are compared as CMP X1, X2 (a - b).
        const Vec cases[] = {
            {"EQ",  cpu::Condition::EQ, 42, 42, true},
            {"NE",  cpu::Condition::NE, 42, 17, true},
            {"CS",  cpu::Condition::CS, 10, 5,  true},   // 10 >= 5 unsigned
            {"CC",  cpu::Condition::CC, 5,  10, true},   // 5 < 10 unsigned
            {"MI",  cpu::Condition::MI, 0xFFFFFFFFFFFFFFFBULL, 0,  true}, // -5 -> negative
            {"PL",  cpu::Condition::PL, 5,  3,  true},   // non-negative
            {"HI",  cpu::Condition::HI, 100, 10, true},  // 100 > 10
            {"LS",  cpu::Condition::LS, 10, 100, true},  // 10 <= 100
            {"GE",  cpu::Condition::GE, 5,  3,  true},   // positive, N==V(0==0)
            {"LT",  cpu::Condition::LT, 5,  10, true},   // N set, N!=V
            {"GT",  cpu::Condition::GT, 42, 17, true},   // 42>17
            {"LE",  cpu::Condition::LE, 42, 42, true},   // equal
            {"VS",  cpu::Condition::VS, 0x8000000000000000ULL, 1, true}, // INT64_MIN - 1 overflows
            {"VC",  cpu::Condition::VC, 5,  3,  true},   // no overflow
            {"AL",  cpu::Condition::AL, 1,  1,  true},   // always
        };

        vaddr_t base = CODE_BASE + 0x800;
        const vaddr_t HALT = CODE_BASE + 0x7000000ULL; // unreachable sentinel
        for (const Vec& c : cases) {
            const vaddr_t pm = base;
            const vaddr_t H = base + 0xF00ULL; // per-test HALT sentinel (distinct)
            std::vector<u32> code;
            auto load = [&](u8 rd, u64 v) {
                std::vector<u32> seq = LoadX(rd, v);
                code.insert(code.end(), seq.begin(), seq.end());
            };
            load(1, c.a);
            load(2, c.b);
            code.push_back(CmpXX(1, 2));
            const int bcond_idx = static_cast<int>(code.size());
            code.push_back(0); // B.cond placeholder
            code.push_back(MovzX(9, 0x1111)); // fall-through marker
            code.push_back(RetXn(30));
            const int taken_idx = static_cast<int>(code.size());
            code.push_back(MovzX(10, 0x2222)); // taken marker
            code.push_back(RetXn(30));

            const u64 bcond_addr = pm + static_cast<u64>(bcond_idx) * 4;
            const u64 taken_addr = pm + static_cast<u64>(taken_idx) * 4;
            code[static_cast<size_t>(bcond_idx)] = Bcond(static_cast<u8>(c.cond),
                                                         static_cast<u32>(taken_addr - bcond_addr));
            memory.WriteBlock(pm, code.data(), code.size() * 4);

            cpu::CpuState cpu_interp;
            cpu_interp.Reset();
            cpu_interp.pc = pm;
            cpu_interp.sp = base; // distinguish marker paths via registers
            cpu_interp.SetX(30, H);
            cpu::CpuState cpu_jit = cpu_interp;

            cpu::Interpreter interp(cpu_interp, memory);
            RunInterpTo(interp, cpu_interp, H, c.name);
            RunJitTo(jit, cpu_jit, memory, H, c.name);

            AssertCpuStatesMatchFull(cpu_interp, cpu_jit, c.name);
            // Confirm the branch actually took the expected arm (redundant differential).
            if (c.expect_taken) {
                NEMU_TEST_ASSERT(cpu_jit.GetX(10) == 0x2222, std::string(c.name) + ": expected taken arm");
            } else {
                NEMU_TEST_ASSERT(cpu_jit.GetX(9) == 0x1111, std::string(c.name) + ": expected fall-through arm");
            }
            std::cout << "  - Differential Test 9 (B.cond " << c.name << "): PASSED" << std::endl;
            base += 0x20;
        }
        (void)HALT;
    }

    const auto stats = jit.GetStats();
    NEMU_TEST_ASSERT(stats.blocks_compiled > 0, "Blocks compiled must be > 0");
    NEMU_TEST_ASSERT(stats.blocks_executed > 0, "Blocks executed must be > 0");
    NEMU_TEST_ASSERT(stats.instructions_recompiled > 0, "Instructions recompiled must be > 0");
    NEMU_TEST_ASSERT(jit.GetCachedBlockCount() > 0, "Cached blocks must be > 0");

    std::cout << "[Test: JIT Dynamic Recompiler Differential Validation PASSED]" << std::endl;
    return 0;
}