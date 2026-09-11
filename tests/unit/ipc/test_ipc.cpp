#include "core/cpu/cpu_state.hpp"
#include "core/cpu/interpreter.hpp"
#include "core/cpu/jit/jit_compiler.hpp"
#include "core/memory/virtual_memory.hpp"
#include "core/kernel/k_process.hpp"
#include "core/kernel/k_thread.hpp"
#include "core/kernel/svc.hpp"
#include "core/kernel/ipc/service_registry.hpp"
#include "core/kernel/ipc/ipc_service.hpp"
#include "core/kernel/ipc/ipc_dispatcher.hpp"
#include "core/kernel/ipc/ipc_types.hpp"
#include "core/kernel/ipc/sm_service.hpp"
#include "core/kernel/ipc/time_service.hpp"
#include "core/kernel/ipc/set_sys_service.hpp"
#include "core/kernel/ipc/hid_service.hpp"
#include "core/kernel/ipc/k_shared_memory.hpp"
#include "core/kernel/ipc/nvdrv_service.hpp"
#include "core/kernel/ipc/vi_service.hpp"
#include "core/kernel/ipc/fsp_srv_service.hpp"
#include "core/kernel/ipc/audren_service.hpp"
#include "core/kernel/ipc/applet_service.hpp"
#include "core/kernel/ipc/acc_service.hpp"
#include "core/filesystem/vfs.hpp"
#include "core/audio/null_audio_backend.hpp"
#include "core/gpu/null_backend.hpp"
#include <iostream>
#include <cstdlib>
#include <cstring>
#include <algorithm>
#include <memory>
#include <string>

#define NEMU_IPC_ASSERT(cond) do { \
    if (!(cond)) { \
        std::cerr << "[FAIL] Assertion failed: " #cond " at " \
                  << __FILE__ << ":" << __LINE__ << std::endl; \
        std::abort(); \
    } \
} while (0)

using namespace nemu;
using namespace nemu::core;
using namespace nemu::core::kernel;
using namespace nemu::core::kernel::ipc;

namespace {

constexpr vaddr_t kTlsBase = KProcess::DEFAULT_TLS_BASE; // 0x00C0000000
constexpr vaddr_t kIpcBuf = kTlsBase + ipc::IpcBufferOffsetTls; // TLS + 0x100

// Write an IPC request into the guest TLS command buffer.
void WriteRequest(memory::VirtualMemory& mem, u32 type, u32 x_id, const void* payload, size_t len) {
    u8 buf[ipc::IpcBufferSize]{};
    auto* b = buf;
    *reinterpret_cast<u32*>(b + 0x00) = type;
    *reinterpret_cast<u32*>(b + 0x10) = x_id;
    if (payload && len > 0) {
        const size_t capped = std::min(len, sizeof(buf) - 0x18);
        std::memcpy(b + 0x18, payload, capped);
    }
    // data_size at +0x08 (u64)
    *reinterpret_cast<u64*>(b + 0x08) = len;
    NEMU_IPC_ASSERT(mem.WriteBlock(kIpcBuf, buf, sizeof(buf)));
}

void WriteServiceNameRequest(memory::VirtualMemory& mem, u32 type, u32 x_id, const char* name) {
    char name_buf[8]{};
    std::strncpy(name_buf, name, sizeof(name_buf));
    WriteRequest(mem, type, x_id, name_buf, sizeof(name_buf));
}

// Read a primitive from the guest TLS command buffer reply.
template <typename T>
T ReadReply(memory::VirtualMemory& mem, size_t offset = static_cast<size_t>(ipc::IpcField::Payload)) {
    u8 buf[ipc::IpcBufferSize]{};
    NEMU_IPC_ASSERT(mem.ReadBlock(kIpcBuf, buf, sizeof(buf)));
    T out = 0;
    std::memcpy(&out, buf + offset, sizeof(T));
    return out;
}

// Drive an IPC service through SvcDispatcher::Dispatch (svcSendSyncRequest),
// returning the resulting X0 result code.
u32 SendSync(KProcess& proc, KThread& thread, Handle session_handle) {
    cpu::CpuState st;
    st.SetX(0, session_handle);
    SvcDispatcher::Dispatch(st, proc, thread, 0x21);
    return static_cast<u32>(st.GetX(0));
}

} // namespace

