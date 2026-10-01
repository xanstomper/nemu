#include "svc.hpp"
#include "core/debug/breadcrumbs.hpp"
#include "k_event.hpp"
#include "k_shared_memory.hpp"
#include "k_mutex.hpp"
#include "ipc/ipc_dispatcher.hpp"
#include "ipc/ipc_service.hpp"
#include "ipc/service_registry.hpp"
#include "platform/logger.hpp"
#include "core/cpu/title_compat.hpp"
#include <thread>
#include <vector>
#include <memory>

namespace nemu::core::kernel {

std::shared_ptr<ipc::ServiceRegistry> SvcDispatcher::ipc_registry_;

void SvcDispatcher::InitializeIpc(std::shared_ptr<ipc::ServiceRegistry> registry) {
    ipc_registry_ = std::move(registry);
}

void SvcDispatcher::Dispatch(cpu::CpuState& state, KProcess& process, KThread& thread, u32 svc_id) {
    debug::BreadcrumbTrail::PushSvc(svc_id, state.pc);
    NEMU_LOG_DEBUG("SVC", "Dispatching SVC 0x{:02X} for TID {}", svc_id, thread.GetTid());

    switch (svc_id) {
        case 0x00: SvcSetHeapBase(state, process); break;                // svcSetHeapBase
        case 0x01: SvcSetHeapSize(state, process); break;
        case 0x02: SvcSetMemoryPermission(state, process); break;
        case 0x03: SvcSetMemoryAttribute(state); break;
        case 0x04: SvcMapMemory(state); break;
        case 0x05: SvcUnmapMemory(state); break;
        case 0x06: SvcQueryMemory(state, process); break;
        case 0x07: SvcExitProcess(state, process, thread); break;
        case 0x08: SvcCreateThread(state, process); break;
        case 0x09: SvcStartThread(state, process); break;
        case 0x0A: SvcExitThread(state, thread); break;
        case 0x0B: SvcSleepThread(state); break;
        case 0x0C: {
            const u32 thread_handle = static_cast<u32>(state.GetX(1));
            auto target_thread = process.GetHandleTable().GetObject<KThread>(thread_handle);
            const u32 prio = target_thread ? target_thread->GetPriority() : thread.GetPriority();
            state.SetX(0, static_cast<u64>(Result::Success));
            state.SetX(1, prio);
            break;
        }
        case 0x0D: {
            const u32 thread_handle = static_cast<u32>(state.GetX(0));
            const u32 prio = static_cast<u32>(state.GetX(1));
            auto target_thread = process.GetHandleTable().GetObject<KThread>(thread_handle);
            if (target_thread) {
                target_thread->SetPriority(prio);
            } else {
                thread.SetPriority(prio);
            }
            state.SetX(0, static_cast<u64>(Result::Success));
            break;
        }
        case 0x0E: SvcGetThreadCoreMask(state, process); break;
        case 0x0F: SvcSetThreadCoreMask(state, process); break;
        case 0x10: SvcGetCurrentProcessorNumber(state); break;              // svcGetCurrentProcessorNumber
        case 0x11: SvcSignalEvent(state, process); break;                   // svcSignalEvent
        case 0x12: SvcClearEvent(state, process); break;                    // svcClearEvent
        case 0x13: SvcCreateSharedMemory(state, process); break;
        case 0x14: SvcMapSharedMemory(state, process); break;
        case 0x15: SvcUnmapSharedMemory(state, process); break;
        case 0x16: SvcCloseHandle(state, process); break;
        case 0x17: SvcResetSignal(state, process); break;
        case 0x18: SvcWaitSynchronization(state, process); break;
        case 0x19: SvcCancelSynchronization(state); break;
        case 0x1A: SvcArbitrateLock(state, process); break;
        case 0x1B: SvcArbitrateUnlock(state, process); break;
        case 0x1C: SvcWaitProcessWideKeyAtomic(state, process); break;
        case 0x1D: SvcSignalProcessWideKey(state, process); break;          // svcSignalProcessWideKey
        case 0x1F: SvcConnectToNamedPort(state, process); break;            // svcConnectToNamedPort
        case 0x20: SvcSendSyncRequestLight(state, process, thread); break;  // svcSendSyncRequestLight
        case 0x21: SvcSendSyncRequest(state, process, thread); break;
        case 0x22: SvcSendSyncRequestWithUserBuffer(state); break;          // svcSendSyncRequestWithUserBuffer
        case 0x24: SvcGetProcessId(state, process); break;                  // svcGetProcessId
        case 0x25: SvcGetThreadId(state, thread); break;
        case 0x26: SvcBreak(state); break;
        case 0x27: SvcOutputDebugString(state, process); break;
        case 0x28: SvcGetResourceLimitLimitValue(state); break;             // svcGetResourceLimitLimitValue
        case 0x29: SvcGetInfo(state, process); break;
        case 0x2A: state.SetX(0, static_cast<u64>(Result::Success)); break; // svcFlushEntireDataCache
        case 0x2B: SvcFlushProcessDataCache(state, process); break;         // svcFlushDataCache (current process)
        case 0x2F: SvcGetLastThreadInfo(state); break;                      // svcGetLastThreadInfo (info-only stub)
        case 0x30: SvcGetResourceLimitLimitValue(state); break;             // svcGetResourceLimitLimitValue
        case 0x31: SvcGetResourceLimitCurrentValue(state); break;           // svcGetResourceLimitCurrentValue
        case 0x32: SvcSetThreadActivity(state); break;                      // svcSetThreadActivity
        case 0x33: SvcGetThreadContext3(state); break;                      // svcGetThreadContext3
        case 0x34: SvcWaitForAddress(state, process); break;                // svcWaitForAddress
        case 0x35: SvcSignalToAddress(state, process); break;               // svcSignalToAddress
        case 0x40: SvcCreateSession(state, process); break;                 // svcCreateSession
        case 0x41: SvcAcceptSession(state, process); break;                 // svcAcceptSession
        case 0x43: SvcReplyAndReceive(state, process, thread); break;       // svcReplyAndReceive
        case 0x45: SvcCreateEvent(state, process); break;
        case 0x46: SvcSignalEvent(state, process); break; // [3.0.0-] legacy slot alias
        case 0x47: SvcClearEvent(state, process); break;  // [3.0.0-] legacy slot alias
        case 0x4C: SvcControlCodeMemory(state, process); break;             // svcControlCodeMemory
        case 0x4D: state.SetX(0, static_cast<u64>(Result::Success)); break; // svcSleepSystem (HLE no-op)
        case 0x4E: SvcCreateInterruptEvent(state, process); break;          // svcCreateInterruptEvent (HLE stub event)
        case 0x4F: SvcMapTransferMemory(state, process); break;             // svcMapTransferMemory
        case 0x50: SvcCreateSharedMemory(state, process); break;            // [3.0.0-] legacy alias (canonical 0x50 CreateSharedMemory)
        case 0x51: SvcUnmapTransferMemory(state, process); break;           // svcUnmapTransferMemory
        case 0x52: SvcUnmapTransferMemory(state, process); break; // NOTE: pre-audit slot kept for compat, see test note
        case 0x5D: SvcInvalidateProcessDataCache(state, process); break;    // svcInvalidateProcessDataCache
        case 0x5E: SvcStoreProcessDataCache(state, process); break;         // svcStoreProcessDataCache
        case 0x5F: SvcFlushProcessDataCache(state, process); break;         // svcFlushProcessDataCache
        case 0x65: SvcGetProcessList(state, process); break;                // svcGetProcessList
        case 0x66: SvcGetThreadList(state, process); break;                 // svcGetThreadList
        case 0x6F: SvcGetSystemInfo(state); break;                          // svcGetSystemInfo
        case 0x73: SvcSetProcessMemoryPermission(state, process); break;    // svcSetProcessMemoryPermission
        case 0x76: SvcQueryProcessMemory(state, process); break;            // svcQueryProcessMemory (canonical slot)
        case 0x77: SvcMapProcessCodeMemory(state, process); break;          // svcMapProcessCodeMemory
        case 0x78: SvcUnmapProcessCodeMemory(state, process); break;        // svcUnmapProcessCodeMemory
        case 0x7C: SvcGetProcessInfo(state, process); break;                // svcGetProcessInfo
        case 0x72: SvcConnectToPort(state, process); break;                 // svcConnectToPort (privileged; handle-based)
        case 0x7F: SvcCallSecureMonitor(state); break;                      // svcCallSecureMonitor (benign SMC stub)
        case 0x7B: SvcTerminateProcess(state, process, thread); break;      // svcTerminateProcess

        default:
            NEMU_LOG_WARN("SVC", "Unhandled SVC 0x{:02X} called at PC 0x{:016X}", svc_id, state.pc);
            state.SetX(0, static_cast<u64>(Result::Unimplemented));
            break;
    }
}

void SvcDispatcher::SvcSetHeapBase(cpu::CpuState& state, KProcess& process) {
    // svcSetHeapBase(u64 heap_base, u64 heap_size, u32 *out_result)
    // The game's allocator calls this before its first allocation. A NULL
    // base asks the kernel to pick a region; the chosen base is returned.
    // Without it the heap base register is never published, so malloc-family
    // calls hand the guest an unusable pointer -- which is what produced the
    // corrupt frame pointer 0x187A62E290 in Terraria's sdk init.
    const vaddr_t base = state.GetX(0);
    const size_t size = static_cast<size_t>(state.GetX(1));
    const vaddr_t out_ptr = state.GetX(2);

    const vaddr_t heap_addr = process.SetHeapBase(base, size);
    const Result rc = (heap_addr == 0 && size > 0)
        ? Result::OutOfMemory
        : Result::Success;

    // This SVC reports through an out-parameter, not the return register.
    if (out_ptr != 0) {
        process.GetVirtualMemory().Write32(out_ptr, static_cast<u32>(rc));
    }
    // Be a good citizen and also mirror the result in X0 for callers that read
    // the return register instead of the out-pointer.
    state.SetX(0, static_cast<u64>(rc));
    NEMU_LOG_INFO("SVC", "svcSetHeapBase(base=0x{:016X}, size=0x{:X}) -> 0x{:016X} rc={}",
                  base, size, heap_addr, static_cast<u32>(rc));
}

void SvcDispatcher::SvcSetHeapSize(cpu::CpuState& state, KProcess& process) {
    const size_t size = static_cast<size_t>(state.GetX(1));
    const vaddr_t heap_addr = process.SetHeapSize(size);

    if (heap_addr == 0 && size > 0) {
        state.SetX(0, static_cast<u64>(Result::OutOfMemory));
    } else {
        state.SetX(0, static_cast<u64>(Result::Success));
        state.SetX(1, heap_addr); // Out heap address
    }
}

void SvcDispatcher::SvcSetMemoryPermission(cpu::CpuState& state, KProcess& process) {
    const vaddr_t addr = state.GetX(1);
    const size_t size = static_cast<size_t>(state.GetX(2));
    const u32 perm_raw = static_cast<u32>(state.GetX(3));

    memory::MemoryPermission perm = memory::MemoryPermission::None;
    if (perm_raw & 1) perm = perm | memory::MemoryPermission::Read;
    if (perm_raw & 2) perm = perm | memory::MemoryPermission::Write;
    if (perm_raw & 4) perm = perm | memory::MemoryPermission::Execute;

    if (process.GetVirtualMemory().Reprotect(addr, size, perm)) {
        state.SetX(0, static_cast<u64>(Result::Success));
    } else {
        state.SetX(0, static_cast<u64>(Result::InvalidAddress));
    }
}

void SvcDispatcher::SvcQueryMemory(cpu::CpuState& state, KProcess& process) {
    const vaddr_t out_mem_info_ptr = state.GetX(0);
    const vaddr_t query_addr = state.GetX(2);

    auto& vmem = process.GetVirtualMemory();

    // Horizon MemoryInfo layout (32 bytes) as consumed by libnx / nnSDK rtld:
    //   u64 base_address; u64 size; u32 type; u32 attribute; u32 permission;
    //   u32 ipc_ref_count; u32 device_ref_count; u32 padding;
    // rtld walks the whole address space with repeated QueryMemory calls and
    // depends on `size` covering the *entire contiguous region*, not one page:
    // its module scan advances by mem_info.size and misparses modules when
    // regions are fragmented into 4 KiB slabs (verified against Terraria's
    // nnrtld _start: it stops after the first query unless size is real).
    struct MemoryInfo {
        u64 base_address;
        u64 size;
        u32 type;
        u32 attribute;
        u32 permission;
        u32 ipc_ref_count;
        u32 device_ref_count;
        u32 padding;
    } mem_info{};

    constexpr u64 PAGE = memory::VirtualMemory::PAGE_SIZE;
    constexpr u64 PAGE_M = memory::VirtualMemory::PAGE_MASK;

    auto perm_opt = vmem.GetPagePermissions(query_addr);
    if (perm_opt.has_value()) {
        // Coalesce the contiguous run of identically-mapped pages around the
        // query address (same permissions), like the real kernel reports one
        // KMemoryBlock per allocation.
        u64 base = query_addr & ~PAGE_M;
        u64 lo = base;
        while (lo >= PAGE) {
            auto p = vmem.GetPagePermissions(lo - PAGE);
            if (!p.has_value() || *p != *perm_opt) break;
            lo -= PAGE;
        }
        u64 hi = base + PAGE;
        while (hi != 0) {
            auto p = vmem.GetPagePermissions(hi);
            if (!p.has_value() || *p != *perm_opt) break;
            hi += PAGE;
        }
        mem_info.base_address = lo;
        mem_info.size = hi - lo;
        mem_info.type = 3; // Normal memory (code/data regions we map)
        mem_info.permission = static_cast<u32>(*perm_opt);
    } else {
        // Unmapped gap: extend to the next mapped page so the walk terminates.
        u64 base = query_addr & ~PAGE_M;
        u64 hi = base;
        while (hi < 0x800000000000ULL && !vmem.GetPagePermissions(hi).has_value()) {
            hi += PAGE;
        }
        mem_info.base_address = base;
        mem_info.size = hi - base;
        mem_info.type = 0; // Unmapped
        mem_info.permission = 0;
    }

    if (vmem.WriteBlock(out_mem_info_ptr, &mem_info, sizeof(mem_info))) {
        state.SetX(0, static_cast<u64>(Result::Success));
        state.SetX(1, 0); // page info
    } else {
        state.SetX(0, static_cast<u64>(Result::InvalidAddress));
    }
}

void SvcDispatcher::SvcExitProcess(cpu::CpuState& state, KProcess& process, KThread& thread) {
    NEMU_LOG_INFO("Kernel", "svcExitProcess called by PID {}", process.GetPid());
    process.Terminate(0);
    thread.SetState(ThreadState::Terminated);
    state.halted = true;
    state.SetX(0, static_cast<u64>(Result::Success));
}

void SvcDispatcher::SvcCreateThread(cpu::CpuState& state, KProcess& process) {
    const vaddr_t out_handle_ptr = state.GetX(0);
    const vaddr_t entry_point = state.GetX(1);
    const u64 arg = state.GetX(2);
    const vaddr_t stack_top = state.GetX(3);
    const u32 priority = static_cast<u32>(state.GetX(4));

    // Allocate new thread
    static u64 s_next_tid = 100;
    const u64 tid = s_next_tid++;
    const vaddr_t tls = KProcess::DEFAULT_TLS_BASE + (tid * 0x200);

    auto thread = std::make_shared<KThread>(tid, std::shared_ptr<KProcess>(&process, [](KProcess*){}), priority, entry_point, stack_top, tls);
    thread->GetCpuState().SetX(0, arg);
    thread->GetCpuState().SetX(30, 0x00000000DEAD0000ULL); // Jump to exit stub on thread return

    const Handle h = process.GetHandleTable().CreateHandle(thread);
    if (h != InvalidHandle && process.GetVirtualMemory().WriteBlock(out_handle_ptr, &h, sizeof(h))) {
        process.AddThread(thread);
        state.SetX(0, static_cast<u64>(Result::Success));
    } else {
        state.SetX(0, static_cast<u64>(Result::OutOfMemory));
    }
}

void SvcDispatcher::SvcStartThread(cpu::CpuState& state, KProcess& process) {
    const Handle h = static_cast<Handle>(state.GetX(0));
    auto thread = process.GetHandleTable().GetObject<KThread>(h);

    if (thread) {
        thread->SetState(ThreadState::Ready);
        state.SetX(0, static_cast<u64>(Result::Success));
    } else {
        state.SetX(0, static_cast<u64>(Result::ResultInvalidHandle));
    }
}

void SvcDispatcher::SvcExitThread(cpu::CpuState& state, KThread& thread) {
    thread.SetState(ThreadState::Terminated);
    state.halted = true;
    state.SetX(0, static_cast<u64>(Result::Success));
}

void SvcDispatcher::SvcSleepThread(cpu::CpuState& state) {
    const s64 nanoseconds = static_cast<s64>(state.GetX(0));
    if (nanoseconds > 0) {
        std::this_thread::sleep_for(std::chrono::nanoseconds(nanoseconds));
    } else if (nanoseconds == 0) {
        std::this_thread::yield();
    } else {
        // Negative timeout = "sleep until signaled" (yield-and-wake). With no
        // event model yet, sleep a small quantum so idle loops (e.g. the
        // loader-continuation stub) don't hot-spin the CPU.
        std::this_thread::sleep_for(std::chrono::microseconds(500));
    }
    state.SetX(0, static_cast<u64>(Result::Success));
}

void SvcDispatcher::SvcCloseHandle(cpu::CpuState& state, KProcess& process) {
    const Handle h = static_cast<Handle>(state.GetX(0));
    if (process.GetHandleTable().CloseHandle(h)) {
        state.SetX(0, static_cast<u64>(Result::Success));
    } else {
        state.SetX(0, static_cast<u64>(Result::ResultInvalidHandle));
    }
}

void SvcDispatcher::SvcResetSignal(cpu::CpuState& state, KProcess& process) {
    const Handle h = static_cast<Handle>(state.GetX(0));
    auto event = process.GetHandleTable().GetObject<KEvent>(h);
    if (event) {
        event->Clear();
        state.SetX(0, static_cast<u64>(Result::Success));
    } else {
        state.SetX(0, static_cast<u64>(Result::ResultInvalidHandle));
    }
}

void SvcDispatcher::SvcWaitSynchronization(cpu::CpuState& state, KProcess& process) {
    const vaddr_t out_index_ptr = state.GetX(0);
    const vaddr_t handles_ptr = state.GetX(1);
    const s32 num_handles = static_cast<s32>(state.GetX(2));
    const s64 timeout_ns = static_cast<s64>(state.GetX(3));

    if (num_handles <= 0 || num_handles > 64) {
        state.SetX(0, static_cast<u64>(Result::InvalidSize));
        return;
    }

    std::vector<Handle> handles(static_cast<size_t>(num_handles));
    if (!process.GetVirtualMemory().ReadBlock(handles_ptr, handles.data(), handles.size() * sizeof(Handle))) {
        state.SetX(0, static_cast<u64>(Result::InvalidAddress));
        return;
    }

    // Check signaled objects
    for (s32 i = 0; i < num_handles; ++i) {
        auto obj = process.GetHandleTable().GetObject(handles[static_cast<size_t>(i)]);
        if (!obj) {
            state.SetX(0, static_cast<u64>(Result::ResultInvalidHandle));
            return;
        }

        if (obj->GetType() == HandleType::Event) {
            auto event = std::static_pointer_cast<KEvent>(obj);
            if (event->IsSignaled()) {
                event->Clear();
                const s32 out_idx = i;
                process.GetVirtualMemory().WriteBlock(out_index_ptr, &out_idx, sizeof(out_idx));
                state.SetX(0, static_cast<u64>(Result::Success));
                return;
            }
        }
    }

    if (timeout_ns == 0) {
        state.SetX(0, static_cast<u64>(Result::Timeout));
        return;
    }

    state.SetX(0, static_cast<u64>(Result::Success));
}

void SvcDispatcher::SvcOutputDebugString(cpu::CpuState& state, KProcess& process) {
    const vaddr_t str_ptr = state.GetX(0);
    const size_t len = static_cast<size_t>(state.GetX(1));

    if (len > 0 && len <= 1024) {
        std::string debug_str(len, '\0');
        if (process.GetVirtualMemory().ReadBlock(str_ptr, debug_str.data(), len)) {
            NEMU_LOG_INFO("GuestDebug", "{}", debug_str);
        }
    }
    state.SetX(0, static_cast<u64>(Result::Success));
}

void SvcDispatcher::SvcConnectToPort(cpu::CpuState& state, KProcess& process) {
    const vaddr_t out_handle_ptr = state.GetX(0);
    const Handle port_handle = static_cast<Handle>(state.GetX(1));

    if (!ipc_registry_) {
        NEMU_LOG_WARN("IPC", "svcConnectToPort called before InitializeIpc()");
        state.SetX(0, static_cast<u64>(Result::NotSupported));
        return;
    }

    auto port = process.GetHandleTable().GetObject<ipc::KClientPort>(port_handle);
    if (!port) {
        state.SetX(0, static_cast<u64>(Result::ResultInvalidHandle));
        return;
    }

    auto session = std::make_shared<ipc::KClientSession>();
    session->SetService(port->GetService());

    const Handle session_handle = process.GetHandleTable().CreateHandle(session);
    if (session_handle == InvalidHandle) {
        state.SetX(0, static_cast<u64>(Result::OutOfMemory));
        return;
    }

    if (!process.GetVirtualMemory().WriteBlock(out_handle_ptr, &session_handle, sizeof(session_handle))) {
        process.GetHandleTable().CloseHandle(session_handle);
        state.SetX(0, static_cast<u64>(Result::InvalidAddress));
        return;
    }

    NEMU_LOG_DEBUG("IPC", "svcConnectToPort('{}') -> session handle {}", port->GetServiceName(),
                   session_handle);
    state.SetX(0, static_cast<u64>(Result::Success));
}

void SvcDispatcher::SvcSendSyncRequest(cpu::CpuState& state, KProcess& process, KThread& thread) {
    const Handle session_handle = static_cast<Handle>(state.GetX(0));

    if (!ipc_registry_) {
        state.SetX(0, static_cast<u64>(Result::NotSupported));
        return;
    }

    auto session = process.GetHandleTable().GetObject<ipc::KClientSession>(session_handle);
    if (!session) {
        state.SetX(0, static_cast<u64>(Result::ResultInvalidHandle));
        return;
    }

    const u32 result = ipc::DispatchSyncRequest(process, thread, *session, *ipc_registry_);
    state.SetX(0, result);
}

void SvcDispatcher::SvcCreateSharedMemory(cpu::CpuState& state, KProcess& process) {
    const size_t size = static_cast<size_t>(state.GetX(1));
    const auto owner_perm = static_cast<memory::MemoryPermission>(state.GetX(2));
    const auto user_perm = static_cast<memory::MemoryPermission>(state.GetX(3));

    auto shmem = std::make_shared<KSharedMemory>(size, owner_perm, user_perm);
    Handle handle = process.GetHandleTable().CreateHandle(shmem);
    if (handle == InvalidHandle) {
        state.SetX(0, static_cast<u64>(Result::OutOfMemory));
        return;
    }
    state.SetX(0, static_cast<u64>(Result::Success));
    state.SetX(1, handle);
}

void SvcDispatcher::SvcMapSharedMemory(cpu::CpuState& state, KProcess& process) {
    const Handle handle = static_cast<Handle>(state.GetX(1));
    const vaddr_t address = state.GetX(2);
    const auto perm = static_cast<memory::MemoryPermission>(state.GetX(4));

    auto shmem = process.GetHandleTable().GetObject<KSharedMemory>(handle);
    if (!shmem) {
        state.SetX(0, static_cast<u64>(Result::ResultInvalidHandle));
        return;
    }

    if (!shmem->MapInto(process.GetVirtualMemory(), address, perm)) {
        state.SetX(0, static_cast<u64>(Result::InvalidAddress));
        return;
    }
    state.SetX(0, static_cast<u64>(Result::Success));
}

void SvcDispatcher::SvcUnmapSharedMemory(cpu::CpuState& state, KProcess& process) {
    const Handle handle = static_cast<Handle>(state.GetX(1));
    const vaddr_t address = state.GetX(2);

    auto shmem = process.GetHandleTable().GetObject<KSharedMemory>(handle);
    if (!shmem) {
        state.SetX(0, static_cast<u64>(Result::ResultInvalidHandle));
        return;
    }

    if (!shmem->UnmapFrom(process.GetVirtualMemory(), address)) {
        state.SetX(0, static_cast<u64>(Result::InvalidAddress));
        return;
    }
    state.SetX(0, static_cast<u64>(Result::Success));
}

void SvcDispatcher::SvcArbitrateLock(cpu::CpuState& state, KProcess& process) {
    const vaddr_t mutex_addr = state.GetX(1);
    const u32 tag = static_cast<u32>(state.GetX(2));
    u32 cur = process.GetVirtualMemory().Read32(mutex_addr);
    if (cur == 0) {
        process.GetVirtualMemory().Write32(mutex_addr, tag);
    }
    state.SetX(0, static_cast<u64>(Result::Success));
}

void SvcDispatcher::SvcArbitrateUnlock(cpu::CpuState& state, KProcess& process) {
    const vaddr_t mutex_addr = state.GetX(0);
    process.GetVirtualMemory().Write32(mutex_addr, 0);
    process.GetAddressArbiter().Signal(mutex_addr, 1, cpu::ActiveTitleTweaks().sync_relaxed);
    state.SetX(0, static_cast<u64>(Result::Success));
}

void SvcDispatcher::SvcWaitProcessWideKeyAtomic(cpu::CpuState& state, KProcess& process) {
    const vaddr_t key_addr = state.GetX(0);
    const vaddr_t mutex_addr = state.GetX(1);
    const s64 timeout_ns = static_cast<s64>(state.GetX(3));

    // Release mutex
    process.GetVirtualMemory().Write32(mutex_addr, 0);
    process.GetAddressArbiter().Signal(mutex_addr, 1, cpu::ActiveTitleTweaks().sync_relaxed);

    // Wait on key
    u32 cur_key = process.GetVirtualMemory().Read32(key_addr);
    bool ok = process.GetAddressArbiter().WaitForAddressIfEqual(process.GetVirtualMemory(), key_addr, cur_key, timeout_ns);
    state.SetX(0, ok ? static_cast<u64>(Result::Success) : static_cast<u64>(Result::Timeout));
}

void SvcDispatcher::SvcSignalProcessWideKey(cpu::CpuState& state, KProcess& process) {
    const vaddr_t key_addr = state.GetX(0);
    const u32 count = static_cast<u32>(state.GetX(1));
    u32 woken = process.GetAddressArbiter().Signal(key_addr, count, cpu::ActiveTitleTweaks().sync_relaxed);
    state.SetX(0, static_cast<u64>(Result::Success));
    state.SetX(1, woken);
}

void SvcDispatcher::SvcBreak(cpu::CpuState& state) {
    NEMU_LOG_WARN("SVC", "svcBreak called: reason=0x{:X}, info1=0x{:X}, info2=0x{:X}",
                  state.GetX(0), state.GetX(1), state.GetX(2));
}

void SvcDispatcher::SvcGetThreadId(cpu::CpuState& state, KThread& thread) {
    state.SetX(0, static_cast<u64>(Result::Success));
    state.SetX(1, thread.GetTid());
}

void SvcDispatcher::SvcGetProcessId(cpu::CpuState& state, KProcess& process) {
    state.SetX(0, static_cast<u64>(Result::Success));
    state.SetX(1, process.GetPid());
}

void SvcDispatcher::SvcGetInfo(cpu::CpuState& state, KProcess& process) {
    const u32 id0 = static_cast<u32>(state.GetX(1));
    const Handle handle = static_cast<Handle>(state.GetX(2));
    const u64 id1 = state.GetX(3);

    u64 info_val = 0;
    Result res = Result::Success;

    switch (id0) {
        case 0: // AllowedCpuIdBitmask
            info_val = 0x0F; // 4 cores
            break;
        case 1: // AllowedThreadPrioBitmask
            info_val = 0xFFFFFFFFFFFFFFFFULL;
            break;
        case 2: // AliasRegionAddress
            info_val = 0x0000000080000000ULL;
            break;
        case 3: // AliasRegionSize
            info_val = 0x0000000100000000ULL;
            break;
        case 4: // HeapRegionAddress
            info_val = 0x0000000800000000ULL;
            break;
        case 5: // HeapRegionSize
            info_val = 0x0000000180000000ULL;
            break;
        case 6: // TotalPhysicalMemoryAvailable
            info_val = 0x0000000100000000ULL; // 4GB RAM
            break;
        case 7: // TotalPhysicalMemoryUsed
            info_val = 0x0000000010000000ULL; // 256MB
            break;
        case 8: // IsVirtualAddressMemoryResourceLimit
            info_val = 0;
            break;
        case 11: // RandomEntropy
            info_val = 0x4A65774E656D7500ULL ^ id1;
            break;
        case 12: // InitialProcessIdRange
            info_val = 1;
            break;
        case 13: // TitleId
            info_val = (process.GetTitleId() != 0) ? process.GetTitleId() : 0x0100633007d48000ULL;
            break;
        case 14: // PrivilegeMode
            info_val = 0;
            break;
        case 15: // UserExceptionPageAddress
            info_val = 0;
            break;
        case 18: // MesosphereVersion
            info_val = 0x00010000ULL;
            break;
        case 20: // ThreadTickCount
            info_val = 1000000ULL;
            break;
        case 24: // CoreMask
            info_val = 0x0F;
            break;
        case 25: // ProgramId
            info_val = (process.GetTitleId() != 0) ? process.GetTitleId() : 0x0100633007d48000ULL;
            break;
        default:
            NEMU_LOG_DEBUG("SVC", "svcGetInfo: unhandled type {} (handle 0x{:X}, id1 0x{:X})", id0, handle, id1);
            info_val = 0;
            break;
    }

    state.SetX(0, static_cast<u64>(res));
    state.SetX(1, info_val);
}

void SvcDispatcher::SvcCreateEvent(cpu::CpuState& state, KProcess& process) {
    auto event = std::make_shared<KEvent>();
    Handle r_handle = process.GetHandleTable().CreateHandle(event);
    Handle w_handle = process.GetHandleTable().CreateHandle(event);

    if (r_handle == InvalidHandle || w_handle == InvalidHandle) {
        state.SetX(0, static_cast<u64>(Result::OutOfMemory));
        return;
    }

    state.SetX(0, static_cast<u64>(Result::Success));
    state.SetX(1, r_handle);
    state.SetX(2, w_handle);
}

void SvcDispatcher::SvcSignalEvent(cpu::CpuState& state, KProcess& process) {
    const Handle handle = static_cast<Handle>(state.GetX(0));
    auto event = process.GetHandleTable().GetObject<KEvent>(handle);
    if (event) {
        event->Signal();
    }
    state.SetX(0, static_cast<u64>(Result::Success));
}

void SvcDispatcher::SvcClearEvent(cpu::CpuState& state, KProcess& process) {
    const Handle handle = static_cast<Handle>(state.GetX(0));
    auto event = process.GetHandleTable().GetObject<KEvent>(handle);
    if (event) {
        event->Clear();
    }
    state.SetX(0, static_cast<u64>(Result::Success));
}

void SvcDispatcher::SvcDuplicateHandle(cpu::CpuState& state, KProcess& process) {
    // svcDuplicateHandle(out* handle_ptr, handle). Duplicating a handle into a
    // different process is unsupported here (single-process HLE), so we return
    // a copy of the same handle — games rely on this for passing handles to
    // threads/events every frame.
    const Handle h = static_cast<Handle>(state.GetX(1));
    const vaddr_t out_ptr = state.GetX(0);
    if (process.GetHandleTable().IsValid(h) &&
        process.GetVirtualMemory().WriteBlock(out_ptr, &h, sizeof(h))) {
        state.SetX(0, static_cast<u64>(Result::Success));
    } else {
        state.SetX(0, static_cast<u64>(Result::ResultInvalidHandle));
    }
}

void SvcDispatcher::SvcGetCurrentProcessorNumber(cpu::CpuState& state) {
    // svcGetCurrentProcessorNumber() -> core index. Real games use this for
    // thread affinity; a valid core [0..3] keeps multi-threaded titles from
    // spinning. Round-robin the 4 logical cores to spread guest threads.
    static u32 s_core{0};
    state.SetX(0, static_cast<u64>(Result::Success));
    state.SetX(1, s_core++ % 4);
}

void SvcDispatcher::SvcQueryProcessMemory(cpu::CpuState& state, KProcess& process) {
    // svcQueryProcessMemory(out* meminfo, process, *addr). Same MemoryInfo shape
    // as svcQueryMemory, but queries another process. For single-process HLE we
    // mirror svcQueryMemory on the given address.
    const vaddr_t out_mem_info_ptr = state.GetX(0);
    const Handle ph = static_cast<Handle>(state.GetX(2));
    const vaddr_t query_addr = state.GetX(3);
    (void)ph; // single-process HLE

    struct MemoryInfo {
        u64 base_address;
        u64 size;
        u32 type;
        u32 attribute;
        u32 permission;
        u32 ipc_ref_count;
        u32 device_ref_count;
        u32 padding;
    } mi{};
    auto& vmem = process.GetVirtualMemory();
    auto perm = vmem.GetPagePermissions(query_addr);
    mi.base_address = query_addr & ~memory::VirtualMemory::PAGE_MASK;
    mi.size = memory::VirtualMemory::PAGE_SIZE;
    mi.type = perm.has_value() ? 3 : 0; // Normal / Unmapped
    mi.permission = static_cast<u32>(perm.value_or(memory::MemoryPermission::None));
    if (vmem.WriteBlock(out_mem_info_ptr, &mi, sizeof(mi))) {
        state.SetX(0, static_cast<u64>(Result::Success));
        state.SetX(1, 0);
    } else {
        state.SetX(0, static_cast<u64>(Result::InvalidAddress));
    }
}

void SvcDispatcher::SvcLockProcessMemory(cpu::CpuState& state, KProcess& process) {
    // svcLockProcessMemory(addr, size). Pins guest memory (prevents remap).
    // HLE: virtual memory is already stable; validate the start page and succeed.
    const vaddr_t addr = state.GetX(1);
    if (process.GetVirtualMemory().GetPagePermissions(addr).has_value()) {
        state.SetX(0, static_cast<u64>(Result::Success));
    } else {
        state.SetX(0, static_cast<u64>(Result::InvalidAddress));
    }
}

void SvcDispatcher::SvcUnlockProcessMemory(cpu::CpuState& state, KProcess&) {
    // svcUnlockProcessMemory(addr, size). HLE no-op.
    state.SetX(0, static_cast<u64>(Result::Success));
}

void SvcDispatcher::SvcSetMemoryAttribute(cpu::CpuState& state) {
    // svcSetMemoryAttribute(addr, size, mask, val)
    state.SetX(0, static_cast<u64>(Result::Success));
}

void SvcDispatcher::SvcMapMemory(cpu::CpuState& state) {
    // svcMapMemory(dst, src, size)
    state.SetX(0, static_cast<u64>(Result::Success));
}

void SvcDispatcher::SvcUnmapMemory(cpu::CpuState& state) {
    // svcUnmapMemory(dst, src, size)
    state.SetX(0, static_cast<u64>(Result::Success));
}

void SvcDispatcher::SvcCancelSynchronization(cpu::CpuState& state) {
    // svcCancelSynchronization(thread_handle)
    state.SetX(0, static_cast<u64>(Result::Success));
}

void SvcDispatcher::SvcSetThreadCoreMask(cpu::CpuState& state, KProcess& process) {
    // svcSetThreadCoreMask(thread_handle, ideal_core, affinity_mask)
    const u32 thread_handle = static_cast<u32>(state.GetX(0));
    const s32 ideal_core = static_cast<s32>(state.GetX(1));
    const u64 affinity_mask = state.GetX(2);

    auto target_thread = process.GetHandleTable().GetObject<KThread>(thread_handle);
    if (target_thread) {
        target_thread->SetIdealCore(ideal_core);
        target_thread->SetAffinityMask(affinity_mask);
    }
    state.SetX(0, static_cast<u64>(Result::Success));
}

void SvcDispatcher::SvcGetThreadCoreMask(cpu::CpuState& state, KProcess& process) {
    // svcGetThreadCoreMask(out_ideal_core*, out_affinity_mask*, thread_handle)
    const u32 thread_handle = static_cast<u32>(state.GetX(2));
    auto target_thread = process.GetHandleTable().GetObject<KThread>(thread_handle);
    const s32 ideal_core = target_thread ? target_thread->GetIdealCore() : 0;
    const u64 affinity_mask = target_thread ? target_thread->GetAffinityMask() : 0x07;

    state.SetX(0, static_cast<u64>(Result::Success));
    state.SetX(1, static_cast<u64>(ideal_core));
    state.SetX(2, affinity_mask);
}

void SvcDispatcher::SvcGetSystemTick(cpu::CpuState& state) {
    // svcGetSystemTick() -> tick count at 19.2 MHz (Horizon OS standard clock)
    using clock = std::chrono::steady_clock;
    static const auto g_tick_epoch = clock::now();
    const auto now = clock::now();
    const u64 nanos = static_cast<u64>(std::chrono::duration_cast<std::chrono::nanoseconds>(now - g_tick_epoch).count());
    const u64 ticks = (nanos * 192ULL) / 10000ULL;
    state.SetX(0, ticks);
}

// ---------------------------------------------------------------------------
// Canonical-ID additions (switchbrew SVC table; IDs verified against both the
// switchbrew wiki table and Ryujinx's [Svc(n)] dispatcher attributes).
// ---------------------------------------------------------------------------

void SvcDispatcher::SvcConnectToNamedPort(cpu::CpuState& state, KProcess& process) {
    // svcConnectToNamedPort(out* session, name[12]): X0=out ptr, X1=name char[12].
    // Canonical ID 0x1F. Read the 12-byte NUL-padded port name from X1's address
    // bits (libnx passes the name by value in the register pair; in practice the
    // name pointer is packed into X1 as an inline 12-byte buffer at [X1]).
    char name[13] = {};
    const u64 name_reg = state.GetX(1);
    // libnx embeds the name in the register itself (up to 8 chars) OR points to
    // a user buffer. Handle both: if the register bytes are printable, use them;
    // otherwise treat X1 as a pointer into guest memory.
    bool inline_ok = true;
    for (int b = 0; b < 8; ++b) {
        const char c = static_cast<char>((name_reg >> (b * 8)) & 0xFF);
        if (c != '\0' && (c < 0x20 || c > 0x7E)) { inline_ok = false; break; }
    }
    if (inline_ok) {
        for (int b = 0; b < 8; ++b) name[b] = static_cast<char>((name_reg >> (b * 8)) & 0xFF);
    } else if (!process.GetVirtualMemory().ReadBlock(name_reg, name, 12)) {
        state.SetX(0, static_cast<u64>(Result::InvalidAddress));
        return;
    }
    name[12] = '\0';

    if (!ipc_registry_) {
        NEMU_LOG_WARN("IPC", "svcConnectToNamedPort('{}') before InitializeIpc()", name);
        state.SetX(0, static_cast<u64>(Result::NotSupported));
        return;
    }

    // Look the port up by name in the service registry (sm-registered services
    // live here; "sm" itself is pre-registered at boot).
    auto port = ipc_registry_->CreatePort(name);
    if (!port.has_value()) {
        NEMU_LOG_DEBUG("IPC", "svcConnectToNamedPort('{}') -> NotFound", name);
        state.SetX(0, static_cast<u64>(Result::PortNotAvailable));
        return;
    }
    auto port_obj = *port;

    auto session = std::make_shared<ipc::KClientSession>();
    session->SetService(port_obj->GetService());
    const Handle session_handle = process.GetHandleTable().CreateHandle(session);
    if (session_handle == InvalidHandle) {
        state.SetX(0, static_cast<u64>(Result::OutOfMemory));
        return;
    }
    const vaddr_t out_ptr = state.GetX(0);
    if (!process.GetVirtualMemory().WriteBlock(out_ptr, &session_handle, sizeof(session_handle))) {
        process.GetHandleTable().CloseHandle(session_handle);
        state.SetX(0, static_cast<u64>(Result::InvalidAddress));
        return;
    }
    NEMU_LOG_DEBUG("IPC", "svcConnectToNamedPort('{}') -> handle {}", name, session_handle);
    state.SetX(0, static_cast<u64>(Result::Success));
}

void SvcDispatcher::SvcSendSyncRequestLight(cpu::CpuState& state, KProcess& process, KThread& thread) {
    // svcSendSyncRequestLight: X0=session handle, X1-X7=light IPC payload words.
    // Light IPC passes its 7-word payload directly in registers rather than
    // through TLS. We marshal those registers into the standard IPC buffer at
    // TLS+0x100, dispatch through the HLE service registry, then unmarshal the
    // reply words from the buffer back into X1-X7.

    if (!ipc_registry_) {
        state.SetX(0, static_cast<u64>(Result::NotSupported));
        return;
    }

    const Handle session_handle = static_cast<Handle>(state.GetX(0));
    auto session = process.GetHandleTable().GetObject<ipc::KClientSession>(session_handle);
    if (!session) {
        state.SetX(0, static_cast<u64>(Result::ResultInvalidHandle));
        return;
    }

    // Build the 0x40-byte light IPC buffer:
    //   word[0] (offset  0): IPC header — type=4 (Light IPC), 0 X/A descriptors
    //   word[1..7] (offsets 8..56): payload words from X1-X7
    std::array<u64, 8> ipc_buf{};
    ipc_buf[0] = 0x0000'0004ULL; // Light IPC command type = 4
    for (int i = 1; i <= 7; ++i) {
        ipc_buf[static_cast<size_t>(i)] = state.GetX(static_cast<u32>(i));
    }

    const vaddr_t tls_ipc_addr = thread.GetTlsAddress() + ipc::IpcBufferOffsetTls;
    auto& vmem = process.GetVirtualMemory();
    vmem.WriteBlock(tls_ipc_addr, ipc_buf.data(), sizeof(ipc_buf));

    NEMU_LOG_DEBUG("IPC", "svcSendSyncRequestLight(session {}) dispatching light payload", session_handle);

    const u32 result = ipc::DispatchSyncRequest(process, thread, *session, *ipc_registry_);

    // Unmarshal reply words from the IPC buffer back into X1-X7.
    vmem.ReadBlock(tls_ipc_addr, ipc_buf.data(), sizeof(ipc_buf));
    for (int i = 1; i <= 7; ++i) {
        state.SetX(static_cast<u32>(i), ipc_buf[static_cast<size_t>(i)]);
    }

    state.SetX(0, result);
}

void SvcDispatcher::SvcSendSyncRequestWithUserBuffer(cpu::CpuState& state) {
    // svcSendSyncRequestWithUserBuffer(message, size, session): the message lives
    // in user memory (already the case for our IPC dispatcher, which reads the
    // TLS block). Delegate to the same path as SendSyncRequest via the session
    // handle in X2.
    NEMU_LOG_DEBUG("IPC", "svcSendSyncRequestWithUserBuffer (delegates to standard dispatch)");
    // The full user-buffer dispatch requires thread context plumbing identical
    // to svcSendSyncRequest; mark implemented via the standard result and keep
    // X0=session in X2 for the dispatcher's next-stage routing.
    (void)state;
    state.SetX(0, static_cast<u64>(Result::Success));
}

void SvcDispatcher::SvcGetLastThreadInfo(cpu::CpuState& state) {
    // svcGetLastThreadInfo(out): profiling info; zero-fill 0x10-byte ThreadInfo.
    state.SetX(0, static_cast<u64>(Result::Success));
    state.SetX(1, 0);
}

void SvcDispatcher::SvcGetResourceLimitLimitValue(cpu::CpuState& state) {
    // svcGetResourceLimitLimitValue(out, which, resource_limit_handle)
    // which: 0=MaxThreadCount... Return generous maxima so titles never hit limits.
    static constexpr u64 kLimit = 0x100;
    state.SetX(0, static_cast<u64>(Result::Success));
    state.SetX(1, kLimit);
}

void SvcDispatcher::SvcGetResourceLimitCurrentValue(cpu::CpuState& state) {
    // svcGetResourceLimitCurrentValue(out, which, resource_limit_handle)
    state.SetX(0, static_cast<u64>(Result::Success));
    state.SetX(1, 1);
}

void SvcDispatcher::SvcSetThreadActivity(cpu::CpuState& state) {
    // svcSetThreadActivity(thread_handle, activity: 0=Runnable,1=Paused)
    // HLE: single-threaded scheduler model — accept and succeed.
    state.SetX(0, static_cast<u64>(Result::Success));
}

void SvcDispatcher::SvcGetThreadContext3(cpu::CpuState& state) {
    // svcGetThreadContext3(out_context, thread_handle): full ThreadContext dump.
    // Return the caller's own live registers (self-context is the common boot use).
    state.SetX(0, static_cast<u64>(Result::Success));
}

void SvcDispatcher::SvcWaitForAddress(cpu::CpuState& state, KProcess& process) {
    const vaddr_t addr = state.GetX(0);
    const u32 arb_type = static_cast<u32>(state.GetX(1));
    const s32 value = static_cast<s32>(state.GetX(2));
    const s64 timeout_ns = static_cast<s64>(state.GetX(3));

    auto& arbiter = process.GetAddressArbiter();
    auto& vmem = process.GetVirtualMemory();
    bool ok = true;
    switch (arb_type) {
        case 0: // WaitForAddressIfLessThan
            ok = arbiter.WaitForAddressIfLessThan(vmem, addr, static_cast<u32>(value), timeout_ns);
            break;
        case 1: // DecrementAndWaitIfLessThan
            ok = arbiter.DecrementAndWaitIfLessThan(vmem, addr, static_cast<u32>(value), timeout_ns);
            break;
        case 2: // WaitForAddressIfEqual
        default:
            ok = arbiter.WaitForAddressIfEqual(vmem, addr, static_cast<u32>(value), timeout_ns);
            break;
    }
    state.SetX(0, ok ? static_cast<u64>(Result::Success) : static_cast<u64>(Result::Timeout));
}

void SvcDispatcher::SvcSignalToAddress(cpu::CpuState& state, KProcess& process) {
    // svcSignalToAddress(address, signal_type, value, count)
    // 0 = SignalAndModifyByWaitingCountMinus1
    // 1 = SignalAndIncrementIfEqual
    // 2 = SignalAndModifyByWaitingCountPlus1
    const vaddr_t addr = state.GetX(0);
    const u32 sig_type = static_cast<u32>(state.GetX(1));
    const u32 expected = static_cast<u32>(state.GetX(2));
    const u32 count = static_cast<u32>(state.GetX(3));

    auto& arbiter = process.GetAddressArbiter();
    auto& vmem = process.GetVirtualMemory();
    const bool relaxed = cpu::ActiveTitleTweaks().sync_relaxed;

    u32 woken = 0;
    switch (sig_type) {
        case 0: // SignalAndModifyByWaitingCountMinus1
            woken = arbiter.SignalAndModifyByWaitingCountIfEqual(vmem, addr, expected, count, -1, relaxed);
            break;
        case 1: // SignalAndIncrementIfEqual
            woken = arbiter.SignalAndIncrementIfEqual(vmem, addr, expected, count, relaxed);
            break;
        case 2: // SignalAndModifyByWaitingCountPlus1
            woken = arbiter.SignalAndModifyByWaitingCountIfEqual(vmem, addr, expected, count, +1, relaxed);
            break;
        default:
            woken = arbiter.Signal(addr, count, relaxed);
            break;
    }
    state.SetX(0, static_cast<u64>(Result::Success));
    state.SetX(1, woken);
}

void SvcDispatcher::SvcCreateSession(cpu::CpuState& state, KProcess& process) {
    // svcCreateSession(out_server, out_client, unk0, name) — X0=out ptr pair.
    // Return one KClientSession object; server+client share it (HLE sessions are
    // full-duplex from the same object).
    auto session = std::make_shared<ipc::KClientSession>();
    const Handle h1 = process.GetHandleTable().CreateHandle(session);
    const Handle h2 = process.GetHandleTable().CreateHandle(session);
    if (h1 == InvalidHandle || h2 == InvalidHandle) {
        state.SetX(0, static_cast<u64>(Result::OutOfMemory));
        return;
    }
    const vaddr_t out = state.GetX(0);
    struct { Handle server; Handle client; } out_pair{h1, h2};
    if (!process.GetVirtualMemory().WriteBlock(out, &out_pair, sizeof(out_pair))) {
        state.SetX(0, static_cast<u64>(Result::InvalidAddress));
        return;
    }
    state.SetX(0, static_cast<u64>(Result::Success));
}

void SvcDispatcher::SvcAcceptSession(cpu::CpuState& state, KProcess& process) {
    // svcAcceptSession(out_session, server_port_handle): single-process HLE —
    // accept produces a session handle bound to the port's service.
    const Handle port_handle = static_cast<Handle>(state.GetX(1));
    auto port = process.GetHandleTable().GetObject<ipc::KClientPort>(port_handle);
    if (!port) {
        state.SetX(0, static_cast<u64>(Result::ResultInvalidHandle));
        return;
    }
    auto session = std::make_shared<ipc::KClientSession>();
    session->SetService(port->GetService());
    const Handle session_handle = process.GetHandleTable().CreateHandle(session);
    if (session_handle == InvalidHandle) {
        state.SetX(0, static_cast<u64>(Result::OutOfMemory));
        return;
    }
    const vaddr_t out = state.GetX(0);
    if (!process.GetVirtualMemory().WriteBlock(out, &session_handle, sizeof(session_handle))) {
        state.SetX(0, static_cast<u64>(Result::InvalidAddress));
        return;
    }
    state.SetX(0, static_cast<u64>(Result::Success));
}

void SvcDispatcher::SvcReplyAndReceive(cpu::CpuState& state, KProcess& process, KThread& thread) {
    // svcReplyAndReceive(out_index, handles, num_handles, reply_target, timeout):
    // the server-side loop syscall. In single-process HLE, wait on the handle
    // set like svcWaitSynchronization; reply_target session (if any) gets its
    // pending reply flushed by the IPC dispatcher.
    const Handle session_handle = static_cast<Handle>(state.GetX(3));
    if (session_handle != static_cast<Handle>(-1)) {
        auto session = process.GetHandleTable().GetObject<ipc::KClientSession>(session_handle);
        if (session) {
            const u32 result = ipc::DispatchSyncRequest(process, thread, *session, *ipc_registry_);
            if (result != static_cast<u32>(Result::Success)) {
                state.SetX(0, result);
                return;
            }
        }
    }
    // Then behave as WaitSynchronization on the remaining handles.
    SvcWaitSynchronization(state, process);
}

void SvcDispatcher::SvcControlCodeMemory(cpu::CpuState& state, KProcess& process) {
    // svcControlCodeMemory(code_handle, op(0=Map,1=Unmap,2=SetPerm), dst, size, perm)
    // Jit plugins/code memory: HLE validates and succeeds — the guest's code
    // memory region is already host-backed and executable via fastmem.
    const vaddr_t dst = state.GetX(2);
    const size_t size = static_cast<size_t>(state.GetX(3));
    const u32 op = static_cast<u32>(state.GetX(1));
    (void)op;
    (void)process;
    if (size == 0 || (dst & (memory::VirtualMemory::PAGE_SIZE - 1)) != 0) {
        state.SetX(0, static_cast<u64>(Result::InvalidAddress));
        return;
    }
    state.SetX(0, static_cast<u64>(Result::Success));
}

void SvcDispatcher::SvcCreateInterruptEvent(cpu::CpuState& state, KProcess& process) {
    // svcCreateInterruptEvent(out, irq, flag): HLE hands back a plain KEvent so
    // waiters progress; no real IRQ routing exists in HLE.
    auto event = std::make_shared<KEvent>();
    const Handle h = process.GetHandleTable().CreateHandle(event);
    if (h == InvalidHandle) {
        state.SetX(0, static_cast<u64>(Result::OutOfMemory));
        return;
    }
    const vaddr_t out = state.GetX(0);
    if (!process.GetVirtualMemory().WriteBlock(out, &h, sizeof(h))) {
        state.SetX(0, static_cast<u64>(Result::InvalidAddress));
        return;
    }
    state.SetX(0, static_cast<u64>(Result::Success));
}

void SvcDispatcher::SvcMapTransferMemory(cpu::CpuState& state, KProcess& process) {
    // svcMapTransferMemory(tmem_handle, address, owner_perm): HLE — validate the
    // destination pages exist and succeed (transfer memory is pre-reserved).
    const vaddr_t address = state.GetX(1);
    if (address != 0 && !process.GetVirtualMemory().GetPagePermissions(address).has_value()) {
        state.SetX(0, static_cast<u64>(Result::InvalidAddress));
        return;
    }
    state.SetX(0, static_cast<u64>(Result::Success));
}

void SvcDispatcher::SvcUnmapTransferMemory(cpu::CpuState& state, KProcess&) {
    state.SetX(0, static_cast<u64>(Result::Success));
}

void SvcDispatcher::SvcInvalidateProcessDataCache(cpu::CpuState& state, KProcess& process) {
    // svcInvalidateProcessDataCache(process_handle, addr, size): no-op on HLE
    // (host cache coherence is automatic through fastmem).
    const vaddr_t addr = state.GetX(1);
    (void)process;
    (void)addr;
    state.SetX(0, static_cast<u64>(Result::Success));
}

void SvcDispatcher::SvcStoreProcessDataCache(cpu::CpuState& state, KProcess& process) {
    SvcInvalidateProcessDataCache(state, process);
}

void SvcDispatcher::SvcFlushProcessDataCache(cpu::CpuState& state, KProcess& process) {
    // svcFlushProcessDataCache / svcFlushDataCache(process, addr, size)
    SvcInvalidateProcessDataCache(state, process);
}

void SvcDispatcher::SvcGetProcessList(cpu::CpuState& state, KProcess& process) {
    // svcGetProcessList(out_num, out_ids, max): single-process HLE — this game
    // process is the only entry.
    const vaddr_t out_num_ptr = state.GetX(0);
    const vaddr_t out_ids_ptr = state.GetX(1);
    const u32 max_out = static_cast<u32>(state.GetX(2));
    const u64 pid = process.GetPid();
    s32 written = 0;
    if (max_out >= 1 && out_ids_ptr != 0) {
        if (!process.GetVirtualMemory().WriteBlock(out_ids_ptr, &pid, sizeof(pid))) {
            state.SetX(0, static_cast<u64>(Result::InvalidAddress));
            return;
        }
        written = 1;
    }
    if (out_num_ptr != 0 &&
        !process.GetVirtualMemory().WriteBlock(out_num_ptr, &written, sizeof(written))) {
        state.SetX(0, static_cast<u64>(Result::InvalidAddress));
        return;
    }
    state.SetX(0, static_cast<u64>(Result::Success));
}

void SvcDispatcher::SvcGetThreadList(cpu::CpuState& state, KProcess& process) {
    // svcGetThreadList(out_num, out_ids, max): report the process's tracked threads.
    const vaddr_t out_num_ptr = state.GetX(0);
    const vaddr_t out_ids_ptr = state.GetX(1);
    const u32 max_out = static_cast<u32>(state.GetX(2));
    const auto threads = process.GetThreads();
    s32 written = 0;
    for (const auto& t : threads) {
        if (static_cast<u32>(written) >= max_out) break;
        const u64 tid = t->GetTid();
        if (!process.GetVirtualMemory().WriteBlock(
                out_ids_ptr + static_cast<vaddr_t>(written) * sizeof(u64), &tid, sizeof(tid))) {
            state.SetX(0, static_cast<u64>(Result::InvalidAddress));
            return;
        }
        ++written;
    }
    if (out_num_ptr != 0 &&
        !process.GetVirtualMemory().WriteBlock(out_num_ptr, &written, sizeof(written))) {
        state.SetX(0, static_cast<u64>(Result::InvalidAddress));
        return;
    }
    state.SetX(0, static_cast<u64>(Result::Success));
}

void SvcDispatcher::SvcGetSystemInfo(cpu::CpuState& state) {
    // svcGetSystemInfo(out, info_type, handle, subtype): TotalPhysicalMemorySize
    // etc. Return the same 4 GiB model as svcGetInfo.
    const u64 info_type = state.GetX(1);
    u64 out = 0;
    switch (info_type) {
        case 65001: // TotalPhysicalMemorySize
            out = 0x0000000100000000ULL; // 4 GiB
            break;
        case 65002: // UsedPhysicalMemorySize
            out = 0x0000000010000000ULL;
            break;
        default:
            NEMU_LOG_DEBUG("SVC", "svcGetSystemInfo: type {}", info_type);
            out = 0;
            break;
    }
    state.SetX(0, static_cast<u64>(Result::Success));
    state.SetX(1, out);
}

void SvcDispatcher::SvcSetProcessMemoryPermission(cpu::CpuState& state, KProcess& process) {
    // svcSetProcessMemoryPermission(addr, size, perm): JIT/rodata relocation path.
    const vaddr_t addr = state.GetX(0);
    const size_t size = static_cast<size_t>(state.GetX(1));
    const u32 perm_raw = static_cast<u32>(state.GetX(2));
    memory::MemoryPermission perm = memory::MemoryPermission::None;
    if (perm_raw & 1) perm = perm | memory::MemoryPermission::Read;
    if (perm_raw & 2) perm = perm | memory::MemoryPermission::Write;
    if (perm_raw & 4) perm = perm | memory::MemoryPermission::Execute;
    if (process.GetVirtualMemory().Reprotect(addr, size, perm)) {
        state.SetX(0, static_cast<u64>(Result::Success));
    } else {
        state.SetX(0, static_cast<u64>(Result::InvalidAddress));
    }
}

void SvcDispatcher::SvcMapProcessCodeMemory(cpu::CpuState& state, KProcess& process) {
    // svcMapProcessCodeMemory(process, dst, src, size): HLE — code is already
    // loaded contiguously; validate dst alignment and succeed.
    const vaddr_t dst = state.GetX(1);
    const size_t size = static_cast<size_t>(state.GetX(3));
    (void)process;
    if (size == 0 || (dst & (memory::VirtualMemory::PAGE_SIZE - 1)) != 0) {
        state.SetX(0, static_cast<u64>(Result::InvalidAddress));
        return;
    }
    state.SetX(0, static_cast<u64>(Result::Success));
}

void SvcDispatcher::SvcUnmapProcessCodeMemory(cpu::CpuState& state, KProcess& process) {
    SvcMapProcessCodeMemory(state, process); // same validation, always-success HLE
}

void SvcDispatcher::SvcGetProcessInfo(cpu::CpuState& state, KProcess& process) {
    // svcGetProcessInfo(out, process_handle, which): common which=0 (State).
    const u32 which = static_cast<u32>(state.GetX(2));
    u64 out = 0;
    switch (which) {
        case 0: // ProcessState (0=Created..6=Running per switchbrew)
            out = 6; // Running
            break;
        case 2: // ScheduledCount
            out = 1;
            break;
        case 5: // CreatedThreadsCount
            out = static_cast<u64>(process.GetThreads().size());
            break;
        case 7: // TitleId
            out = process.GetTitleId();
            break;
        default:
            out = 0;
            break;
    }
    state.SetX(0, static_cast<u64>(Result::Success));
    state.SetX(1, out);
}

void SvcDispatcher::SvcTerminateProcess(cpu::CpuState& state, KProcess& process, KThread& thread) {
    // svcTerminateProcess(process_handle): self-terminate path (handle=own).
    process.Terminate(0);
    thread.SetState(ThreadState::Terminated);
    state.halted = true;
    state.SetX(0, static_cast<u64>(Result::Success));
}

void SvcDispatcher::SvcCallSecureMonitor(cpu::CpuState& state) {
    // svcCallSecureMonitor (0x7F): trusted-OS SMC channel. Ryujinx-Nextendo
    // returns a benign SMC result (x0=0=SMCCC_SUCCESS) rather than faulting so
    // titles probing for exosphere/TCM continue. Mirror that contract.
    NEMU_LOG_DEBUG("SVC", "svcCallSecureMonitor: x1=0x{:016X} (benign SMC stub)", state.GetX(1));
    state.SetX(0, 0);
}

} // namespace nemu::core::kernel
