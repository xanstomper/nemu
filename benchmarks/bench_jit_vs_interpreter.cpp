#include "bench.hpp"
#include "core/cpu/cpu_state.hpp"
#include "core/cpu/interpreter.hpp"
#include "core/cpu/jit/jit_compiler.hpp"
#include "core/memory/virtual_memory.hpp"
#include <iostream>
#include <vector>

using namespace nemu;
using namespace nemu::core;

int main() {
    std::cout << "=========================================================" << std::endl;
    std::cout << "   NEMU BENCHMARK: AArch64 Interpreter vs JIT Speedup    " << std::endl;
    std::cout << "=========================================================" << std::endl;

    memory::VirtualMemory memory;
    const vaddr_t CODE_BASE = 0x0071000000ULL;
    memory.Map(CODE_BASE, 0x10000, memory::MemoryPermission::All);

    // Assembly loop kernel:
    // 0x00: MOVZ X0, #0        (accumulator = 0)
    // 0x04: MOVZ X1, #1000     (limit)
    // 0x08: MOVZ X2, #0        (counter = 0)
    // Loop header (0x0C):
    // 0x0C: ADD  X0, X0, #3    (acc += 3)
    // 0x10: ADD  X2, X2, #1    (counter += 1)
    // 0x14: CMP  X2, X1        (counter - limit)
    // 0x18: B.NE -0x0C         (branch back to 0x0C if counter != limit)
    // 0x1C: RET
    const u32 kernel_code[] = {
        0xD2800000, // MOVZ X0, #0
        0xD2807D01, // MOVZ X1, #1000
        0xD2800002, // MOVZ X2, #0
        0x91000C00, // ADD X0, X0, #3
        0x91000442, // ADD X2, X2, #1
        0xEB01005F, // CMP X2, X1 -> SUBS XZR, X2, X1
        0x54FFFFA1, // B.NE -0x0C (branch back to 0x0C: ADD X0, X0, #3)
        0xD65F03C0  // RET
    };
    memory.WriteBlock(CODE_BASE, kernel_code, sizeof(kernel_code));

    constexpr u64 RUNS = 2000;
    constexpr u64 INSN_PER_RUN = 3 + 1000 * 4 + 1; // 4004 instructions per run
    constexpr u64 TOTAL_INSTRUCTIONS = RUNS * INSN_PER_RUN; // ~8.0 Million instructions

    // 1. Benchmark Interpreter
    double interp_time_ms = 0.0;
    {
        bench::BenchmarkTimer timer("AArch64 Reference Interpreter", TOTAL_INSTRUCTIONS);
        cpu::CpuState state;
        cpu::Interpreter interp(state, memory);

        for (u64 r = 0; r < RUNS; ++r) {
            state.Reset();
            state.pc = CODE_BASE;
            state.SetX(30, 0xDEAD0000ULL);

            while (state.pc != 0xDEAD0000ULL && !state.halted) {
                interp.Step();
            }
        }
        interp_time_ms = timer.ElapsedMilliseconds();
    }

    // 2. Benchmark JIT Compiler
    double jit_time_ms = 0.0;
    {
        bench::BenchmarkTimer timer("x86-64 Dynamic Recompiler (JIT)", TOTAL_INSTRUCTIONS);
        cpu::jit::JitCompiler jit;
        cpu::CpuState state;

        for (u64 r = 0; r < RUNS; ++r) {
            state.Reset();
            state.pc = CODE_BASE;
            state.SetX(30, 0xDEAD0000ULL);

            while (state.pc != 0xDEAD0000ULL && !state.halted) {
                if (!jit.Execute(state, memory)) {
                    std::cerr << "JIT execution failed at PC 0x" << std::hex << state.pc << std::endl;
                    return 1;
                }
            }
        }
        jit_time_ms = timer.ElapsedMilliseconds();
    }

    const double speedup = interp_time_ms / (jit_time_ms > 0.0 ? jit_time_ms : 0.001);
    std::cout << std::endl;
    std::cout << ">>> JIT vs Interpreter Speedup Factor: " << std::fixed << std::setprecision(2)
              << speedup << "x faster <<<" << std::endl;
    std::cout << "=========================================================" << std::endl;

    return 0;
}