// ---------------------------------------------------------------------------
// Test 1: JIT SVC routing
// ---------------------------------------------------------------------------
void TestJitSvcRouting() {
    std::cout << "[TEST] JIT SVC routing ...\n";
    memory::VirtualMemory memory;
    const vaddr_t code = 0x0072000000ULL;
    NEMU_IPC_ASSERT(memory.Map(code, 0x1000, memory::MemoryPermission::All));

    // MOVZ X0, #7 ; SVC #0x2B
    const u32 program[] = {
        0xD28000E0,            // MOVZ X0, #7
        0xD4000561,            // SVC #0x2B
        0xD65F03C0,            // RET
    };
    memory.WriteBlock(code, program, sizeof(program));

    bool handler_called = false;
    u32 seen_svc = 0;
    u64 seen_x0_before = 0;

    cpu::jit::JitCompiler::SetSvcHandler(
        [&](cpu::CpuState& state, u32 svc_id) {
            handler_called = true;
            seen_svc = svc_id;
            seen_x0_before = state.GetX(0);
            state.SetX(0, 0x2B00); // route signal: handler ran
            state.SetX(1, 0xCAFE); // secondary side effect
        });

    cpu::CpuState st;
    st.Reset();
    st.pc = code;

    cpu::jit::JitCompiler jit;
    NEMU_IPC_ASSERT(jit.Execute(st, memory)); // compiles + executes block w/ SVC

    NEMU_IPC_ASSERT(handler_called && "JIT block must route SVC to handler");
    NEMU_IPC_ASSERT(seen_svc == 0x2B && "handler must receive the SVC id");
    NEMU_IPC_ASSERT(seen_x0_before == 7 && "handler must observe pre-SVC regs");
    NEMU_IPC_ASSERT(st.GetX(0) == 0x2B00 && "handler side-effect X0 must persist");
    NEMU_IPC_ASSERT(st.GetX(1) == 0xCAFE && "handler side-effect X1 must persist");
    NEMU_IPC_ASSERT(st.pc == code + 8 && "PC must advance past the SVC (+ two insns)");

    // Reset the static handler to avoid leaking into later tests.
    cpu::jit::JitCompiler::SetSvcHandler({});
    std::cout << "  PASSED.\n";
}

// ---------------------------------------------------------------------------
// Test 2: service registry + service registration
// ---------------------------------------------------------------------------
void TestRegistry() {
    std::cout << "[TEST] service registry ...\n";
    ServiceRegistry reg;

    NEMU_IPC_ASSERT(reg.Register(std::make_shared<SmService>()));
    NEMU_IPC_ASSERT(reg.Register(std::make_shared<TimeService>()));
    NEMU_IPC_ASSERT(reg.Register(std::make_shared<SetSysService>()));
    NEMU_IPC_ASSERT(reg.Register(std::make_shared<HidService>()));

    NEMU_IPC_ASSERT(reg.Count() == 4);
    NEMU_IPC_ASSERT(reg.IsRegistered("sm:"));
    NEMU_IPC_ASSERT(reg.IsRegistered("time:u"));
    NEMU_IPC_ASSERT(reg.IsRegistered("set:sys"));
    NEMU_IPC_ASSERT(reg.IsRegistered("hid"));
    NEMU_IPC_ASSERT(!reg.IsRegistered("nonexistent:"));

    // Duplicate registration must fail.
    NEMU_IPC_ASSERT(!reg.Register(std::make_shared<SmService>()));

    auto port = reg.CreatePort("time:u");
    NEMU_IPC_ASSERT(port.has_value() && *port && "CreatePort must resolve time:u");
    NEMU_IPC_ASSERT(port->get()->GetServiceName() == "time:u");

    NEMU_IPC_ASSERT(!reg.CreatePort("ghost:").has_value());
    std::cout << "  PASSED.\n";
}

// ---------------------------------------------------------------------------
// Test 3: sm: GetServiceHandle (via DispatchSyncRequest)
// ---------------------------------------------------------------------------
void TestSmGetServiceHandle() {
    std::cout << "[TEST] sm: GetServiceHandle ...\n";
    ServiceRegistry reg;
    reg.Register(std::make_shared<SmService>());
    reg.Register(std::make_shared<SetSysService>());

    auto proc = std::make_shared<KProcess>(1, "Ipctest");
    KThread thread(1, proc, 44, 0, KProcess::DEFAULT_STACK_TOP, kTlsBase);

    auto sm_session = std::make_shared<KClientSession>();
    sm_session->SetService(reg.Find("sm:"));
    NEMU_IPC_ASSERT(sm_session->GetService());

    // GetServiceHandle("set:sys")
    WriteServiceNameRequest(proc->GetVirtualMemory(),
                            static_cast<u32>(IpcCommandType::Request),
                            SmService::GetServiceHandle,
                            "set:sys");

    const u32 result = DispatchSyncRequest(*proc, thread, *sm_session, reg);
    NEMU_IPC_ASSERT(result == static_cast<u32>(IpcResult::Success));

    const Handle port_handle = ReadReply<Handle>(proc->GetVirtualMemory());
    NEMU_IPC_ASSERT(port_handle != InvalidHandle);
    auto port = proc->GetHandleTable().GetObject<KClientPort>(port_handle);
    NEMU_IPC_ASSERT(port && "GetServiceHandle must fabricate a valid port handle");
    NEMU_IPC_ASSERT(port->GetServiceName() == "set:sys");

    // Unknown service -> NotFound.
    WriteServiceNameRequest(proc->GetVirtualMemory(),
                            static_cast<u32>(IpcCommandType::Request),
                            SmService::GetServiceHandle,
                            "bogus:");
    const u32 miss = DispatchSyncRequest(*proc, thread, *sm_session, reg);
    NEMU_IPC_ASSERT(miss == static_cast<u32>(IpcResult::NotFound));
    std::cout << "  PASSED.\n";
}

