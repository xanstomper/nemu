#include "svc.hpp"
#include "k_event.hpp"
#include "platform/logger.hpp"
#include <thread>
#include <vector>

namespace nemu::core::kernel {

void SvcDispatcher::Dispatch(cpu::CpuState& state, KProcess& process, KThread& thread, u32 svc_id) {
    NEMU_LOG_DEBUG("SVC", "Dispatching SVC 0x{:02X} for TID {}", svc_id, thread.GetTid());

    switch (svc_id) {
        case 0x01: SvcSetHeapSize(state, process); break;
        case 0x02: SvcSetMemoryPermission(state, process); break;
        case 0x06: SvcQueryMemory(state, process); break;
        case 0x07: SvcExitProcess(state, process, thread); break;
        case 0x08: SvcCreateThread(state, process); break;
        case 0x09: SvcStartThread(state, process); break;
        case 0x0A: SvcExitThread(state, thread); break;
        case 0x0B: SvcSleepThread(state); break;
        case 0x16: SvcCloseHandle(state, process); break;
        case 0x17: SvcResetSignal(state, process); break;
        case 0x18: SvcWaitSynchronization(state, process); break;
        case 0x27: SvcOutputDebugString(state, process); break;

        default:
            NEMU_LOG_WARN("SVC", "Unhandled SVC 0x{:02X} called at PC 0x{:016X}", svc_id, state.pc);
            state.SetX(0, static_cast<u64>(Result::Unimplemented));
            break;
    }
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
    auto perm_opt = vmem.GetPagePermissions(query_addr);

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

    if (perm_opt.has_value()) {
        mem_info.base_address = query_addr & ~memory::VirtualMemory::PAGE_MASK;
        mem_info.size = memory::VirtualMemory::PAGE_SIZE;
        mem_info.type = 3; // Normal memory
        mem_info.permission = static_cast<u32>(*perm_opt);
    } else {
        mem_info.base_address = query_addr & ~memory::VirtualMemory::PAGE_MASK;
        mem_info.size = memory::VirtualMemory::PAGE_SIZE;
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

    const Handle h = process.GetHandleTable().CreateHandle(thread);
    if (h != InvalidHandle && process.GetVirtualMemory().WriteBlock(out_handle_ptr, &h, sizeof(h))) {
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

} // namespace nemu::core::kernel
