#pragma once

#include "core/types.hpp"
#include "core/cpu/cpu_state.hpp"
#include "k_process.hpp"
#include "k_thread.hpp"
#include <memory>

namespace nemu::core::kernel::ipc {
class ServiceRegistry;
} // namespace nemu::core::kernel::ipc

namespace nemu::core::kernel {

enum class Result : u32 {
    Success = 0,
    ResultInvalidHandle = 0xE401,
    InvalidAddress = 0xDC01,
    InvalidSize = 0xCA01,
    OutOfMemory = 0xCE01,
    Timeout = 0xEA01,
    Cancelled = 0xEC01,
    Unimplemented = 0xF001,
    PortNotAvailable = 0xEE01,
    NotSupported = 0xF201
};

class SvcDispatcher {
public:
    static void Dispatch(cpu::CpuState& state, KProcess& process, KThread& thread, u32 svc_id);

    /// Inject the shared Horizon HLE service registry used by the IPC SVCs
    /// (svcConnectToPort / svcSendSyncRequest). Must be called before the
    /// guest performs any IPC.
    static void InitializeIpc(std::shared_ptr<ipc::ServiceRegistry> registry);

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
    static void SvcArbitrateLock(cpu::CpuState& state, KProcess& process);
    static void SvcArbitrateUnlock(cpu::CpuState& state, KProcess& process);
    static void SvcWaitProcessWideKeyAtomic(cpu::CpuState& state, KProcess& process);
    static void SvcSignalProcessWideKey(cpu::CpuState& state, KProcess& process);

    // Shared Memory
    static void SvcCreateSharedMemory(cpu::CpuState& state, KProcess& process);
    static void SvcMapSharedMemory(cpu::CpuState& state, KProcess& process);
    static void SvcUnmapSharedMemory(cpu::CpuState& state, KProcess& process);

    // IPC (Horizon HLE)
    static void SvcConnectToPort(cpu::CpuState& state, KProcess& process);
    static void SvcSendSyncRequest(cpu::CpuState& state, KProcess& process, KThread& thread);

    // Diagnostics & Debugging
    static void SvcOutputDebugString(cpu::CpuState& state, KProcess& process);
    static void SvcBreak(cpu::CpuState& state);

    // System Information & Identification
    static void SvcGetInfo(cpu::CpuState& state, KProcess& process);
    static void SvcGetThreadId(cpu::CpuState& state, KThread& thread);
    static void SvcGetProcessId(cpu::CpuState& state, KProcess& process);

    // Event Synchronization
    static void SvcCreateEvent(cpu::CpuState& state, KProcess& process);
    static void SvcSignalEvent(cpu::CpuState& state, KProcess& process);
    static void SvcClearEvent(cpu::CpuState& state, KProcess& process);

    // Handle duplication + processor/memory-info (commercial-game essentials)
    static void SvcDuplicateHandle(cpu::CpuState& state, KProcess& process);
    static void SvcGetCurrentProcessorNumber(cpu::CpuState& state);
    static void SvcQueryProcessMemory(cpu::CpuState& state, KProcess& process);
    static void SvcLockProcessMemory(cpu::CpuState& state, KProcess& process);
    static void SvcUnlockProcessMemory(cpu::CpuState& state, KProcess& process);

    // Canonical-ID additions (switchbrew SVC table, 2-source verified 2026-09-28)
    static void SvcConnectToNamedPort(cpu::CpuState& state, KProcess& process);
    static void SvcSendSyncRequestLight(cpu::CpuState& state, KProcess& process, KThread& thread);
    static void SvcSendSyncRequestWithUserBuffer(cpu::CpuState& state);
    static void SvcGetLastThreadInfo(cpu::CpuState& state);
    static void SvcGetResourceLimitLimitValue(cpu::CpuState& state);
    static void SvcGetResourceLimitCurrentValue(cpu::CpuState& state);
    static void SvcSetThreadActivity(cpu::CpuState& state);
    static void SvcGetThreadContext3(cpu::CpuState& state);
    static void SvcWaitForAddress(cpu::CpuState& state, KProcess& process);
    static void SvcSignalToAddress(cpu::CpuState& state, KProcess& process);
    static void SvcCreateSession(cpu::CpuState& state, KProcess& process);
    static void SvcAcceptSession(cpu::CpuState& state, KProcess& process);
    static void SvcReplyAndReceive(cpu::CpuState& state, KProcess& process, KThread& thread);
    static void SvcControlCodeMemory(cpu::CpuState& state, KProcess& process);
    static void SvcCreateInterruptEvent(cpu::CpuState& state, KProcess& process);
    static void SvcMapTransferMemory(cpu::CpuState& state, KProcess& process);
    static void SvcUnmapTransferMemory(cpu::CpuState& state, KProcess& process);
    static void SvcInvalidateProcessDataCache(cpu::CpuState& state, KProcess& process);
    static void SvcStoreProcessDataCache(cpu::CpuState& state, KProcess& process);
    static void SvcFlushProcessDataCache(cpu::CpuState& state, KProcess& process);
    static void SvcGetProcessList(cpu::CpuState& state, KProcess& process);
    static void SvcGetThreadList(cpu::CpuState& state, KProcess& process);
    static void SvcGetSystemInfo(cpu::CpuState& state);
    static void SvcSetProcessMemoryPermission(cpu::CpuState& state, KProcess& process);
    static void SvcMapProcessCodeMemory(cpu::CpuState& state, KProcess& process);
    static void SvcUnmapProcessCodeMemory(cpu::CpuState& state, KProcess& process);
    static void SvcGetProcessInfo(cpu::CpuState& state, KProcess& process);
    static void SvcTerminateProcess(cpu::CpuState& state, KProcess& process, KThread& thread);
    static void SvcCallSecureMonitor(cpu::CpuState& state);

    // Extended Horizon System & Timing SVCs
    static void SvcSetMemoryAttribute(cpu::CpuState& state);
    static void SvcMapMemory(cpu::CpuState& state);
    static void SvcUnmapMemory(cpu::CpuState& state);
    static void SvcCancelSynchronization(cpu::CpuState& state);
    static void SvcSetThreadCoreMask(cpu::CpuState& state);
    static void SvcGetThreadCoreMask(cpu::CpuState& state);
    static void SvcGetSystemTick(cpu::CpuState& state);

    /// Shared service manager (injected once at boot).
    static std::shared_ptr<ipc::ServiceRegistry> ipc_registry_;
};

} // namespace nemu::core::kernel