// ---------------------------------------------------------------------------
// Test 4: time:u clock service via sm:
// ---------------------------------------------------------------------------
void TestTimeService() {
    std::cout << "[TEST] time:u clock ...\n";
    auto reg = std::make_shared<ServiceRegistry>();
    reg->Register(std::make_shared<SmService>());
    reg->Register(std::make_shared<TimeService>());
    SvcDispatcher::InitializeIpc(reg); // needed for the SVC-level IPC path

    auto proc = std::make_shared<KProcess>(2, "TimeTest");
    KThread thread(2, proc, 44, 0, KProcess::DEFAULT_STACK_TOP, kTlsBase);

    auto sm_session = std::make_shared<KClientSession>();
    sm_session->SetService(reg->Find("sm:"));

    // 1. GetServiceHandle("time:u")
    WriteServiceNameRequest(proc->GetVirtualMemory(), static_cast<u32>(IpcCommandType::Request),
                            SmService::GetServiceHandle, "time:u");
    NEMU_IPC_ASSERT(DispatchSyncRequest(*proc, thread, *sm_session, *reg) ==
                    static_cast<u32>(IpcResult::Success));
    const Handle time_port = ReadReply<Handle>(proc->GetVirtualMemory());
    NEMU_IPC_ASSERT(time_port != InvalidHandle);

    // 2. svcConnectToPort -> session handle (page-aligned out pointer)
    cpu::CpuState st;
    const vaddr_t out_ptr = 0x0081000000ULL;
    proc->GetVirtualMemory().Map(out_ptr, memory::VirtualMemory::PAGE_SIZE,
                                 memory::MemoryPermission::ReadWrite);
    st.SetX(0, out_ptr);
    st.SetX(1, time_port);
    SvcDispatcher::Dispatch(st, *proc, thread, 0x2B);
    NEMU_IPC_ASSERT(st.GetX(0) == static_cast<u64>(Result::Success));
    Handle time_session = InvalidHandle;
    proc->GetVirtualMemory().ReadBlock(out_ptr, &time_session, sizeof(time_session));
    NEMU_IPC_ASSERT(time_session != InvalidHandle);

    // 3. time:u GetStandardUserSystemClock -> clock subservice handle
    WriteRequest(proc->GetVirtualMemory(), static_cast<u32>(IpcCommandType::Request),
                 TimeService::GetStandardUserSystemClock, nullptr, 0);
    NEMU_IPC_ASSERT(SendSync(*proc, thread, time_session) == static_cast<u32>(IpcResult::Success));
    const Handle clock_session = ReadReply<Handle>(proc->GetVirtualMemory());
    NEMU_IPC_ASSERT(clock_session != InvalidHandle);
    auto clock = proc->GetHandleTable().GetObject<KClientSession>(clock_session);
    NEMU_IPC_ASSERT(clock && "clock subservice handle must resolve to a session");
    NEMU_IPC_ASSERT(clock->GetServiceName() != "time:u");

    // 4. GetCurrentTime on the clock subservice.
    WriteRequest(proc->GetVirtualMemory(), static_cast<u32>(IpcCommandType::Request),
                 TimeClockService::GetCurrentTime, nullptr, 0);
    NEMU_IPC_ASSERT(SendSync(*proc, thread, clock_session) == static_cast<u32>(IpcResult::Success));
    const u64 now_ns = ReadReply<u64>(proc->GetVirtualMemory());
    NEMU_IPC_ASSERT(now_ns > 0 && "clock must return a nonzero timestamp");

    std::cout << "  PASSED.\n";
}

