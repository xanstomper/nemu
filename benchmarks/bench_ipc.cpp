#include "bench.hpp"
#include "core/cpu/cpu_state.hpp"
#include "core/memory/virtual_memory.hpp"
#include "core/kernel/k_process.hpp"
#include "core/kernel/k_thread.hpp"
#include "core/kernel/svc.hpp"
#include "core/kernel/ipc/service_registry.hpp"
#include "core/kernel/ipc/sm_service.hpp"
#include "core/kernel/ipc/time_service.hpp"
#include "core/kernel/ipc/ipc_dispatcher.hpp"
#include "core/kernel/ipc/ipc_types.hpp"
#include <iostream>
#include <memory>
#include <cstring>

using namespace nemu;
using namespace nemu::core;
using namespace nemu::core::kernel;
using namespace nemu::core::kernel::ipc;

int main() {
    std::cout << "=========================================================" << std::endl;
    std::cout << "      NEMU BENCHMARK: Horizon OS IPC Roundtrip Latency   " << std::endl;
    std::cout << "=========================================================" << std::endl;

    auto reg = std::make_shared<ServiceRegistry>();
    reg->Register(std::make_shared<SmService>());
    reg->Register(std::make_shared<TimeService>());
    SvcDispatcher::InitializeIpc(reg);

    auto proc = std::make_shared<KProcess>(1, "BenchProc");
    const vaddr_t TLS_BASE = KProcess::DEFAULT_TLS_BASE;
    KThread thread(1, proc, 44, 0, KProcess::DEFAULT_STACK_TOP, TLS_BASE);

    // Setup SM session
    auto sm_session = std::make_shared<KClientSession>();
    sm_session->SetService(reg->Find("sm:"));

    // Populate TLS buffer with GetStandardUserSystemClock command on time:u
    const vaddr_t ipc_buf = TLS_BASE + ipc::IpcBufferOffsetTls;
    u8 raw_buf[ipc::IpcBufferSize]{};
    *reinterpret_cast<u32*>(raw_buf + 0x00) = static_cast<u32>(IpcCommandType::Request);
    *reinterpret_cast<u32*>(raw_buf + 0x10) = TimeService::GetStandardUserSystemClock;
    proc->GetVirtualMemory().WriteBlock(ipc_buf, raw_buf, sizeof(raw_buf));

    constexpr u64 REQUESTS = 500'000;

    {
        bench::BenchmarkTimer timer("svcSendSyncRequest (TimeService::GetCurrentTime)", REQUESTS);

        for (u64 i = 0; i < REQUESTS; ++i) {
            DispatchSyncRequest(*proc, thread, *sm_session, *reg);
        }
    }

    std::cout << "=========================================================" << std::endl;
    return 0;
}
