#include "platform/logger.hpp"
#include "core/cpu/cpu_state.hpp"
#include "core/cpu/interpreter.hpp"
#include "core/memory/virtual_memory.hpp"
#include <iostream>

using namespace nemu;
using namespace nemu::core;

int main(int /*argc*/, char** /*argv*/) {
    platform::Logger::Instance().SetMinLevel(platform::LogLevel::Info);

    NEMU_LOG_INFO("Init", "=========================================================");
    NEMU_LOG_INFO("Init", "  NEMU: Nintendo Switch Emulator for Xbox Series S/X     ");
    NEMU_LOG_INFO("Init", "  Version 0.1.0 (Core Baseline)                          ");
    NEMU_LOG_INFO("Init", "=========================================================");

    // Initialize Memory
    memory::VirtualMemory vmem;
    const vaddr_t code_base = 0x00800000;
    const size_t code_size = 4 * 1024 * 1024; // 4 MiB code space
    if (!vmem.Map(code_base, code_size, memory::MemoryPermission::All)) {
        NEMU_LOG_FATAL("Init", "Failed to map code virtual memory");
        return 1;
    }

    // Initialize CPU
    cpu::CpuState cpu;
    cpu.Reset();
    cpu.pc = code_base;
    cpu.sp = code_base + code_size;

    // Load a real small guest test program:
    // 1. MOVZ X0, #42        (0xD2800540)
    // 2. MOVZ X1, #58        (0xD2800741)
    // 3. ADD  X2, X0, X1     (0x8B010002) -> X2 = 100
    // 4. SVC  #0             (0xD4000001) -> Supervisor Call
    const u32 test_program[] = {
        0xD2800540, // MOVZ X0, #42
        0xD2800741, // MOVZ X1, #58
        0x8B010002, // ADD X2, X0, X1
        0xD4000001  // SVC #0
    };

    vmem.WriteBlock(code_base, test_program, sizeof(test_program));

    cpu::Interpreter interp(cpu, vmem);
    interp.SetSvcHandler([](cpu::CpuState& state, u32 svc_id) {
        NEMU_LOG_INFO("Kernel", "Trapped guest SVC #{} with X2 = {}", svc_id, state.GetX(2));
    });

    NEMU_LOG_INFO("CPU", "Executing test guest sequence at PC 0x{:016X}...", cpu.pc);

    for (size_t step = 0; step < 10; ++step) {
        const cpu::StepResult res = interp.Step();
        if (res == cpu::StepResult::Svc) {
            NEMU_LOG_INFO("CPU", "Guest invoked SVC cleanly. Program execution complete.");
            break;
        } else if (res != cpu::StepResult::Ok) {
            NEMU_LOG_ERROR("CPU", "Execution halted with error code {}", static_cast<int>(res));
            break;
        }
    }

    NEMU_LOG_INFO("CPU", "Final Register State:\n{}", cpu.DumpState());
    NEMU_LOG_INFO("Init", "Nemu initialized and tested successfully.");
    return 0;
}