// ---------------------------------------------------------------------------
// Test 5: set:sys firmware / language via DispatchSyncRequest
// ---------------------------------------------------------------------------
void TestSetSys() {
    std::cout << "[TEST] set:sys ...\n";
    ServiceRegistry reg;
    reg.Register(std::make_shared<SmService>());
    reg.Register(std::make_shared<SetSysService>());

    auto proc = std::make_shared<KProcess>(3, "SetTest");
    KThread thread(3, proc, 44, 0, KProcess::DEFAULT_STACK_TOP, kTlsBase);

    auto set_session = std::make_shared<KClientSession>();
    set_session->SetService(reg.Find("set:sys"));

    // GetLanguageCode -> u32
    WriteRequest(proc->GetVirtualMemory(), static_cast<u32>(IpcCommandType::Request),
                 SetSysService::GetLanguageCode, nullptr, 0);
    NEMU_IPC_ASSERT(DispatchSyncRequest(*proc, thread, *set_session, reg) ==
                    static_cast<u32>(IpcResult::Success));
    NEMU_IPC_ASSERT(ReadReply<u32>(proc->GetVirtualMemory()) == 0x656E);

    // GetColorSetId -> u32
    WriteRequest(proc->GetVirtualMemory(), static_cast<u32>(IpcCommandType::Request),
                 SetSysService::GetColorSetId, nullptr, 0);
    NEMU_IPC_ASSERT(DispatchSyncRequest(*proc, thread, *set_session, reg) ==
                    static_cast<u32>(IpcResult::Success));
    NEMU_IPC_ASSERT(ReadReply<u32>(proc->GetVirtualMemory()) == 2);

    // GetFirmwareVersion -> 0x100 bytes, major == 1
    WriteRequest(proc->GetVirtualMemory(), static_cast<u32>(IpcCommandType::Request),
                 SetSysService::GetFirmwareVersion, nullptr, 0);
    NEMU_IPC_ASSERT(DispatchSyncRequest(*proc, thread, *set_session, reg) ==
                    static_cast<u32>(IpcResult::Success));
    u8 ver[0x100]{};
    NEMU_IPC_ASSERT(proc->GetVirtualMemory().ReadBlock(kIpcBuf + 0x18, ver, sizeof(ver)));
    NEMU_IPC_ASSERT(ver[0] == 1 && "firmware major");
    NEMU_IPC_ASSERT(ver[1] == 0 && "firmware minor");
    NEMU_IPC_ASSERT(std::string(reinterpret_cast<char*>(ver + 8)).substr(0, 4) == "1.0.");

    std::cout << "  PASSED.\n";
}

// ---------------------------------------------------------------------------
// Test 6: hid shared memory + button state
// ---------------------------------------------------------------------------
void TestHidService() {
    std::cout << "[TEST] hid input shared memory ...\n";
    ServiceRegistry reg;
    reg.Register(std::make_shared<SmService>());
    reg.Register(std::make_shared<HidService>());

    auto proc = std::make_shared<KProcess>(4, "HidTest");
    KThread thread(4, proc, 44, 0, KProcess::DEFAULT_STACK_TOP, kTlsBase);

    auto hid_session = std::make_shared<KClientSession>();
    hid_session->SetService(reg.Find("hid"));

    // GetSharedMemoryHandle
    WriteRequest(proc->GetVirtualMemory(), static_cast<u32>(IpcCommandType::Request),
                 HidService::GetSharedMemoryHandle, nullptr, 0);
    NEMU_IPC_ASSERT(DispatchSyncRequest(*proc, thread, *hid_session, reg) ==
                    static_cast<u32>(IpcResult::Success));
    const Handle shmem_handle = ReadReply<Handle>(proc->GetVirtualMemory());
    NEMU_IPC_ASSERT(shmem_handle != InvalidHandle);

    auto shmem = proc->GetHandleTable().GetObject<KSharedMemory>(shmem_handle);
    NEMU_IPC_ASSERT(shmem && "shared memory handle must resolve to KSharedMemory");
    const vaddr_t shmem_addr = shmem->GetAddress();
    NEMU_IPC_ASSERT(shmem_addr != 0);

    const auto hid = std::static_pointer_cast<HidService>(reg.Find("hid"));
    NEMU_IPC_ASSERT(hid->IsSharedMemoryReady());
    NEMU_IPC_ASSERT(hid->GetSharedAddress() == shmem_addr);

    // SetButtonState(A) via HLE bridge
    const u32 buttons = static_cast<u32>(hid::HidButton::A);
    WriteRequest(proc->GetVirtualMemory(), static_cast<u32>(IpcCommandType::Request),
                 HidService::SetButtonState, &buttons, sizeof(buttons));
    NEMU_IPC_ASSERT(DispatchSyncRequest(*proc, thread, *hid_session, reg) ==
                    static_cast<u32>(IpcResult::Success));

    // Guest should observe the shared pad entry.
    hid::SharedPadHeader header{};
    NEMU_IPC_ASSERT(proc->GetVirtualMemory().ReadBlock(shmem_addr, &header, sizeof(header)));
    NEMU_IPC_ASSERT(header.entry_count == 2);
    NEMU_IPC_ASSERT(header.local.buttons == buttons);
    NEMU_IPC_ASSERT(header.global.buttons == buttons);
    NEMU_IPC_ASSERT(header.local.timestamp >= 1);

    std::cout << "  PASSED.\n";
}

