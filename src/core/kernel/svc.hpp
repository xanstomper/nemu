#pragma once

#include "core/types.hpp"
#include "core/cpu/cpu_state.hpp"
#include "k_process.hpp"
#include "k_thread.hpp"

namespace nemu::core::kernel {

enum class Result : u32 {
    Success = 0,
    ResultInvalidHandle = 0xE401,
    InvalidAddress = 0xDC01,
    InvalidSize = 0xCA01,
    OutOfMemory = 0xCE01,
    Timeout = 0xEA01,
    Cancelled = 0xEC01,
    Unimplemented = 0xF001
};

class SvcDispatcher {
public:
    static void Dispatch(cpu::CpuState& state, KProcess& process, KThread& thread, u32 svc_id);

private:
    // Memory Management
    static void SvcSetHeapSize(cpu::CpuState& state, KProcess& process);
    static void SvcSetMemoryPermission(cpu::CpuState& state, KProcess& process);
    static void SvcQueryMemory(cpu::CpuState& state, KProcess& process);

    // Process & Thread Management
    static void SvcExitProcess(cpu::CpuState& state, KProcess& process, KThread& thread);
    static void SvcCreateThread(cpu::CpuState& state, KProcess& process);
    static void SvcStartThread(cpu::CpuState& state, KProcess& process);
    static void SvcExitThread(cpu::CpuState& state, KThread& thread);
    static void SvcSleepThread(cpu::CpuState& state);

    // Synchronization & Handles
    static void SvcCloseHandle(cpu::CpuState& state, KProcess& process);
    static void SvcResetSignal(cpu::CpuState& state, KProcess& process);
    static void SvcWaitSynchronization(cpu::CpuState& state, KProcess& process);

    // Diagnostics & Debugging
    static void SvcOutputDebugString(cpu::CpuState& state, KProcess& process);
};

} // namespace nemu::core::kernel
