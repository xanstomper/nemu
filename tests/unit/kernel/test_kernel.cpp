#include "core/kernel/k_handle_table.hpp"
#include "core/kernel/k_process.hpp"
#include "core/kernel/k_thread.hpp"
#include "core/kernel/k_event.hpp"
#include "core/kernel/svc.hpp"
#include "core/cpu/interpreter.hpp"
#include <iostream>
#include <cstdlib>

using namespace nemu;
using namespace nemu::core;
using namespace nemu::core::kernel;

#define NEMU_TEST_ASSERT(cond) do { \
    if (!(cond)) { \
        std::cerr << "[FAIL] Assertion failed: " #cond " at " << __FILE__ << ":" << __LINE__ << std::endl; \
        std::abort(); \
    } \
} while (0)

void TestHandleTable() {
    std::cout << "[TEST] Running TestHandleTable...\n";
    KHandleTable table;

    auto ev1 = std::make_shared<KEvent>(true);
    auto ev2 = std::make_shared<KEvent>(false);

    const Handle h1 = table.CreateHandle(ev1);
    const Handle h2 = table.CreateHandle(ev2);

    NEMU_TEST_ASSERT(h1 != InvalidHandle);
    NEMU_TEST_ASSERT(h2 != InvalidHandle);
    NEMU_TEST_ASSERT(h1 != h2);
    NEMU_TEST_ASSERT(table.IsValid(h1));
    NEMU_TEST_ASSERT(table.IsValid(h2));

    auto retrieved_ev1 = table.GetObject<KEvent>(h1);
    NEMU_TEST_ASSERT(retrieved_ev1 == ev1);

    // Cast to wrong type should return nullptr
    auto invalid_cast = table.GetObject<KProcess>(h1);
    NEMU_TEST_ASSERT(invalid_cast == nullptr);

    // Close handle
    NEMU_TEST_ASSERT(table.CloseHandle(h1));
    NEMU_TEST_ASSERT(!table.IsValid(h1));
    NEMU_TEST_ASSERT(table.GetObject(h1) == nullptr);

    // Double close should fail
    NEMU_TEST_ASSERT(!table.CloseHandle(h1));

    std::cout << "  PASSED.\n";
}

void TestDynamicHeap() {
    std::cout << "[TEST] Running TestDynamicHeap...\n";
    KProcess proc(1, "TestProc");

    NEMU_TEST_ASSERT(proc.GetHeapSize() == 0);

    // Expand heap to 64 KiB
    const size_t heap_req = 64 * 1024;
    const vaddr_t heap_addr = proc.SetHeapSize(heap_req);

    NEMU_TEST_ASSERT(heap_addr == KProcess::DEFAULT_HEAP_BASE);
    NEMU_TEST_ASSERT(proc.GetHeapSize() == heap_req);

    // Heap should be writable and readable
    auto& vmem = proc.GetVirtualMemory();
    NEMU_TEST_ASSERT(vmem.IsValidAddress(heap_addr, heap_req));

    vmem.Write32(heap_addr, 0x12345678);
    NEMU_TEST_ASSERT(vmem.Read32(heap_addr) == 0x12345678);

    // Shrink heap
    proc.SetHeapSize(0);
    NEMU_TEST_ASSERT(proc.GetHeapSize() == 0);
    NEMU_TEST_ASSERT(!vmem.IsValidAddress(heap_addr, 4));

    std::cout << "  PASSED.\n";
}

void TestSynchronizationEvent() {
    std::cout << "[TEST] Running TestSynchronizationEvent...\n";
    KEvent event(true); // auto-clear

    NEMU_TEST_ASSERT(!event.IsSignaled());
    event.Signal();
    NEMU_TEST_ASSERT(event.IsSignaled());

    // Wait should succeed and auto-clear
    NEMU_TEST_ASSERT(event.Wait(std::chrono::milliseconds(10)));
    NEMU_TEST_ASSERT(!event.IsSignaled());

    // Immediate wait when unsignaled should return false
    NEMU_TEST_ASSERT(!event.Wait(std::chrono::nanoseconds::zero()));

    std::cout << "  PASSED.\n";
}

void TestSvcExecution() {
    std::cout << "[TEST] Running TestSvcExecution...\n";
    auto proc = std::make_shared<KProcess>(2, "SvcTestProc");

    // Map code page
    const vaddr_t code_addr = 0x0071000000ULL;
    NEMU_TEST_ASSERT(proc->GetVirtualMemory().Map(code_addr, 0x1000, memory::MemoryPermission::All));

    // Guest program:
    // 1. MOVZ X1, #0x4000, LSL #0   (0xD2880001) -> size = 16384
    // 2. SVC  #0x01                 (0xD4000021) -> svcSetHeapSize
    // 3. SVC  #0x07                 (0xD40000E1) -> svcExitProcess
    const u32 code[] = {
        0xD2880001,
        0xD4000021,
        0xD40000E1
    };
    NEMU_TEST_ASSERT(proc->GetVirtualMemory().WriteBlock(code_addr, code, sizeof(code)));

    KThread thread(10, proc, 44, code_addr, KProcess::DEFAULT_STACK_TOP, KProcess::DEFAULT_TLS_BASE);
    cpu::Interpreter interp(thread.GetCpuState(), proc->GetVirtualMemory());

    interp.SetSvcHandler([&proc, &thread](cpu::CpuState& state, u32 svc_id) {
        SvcDispatcher::Dispatch(state, *proc, thread, svc_id);
    });

    // Step 1: MOVZ
    NEMU_TEST_ASSERT(interp.Step() == cpu::StepResult::Ok);
    NEMU_TEST_ASSERT(thread.GetCpuState().GetX(1) == 0x4000);

    // Step 2: SVC #0x01 (SetHeapSize)
    NEMU_TEST_ASSERT(interp.Step() == cpu::StepResult::Svc);
    NEMU_TEST_ASSERT(thread.GetCpuState().GetX(0) == static_cast<u64>(Result::Success));
    NEMU_TEST_ASSERT(thread.GetCpuState().GetX(1) == KProcess::DEFAULT_HEAP_BASE);
    NEMU_TEST_ASSERT(proc->GetHeapSize() == 0x4000);

    // Step 3: SVC #0x07 (ExitProcess)
    NEMU_TEST_ASSERT(interp.Step() == cpu::StepResult::Svc);
    NEMU_TEST_ASSERT(proc->GetState() == ProcessState::Terminated);
    NEMU_TEST_ASSERT(thread.GetCpuState().halted == true);

    std::cout << "  PASSED.\n";
}

int main() {
    std::cout << "========================================\n";
    std::cout << "    NEMU HORIZON KERNEL UNIT TESTS      \n";
    std::cout << "========================================\n";

    TestHandleTable();
    TestDynamicHeap();
    TestSynchronizationEvent();
    TestSvcExecution();

    std::cout << "ALL KERNEL UNIT TESTS PASSED SUCCESSFULLY!\n";
    return 0;
}