// ---------------------------------------------------------------------------
// Test 7: nvdrv GPU Driver Service
// ---------------------------------------------------------------------------
void TestNvDrvService() {
    std::cout << "[TEST] nvdrv GPU driver service ...\n";
    auto backend = std::make_shared<gpu::NullGpuBackend>();
    backend->Initialize(1280, 720);
    auto maxwell = std::make_shared<gpu::Maxwell3D>(backend);

    auto proc = std::make_shared<KProcess>(5, "NvDrvTest");
    KThread thread(5, proc, 45, 0, KProcess::DEFAULT_STACK_TOP, kTlsBase);

    auto dev_mgr = std::make_shared<gpu::nvhost::NvDeviceManager>(maxwell, &proc->GetVirtualMemory());
    ServiceRegistry reg;
    reg.Register(std::make_shared<NvDrvService>("nvdrv:a", dev_mgr));

    auto session = std::make_shared<KClientSession>();
    session->SetService(reg.Find("nvdrv:a"));

    // 1. Initialize (cmd 3)
    WriteRequest(proc->GetVirtualMemory(), static_cast<u32>(IpcCommandType::Request), 3, nullptr, 0);
    NEMU_IPC_ASSERT(DispatchSyncRequest(*proc, thread, *session, reg) == static_cast<u32>(IpcResult::Success));

    // 2. Open /dev/nvmap (cmd 0)
    char path[] = "/dev/nvmap";
    WriteRequest(proc->GetVirtualMemory(), static_cast<u32>(IpcCommandType::Request), 0, path, sizeof(path));
    NEMU_IPC_ASSERT(DispatchSyncRequest(*proc, thread, *session, reg) == static_cast<u32>(IpcResult::Success));
    const s32 fd = static_cast<s32>(ReadReply<u32>(proc->GetVirtualMemory(), static_cast<size_t>(ipc::IpcField::Payload)));
    NEMU_IPC_ASSERT(fd > 0);

    // 3. Ioctl NVMAP_IOC_CREATE (cmd 1)
    struct { u32 fd; u32 cmd; u32 size; u32 handle; } ioctl_args{static_cast<u32>(fd), 0xC0180101, 0x10000, 0};
    WriteRequest(proc->GetVirtualMemory(), static_cast<u32>(IpcCommandType::Request), 1, &ioctl_args, sizeof(ioctl_args));
    NEMU_IPC_ASSERT(DispatchSyncRequest(*proc, thread, *session, reg) == static_cast<u32>(IpcResult::Success));
    const u32 created_handle = ReadReply<u32>(proc->GetVirtualMemory(), static_cast<size_t>(ipc::IpcField::Payload) + 12);
    NEMU_IPC_ASSERT(created_handle != 0);

    // 4. Close (cmd 2)
    WriteRequest(proc->GetVirtualMemory(), static_cast<u32>(IpcCommandType::Request), 2, &fd, sizeof(fd));
    NEMU_IPC_ASSERT(DispatchSyncRequest(*proc, thread, *session, reg) == static_cast<u32>(IpcResult::Success));

    backend->Shutdown();
    std::cout << "  PASSED.\n";
}

