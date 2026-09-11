#include "core/cpu/cpu_state.hpp"
#include "core/cpu/interpreter.hpp"
#include "core/cpu/jit/jit_compiler.hpp"
#include "core/memory/virtual_memory.hpp"
#include <iostream>
#include <vector>
#include <cstring>
#include <cstdlib>

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

int main() {
    std::cout << "[Test: JIT Dynamic Recompiler Differential Validation]" << std::endl;

    memory::VirtualMemory memory;
    const vaddr_t CODE_BASE = 0x0071000000ULL;
    NEMU_TEST_ASSERT(memory.Map(CODE_BASE, 0x10000, memory::MemoryPermission::All), "Map code memory");

    cpu::jit::JitCompiler jit;

    // Test 1: MOVZ + ADD + RET
    {
        const vaddr_t pc1 = CODE_BASE;
        const u32 code1[] = {
            0xD2800540, // MOVZ X0, #42
            0xD2800741, // MOVZ X1, #58
            0x8B010002, // ADD X2, X0, X1
            0xD65F03C0  // RET (lr=X30)
        };
        memory.WriteBlock(pc1, code1, sizeof(code1));

        cpu::CpuState cpu_interp;
        cpu_interp.Reset();
        cpu_interp.pc = pc1;
        cpu_interp.SetX(30, 0xDEADBEEFCAFE0000ULL);

        cpu::CpuState cpu_jit = cpu_interp;

        // Execute via Interpreter until RET
        cpu::Interpreter interp(cpu_interp, memory);
        for (int i = 0; i < 4; ++i) {
            interp.Step();
        }

        // Execute via JIT
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

    // Check JIT compilation stats
    const auto stats = jit.GetStats();
    NEMU_TEST_ASSERT(stats.blocks_compiled == 3, "Blocks compiled must be 3");
    NEMU_TEST_ASSERT(stats.blocks_executed == 3, "Blocks executed must be 3");
    NEMU_TEST_ASSERT(jit.GetCachedBlockCount() == 3, "Cached blocks must be 3");

    std::cout << "[Test: JIT Dynamic Recompiler Differential Validation PASSED]" << std::endl;
    return 0;
}