// ---------------------------------------------------------------------------
// Test 8: vi Display & Presentation Service
// ---------------------------------------------------------------------------
void TestViService() {
    std::cout << "[TEST] vi display service ...\n";
    auto backend = std::make_shared<gpu::NullGpuBackend>();
    backend->Initialize(1280, 720);
    auto flinger = std::make_shared<gpu::presentation::Nvnflinger>(backend);

    auto proc = std::make_shared<KProcess>(6, "ViTest");
    KThread thread(6, proc, 46, 0, KProcess::DEFAULT_STACK_TOP, kTlsBase);

    ServiceRegistry reg;
    reg.Register(std::make_shared<ViService>("vi:u", flinger));

    auto session = std::make_shared<KClientSession>();
    session->SetService(reg.Find("vi:u"));

    // 1. GetDisplayService (cmd 0)
    WriteRequest(proc->GetVirtualMemory(), static_cast<u32>(IpcCommandType::Request), 0, nullptr, 0);
    NEMU_IPC_ASSERT(DispatchSyncRequest(*proc, thread, *session, reg) == static_cast<u32>(IpcResult::Success));
    const Handle disp_srv_handle = ReadReply<Handle>(proc->GetVirtualMemory(), static_cast<size_t>(ipc::IpcField::Payload) + 4);
    NEMU_IPC_ASSERT(disp_srv_handle != InvalidHandle);

    auto disp_session = proc->GetHandleTable().GetObject<KClientSession>(disp_srv_handle);
    NEMU_IPC_ASSERT(disp_session != nullptr);

    // 2. OpenDisplay (cmd 101)
    char disp_name[] = "Default";
    WriteRequest(proc->GetVirtualMemory(), static_cast<u32>(IpcCommandType::Request), 101, disp_name, sizeof(disp_name));
    NEMU_IPC_ASSERT(DispatchSyncRequest(*proc, thread, *disp_session, reg) == static_cast<u32>(IpcResult::Success));
    const u64 disp_id = ReadReply<u64>(proc->GetVirtualMemory(), static_cast<size_t>(ipc::IpcField::Payload) + 4);
    NEMU_IPC_ASSERT(disp_id != 0);

    // 3. CreateStrayLayer (cmd 2030)
    WriteRequest(proc->GetVirtualMemory(), static_cast<u32>(IpcCommandType::Request), 2030, &disp_id, sizeof(disp_id));
    NEMU_IPC_ASSERT(DispatchSyncRequest(*proc, thread, *disp_session, reg) == static_cast<u32>(IpcResult::Success));
    const u64 layer_id = ReadReply<u64>(proc->GetVirtualMemory(), static_cast<size_t>(ipc::IpcField::Payload) + 4);
    const u32 binder_id = ReadReply<u32>(proc->GetVirtualMemory(), static_cast<size_t>(ipc::IpcField::Payload) + 12);
    NEMU_IPC_ASSERT(layer_id != 0 && binder_id != 0);

    // 4. GetRelayService (cmd 100)
    WriteRequest(proc->GetVirtualMemory(), static_cast<u32>(IpcCommandType::Request), 100, nullptr, 0);
    NEMU_IPC_ASSERT(DispatchSyncRequest(*proc, thread, *disp_session, reg) == static_cast<u32>(IpcResult::Success));
    const Handle relay_handle = ReadReply<Handle>(proc->GetVirtualMemory(), static_cast<size_t>(ipc::IpcField::Payload) + 4);
    NEMU_IPC_ASSERT(relay_handle != InvalidHandle);

    auto binder_session = proc->GetHandleTable().GetObject<KClientSession>(relay_handle);
    NEMU_IPC_ASSERT(binder_session != nullptr);

    // 5. TransactParcel DequeueBuffer (code 3)
    struct { u32 binder_id; u32 code; } parcel_args{binder_id, 3};
    WriteRequest(proc->GetVirtualMemory(), static_cast<u32>(IpcCommandType::Request), 0, &parcel_args, sizeof(parcel_args));
    NEMU_IPC_ASSERT(DispatchSyncRequest(*proc, thread, *binder_session, reg) == static_cast<u32>(IpcResult::Success));
    const s32 slot = static_cast<s32>(ReadReply<u32>(proc->GetVirtualMemory(), static_cast<size_t>(ipc::IpcField::Payload) + 4));
    NEMU_IPC_ASSERT(slot >= 0 && slot < 64);

    // 6. TransactParcel QueueBuffer (code 4)
    struct { u32 binder_id; u32 code; u32 slot; } queue_args{binder_id, 4, static_cast<u32>(slot)};
    WriteRequest(proc->GetVirtualMemory(), static_cast<u32>(IpcCommandType::Request), 0, &queue_args, sizeof(queue_args));
    NEMU_IPC_ASSERT(DispatchSyncRequest(*proc, thread, *binder_session, reg) == static_cast<u32>(IpcResult::Success));

    // 7. Compose and present frame via flinger
    NEMU_IPC_ASSERT(flinger->ComposeAndPresent());
    NEMU_IPC_ASSERT(flinger->GetTotalFramesPresented() == 1);

    backend->Shutdown();
    std::cout << "  PASSED.\n";
}

void TestFspSrvService() {
    std::cout << "[TEST] fsp-srv filesystem proxy ...\n";
    auto vfs = std::make_shared<filesystem::VirtualFileSystem>();
    const auto temp_dir = std::filesystem::temp_directory_path() / "nemu_test_fsp";
    std::filesystem::create_directories(temp_dir);
    vfs->Mount("sdmc:/", temp_dir);

    ServiceRegistry reg;
    reg.Register(std::make_shared<FspSrvService>(vfs));

    auto proc = std::make_shared<KProcess>(1, "Ipctest");
    KThread thread(1, proc, 44, 0, KProcess::DEFAULT_STACK_TOP, kTlsBase);

    auto port = reg.CreatePort("fsp-srv");
    NEMU_IPC_ASSERT(port.has_value());
    auto session = std::make_shared<KClientSession>();
    session->SetService((*port)->GetService());
    const Handle fsp_handle = proc->GetHandleTable().CreateHandle(session);
    (void)fsp_handle;

    // 1. SetCurrentProcess (cmd 1)
    WriteRequest(proc->GetVirtualMemory(), static_cast<u32>(IpcCommandType::Request), 1, nullptr, 0);
    NEMU_IPC_ASSERT(DispatchSyncRequest(*proc, thread, *session, reg) == static_cast<u32>(IpcResult::Success));

    // 2. OpenSdCardFileSystem (cmd 101 / 0x65)
    WriteRequest(proc->GetVirtualMemory(), static_cast<u32>(IpcCommandType::Request), 0x65, nullptr, 0);
    NEMU_IPC_ASSERT(DispatchSyncRequest(*proc, thread, *session, reg) == static_cast<u32>(IpcResult::Success));
    const Handle sd_handle = ReadReply<Handle>(proc->GetVirtualMemory(), static_cast<size_t>(ipc::IpcField::Payload) + 4);
    NEMU_IPC_ASSERT(sd_handle != InvalidHandle);

    auto sd_session = std::dynamic_pointer_cast<KClientSession>(proc->GetHandleTable().GetObject(sd_handle));
    NEMU_IPC_ASSERT(sd_session != nullptr);

    // 3. CreateFile on SD (cmd 0)
    char fname[64]{};
    std::strncpy(fname + 8, "test.txt", 16);
    WriteRequest(proc->GetVirtualMemory(), static_cast<u32>(IpcCommandType::Request), 0, fname, sizeof(fname));
    NEMU_IPC_ASSERT(DispatchSyncRequest(*proc, thread, *sd_session, reg) == static_cast<u32>(IpcResult::Success));

    // 4. GetEntryType (cmd 11)
    char check_name[64]{};
    std::strncpy(check_name, "test.txt", 16);
    WriteRequest(proc->GetVirtualMemory(), static_cast<u32>(IpcCommandType::Request), 11, check_name, sizeof(check_name));
    NEMU_IPC_ASSERT(DispatchSyncRequest(*proc, thread, *sd_session, reg) == static_cast<u32>(IpcResult::Success));
    const u32 entry_type = ReadReply<u32>(proc->GetVirtualMemory(), static_cast<size_t>(ipc::IpcField::Payload) + 4);
    NEMU_IPC_ASSERT(entry_type == 1); // 1 = file

    std::filesystem::remove_all(temp_dir);
    std::cout << "  PASSED.\n";
}

void TestAudrenService() {
    std::cout << "[TEST] audren:u audio renderer ...\n";
    auto backend = std::make_shared<audio::NullAudioBackend>();
    backend->Initialize(48000, 2);

    ServiceRegistry reg;
    reg.Register(std::make_shared<AudrenManagerService>(backend));

    auto proc = std::make_shared<KProcess>(1, "Ipctest");
    KThread thread(1, proc, 44, 0, KProcess::DEFAULT_STACK_TOP, kTlsBase);

    auto port = reg.CreatePort("audren:u");
    NEMU_IPC_ASSERT(port.has_value());
    auto session = std::make_shared<KClientSession>();
    session->SetService((*port)->GetService());

    // 1. OpenAudioRenderer (cmd 0)
    struct { u32 rate; u32 count; } ren_args{48000, 160};
    WriteRequest(proc->GetVirtualMemory(), static_cast<u32>(IpcCommandType::Request), 0, &ren_args, sizeof(ren_args));
    NEMU_IPC_ASSERT(DispatchSyncRequest(*proc, thread, *session, reg) == static_cast<u32>(IpcResult::Success));
    const Handle ren_handle = ReadReply<Handle>(proc->GetVirtualMemory(), static_cast<size_t>(ipc::IpcField::Payload) + 4);
    NEMU_IPC_ASSERT(ren_handle != InvalidHandle);

    auto ren_session = std::dynamic_pointer_cast<KClientSession>(proc->GetHandleTable().GetObject(ren_handle));
    NEMU_IPC_ASSERT(ren_session != nullptr);

    // 2. Start (cmd 5)
    WriteRequest(proc->GetVirtualMemory(), static_cast<u32>(IpcCommandType::Request), 5, nullptr, 0);
    NEMU_IPC_ASSERT(DispatchSyncRequest(*proc, thread, *ren_session, reg) == static_cast<u32>(IpcResult::Success));

    // 3. RequestUpdate (cmd 4)
    WriteRequest(proc->GetVirtualMemory(), static_cast<u32>(IpcCommandType::Request), 4, nullptr, 0);
    NEMU_IPC_ASSERT(DispatchSyncRequest(*proc, thread, *ren_session, reg) == static_cast<u32>(IpcResult::Success));
    const u64 rendered = ReadReply<u64>(proc->GetVirtualMemory(), static_cast<size_t>(ipc::IpcField::Payload) + 4);
    NEMU_IPC_ASSERT(rendered == 160);

    // 4. Stop (cmd 6)
    WriteRequest(proc->GetVirtualMemory(), static_cast<u32>(IpcCommandType::Request), 6, nullptr, 0);
    NEMU_IPC_ASSERT(DispatchSyncRequest(*proc, thread, *ren_session, reg) == static_cast<u32>(IpcResult::Success));

    backend->Shutdown();
    std::cout << "  PASSED.\n";
}

void TestAppletService() {
    std::cout << "[TEST] appletOE application service ...\n";
    ServiceRegistry reg;
    reg.Register(std::make_shared<AppletManagerService>("appletOE"));

    auto proc = std::make_shared<KProcess>(1, "Ipctest");
    KThread thread(1, proc, 44, 0, KProcess::DEFAULT_STACK_TOP, kTlsBase);

    auto port = reg.CreatePort("appletOE");
    NEMU_IPC_ASSERT(port.has_value());
    auto session = std::make_shared<KClientSession>();
    session->SetService((*port)->GetService());

    // 1. OpenSession (cmd 0)
    WriteRequest(proc->GetVirtualMemory(), static_cast<u32>(IpcCommandType::Request), 0, nullptr, 0);
    NEMU_IPC_ASSERT(DispatchSyncRequest(*proc, thread, *session, reg) == static_cast<u32>(IpcResult::Success));
    const Handle sess_h = ReadReply<Handle>(proc->GetVirtualMemory(), static_cast<size_t>(ipc::IpcField::Payload) + 4);
    NEMU_IPC_ASSERT(sess_h != InvalidHandle);

    auto applet_sess = std::dynamic_pointer_cast<KClientSession>(proc->GetHandleTable().GetObject(sess_h));
    NEMU_IPC_ASSERT(applet_sess != nullptr);

    // 2. OpenApplicationFunctions (cmd 20)
    WriteRequest(proc->GetVirtualMemory(), static_cast<u32>(IpcCommandType::Request), 20, nullptr, 0);
    NEMU_IPC_ASSERT(DispatchSyncRequest(*proc, thread, *applet_sess, reg) == static_cast<u32>(IpcResult::Success));
    const Handle funcs_h = ReadReply<Handle>(proc->GetVirtualMemory(), static_cast<size_t>(ipc::IpcField::Payload) + 4);
    NEMU_IPC_ASSERT(funcs_h != InvalidHandle);

    auto funcs_sess = std::dynamic_pointer_cast<KClientSession>(proc->GetHandleTable().GetObject(funcs_h));
    NEMU_IPC_ASSERT(funcs_sess != nullptr);

    // 3. NotifyRunning (cmd 20 on funcs)
    WriteRequest(proc->GetVirtualMemory(), static_cast<u32>(IpcCommandType::Request), 20, nullptr, 0);
    NEMU_IPC_ASSERT(DispatchSyncRequest(*proc, thread, *funcs_sess, reg) == static_cast<u32>(IpcResult::Success));
    const u32 running = ReadReply<u32>(proc->GetVirtualMemory(), static_cast<size_t>(ipc::IpcField::Payload) + 4);
    NEMU_IPC_ASSERT(running == 1);

    std::cout << "  PASSED.\n";
}

void TestAccountService() {
    std::cout << "[TEST] acc:u0 account service ...\n";
    ServiceRegistry reg;
    reg.Register(std::make_shared<AccountService>());

    auto proc = std::make_shared<KProcess>(1, "Ipctest");
    KThread thread(1, proc, 44, 0, KProcess::DEFAULT_STACK_TOP, kTlsBase);

    auto port = reg.CreatePort("acc:u0");
    NEMU_IPC_ASSERT(port.has_value());
    auto session = std::make_shared<KClientSession>();
    session->SetService((*port)->GetService());

    // 1. GetUserCount (cmd 0)
    WriteRequest(proc->GetVirtualMemory(), static_cast<u32>(IpcCommandType::Request), 0, nullptr, 0);
    NEMU_IPC_ASSERT(DispatchSyncRequest(*proc, thread, *session, reg) == static_cast<u32>(IpcResult::Success));
    const u32 count = ReadReply<u32>(proc->GetVirtualMemory(), static_cast<size_t>(ipc::IpcField::Payload) + 4);
    NEMU_IPC_ASSERT(count == 1);

    // 2. ListOpenUsers (cmd 2)
    WriteRequest(proc->GetVirtualMemory(), static_cast<u32>(IpcCommandType::Request), 2, nullptr, 0);
    NEMU_IPC_ASSERT(DispatchSyncRequest(*proc, thread, *session, reg) == static_cast<u32>(IpcResult::Success));
    const u64 uid_low = ReadReply<u64>(proc->GetVirtualMemory(), static_cast<size_t>(ipc::IpcField::Payload) + 4);
    NEMU_IPC_ASSERT(uid_low == 1);

    std::cout << "  PASSED.\n";
}

int main() {
    std::cout << "========================================\n";
    std::cout << "   NEMU HORIZON OS IPC ENGINE TESTS     \n";
    std::cout << "========================================\n";

    TestJitSvcRouting();
    TestRegistry();
    TestSmGetServiceHandle();
    TestTimeService();
    TestSetSys();
    TestHidService();
    TestNvDrvService();
    TestViService();
    TestFspSrvService();
    TestAudrenService();
    TestAppletService();
    TestAccountService();

    std::cout << "ALL IPC TESTS PASSED SUCCESSFULLY!\n";
    return 0;
}