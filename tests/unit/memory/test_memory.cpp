#include "core/memory/virtual_memory.hpp"
#include "core/memory/fastmem.hpp"
#include "core/memory/fastmem_exception_handler.hpp"
#include <iostream>
#include <cstdlib>
#include <vector>

using namespace nemu;
using namespace nemu::core::memory;

#define NEMU_TEST_ASSERT(cond) do { \
    if (!(cond)) { \
        std::cerr << "[FAIL] Assertion failed: " #cond " at " << __FILE__ << ":" << __LINE__ << std::endl; \
        std::abort(); \
    } \
} while (0)

void TestMappingAndPermissions() {
    std::cout << "[TEST] Running TestMappingAndPermissions...\n";
    VirtualMemory mem;

    const vaddr_t page_va = 0x00010000;
    const size_t page_sz = 0x1000; // 4 KiB

    NEMU_TEST_ASSERT(mem.Map(page_va, page_sz, MemoryPermission::Read) == true);
    NEMU_TEST_ASSERT(mem.IsValidAddress(page_va, page_sz) == true);
    NEMU_TEST_ASSERT(mem.IsValidAddress(page_va + page_sz, 1) == false);

    // Read should return 0 (zero-initialized)
    NEMU_TEST_ASSERT(mem.Read8(page_va) == 0);

    // Write should fail because it's Read-only
    mem.Write8(page_va, 0x42);
    NEMU_TEST_ASSERT(mem.Read8(page_va) == 0);

    // Reprotect to ReadWrite
    NEMU_TEST_ASSERT(mem.Reprotect(page_va, page_sz, MemoryPermission::ReadWrite) == true);
    mem.Write8(page_va, 0x42);
    NEMU_TEST_ASSERT(mem.Read8(page_va) == 0x42);

    // Unmap
    NEMU_TEST_ASSERT(mem.Unmap(page_va, page_sz) == true);
    NEMU_TEST_ASSERT(mem.IsValidAddress(page_va) == false);

    std::cout << "  PASSED.\n";
}

void TestBlockAndBoundary() {
    std::cout << "[TEST] Running TestBlockAndBoundary...\n";
    VirtualMemory mem;

    const vaddr_t va = 0x00020000;
    const size_t sz = 0x3000; // 3 pages (12 KiB)
    NEMU_TEST_ASSERT(mem.Map(va, sz, MemoryPermission::All) == true);

    // Write 64-bit integer across page boundary: 0x00020FFC (spans page 0 and page 1)
    const u64 test_val = 0x1122334455667788ULL;
    const vaddr_t boundary_addr = 0x00020FFC;
    mem.Write64(boundary_addr, test_val);

    const u64 read_back = mem.Read64(boundary_addr);
    NEMU_TEST_ASSERT(read_back == test_val);

    // Test block transfer
    std::vector<u8> src_buf(5000);
    for (size_t i = 0; i < src_buf.size(); ++i) {
        src_buf[i] = static_cast<u8>(i & 0xFF);
    }

    NEMU_TEST_ASSERT(mem.WriteBlock(va, src_buf.data(), src_buf.size()) == true);

    std::vector<u8> dst_buf(5000, 0);
    NEMU_TEST_ASSERT(mem.ReadBlock(va, dst_buf.data(), dst_buf.size()) == true);
    NEMU_TEST_ASSERT(src_buf == dst_buf);

    std::cout << "  PASSED.\n";
}

void TestFastmemAndTiers() {
    std::cout << "[TEST] Running TestFastmemAndTiers...\n";
    auto& fm = FastmemManager::Instance();

    // 1. Test 4GB retail initialization
    NEMU_TEST_ASSERT(fm.Initialize(MemoryTier::Retail4GB));
    NEMU_TEST_ASSERT(fm.IsEnabled());
    NEMU_TEST_ASSERT(fm.GetDramSize() == FastmemManager::SIZE_4GB);
    NEMU_TEST_ASSERT(fm.GetBase() != nullptr);

    const vaddr_t test_va = 0x80000000ULL; // 2 GiB heap offset
    const size_t test_sz = 0x10000;         // 64 KiB

    // Commit ReadWrite
    NEMU_TEST_ASSERT(fm.Commit(test_va, test_sz, MemoryPermission::ReadWrite));
    NEMU_TEST_ASSERT(fm.IsValidRange(test_va, test_sz));

    // Direct host pointer access
    u8* host_ptr = fm.GetPointer(test_va);
    NEMU_TEST_ASSERT(host_ptr != nullptr);

    // Direct memory write and readback
    const u64 sample = 0xFEEDC0FFEE010203ULL;
    *reinterpret_cast<u64*>(host_ptr) = sample;
    NEMU_TEST_ASSERT(*reinterpret_cast<volatile u64*>(host_ptr) == sample);

    // Change protection
    NEMU_TEST_ASSERT(fm.Protect(test_va, test_sz, MemoryPermission::Read));

    // Decommit and shutdown
    NEMU_TEST_ASSERT(fm.Decommit(test_va, test_sz));
    fm.Shutdown();
    NEMU_TEST_ASSERT(!fm.IsEnabled());
    NEMU_TEST_ASSERT(fm.GetBase() == nullptr);

    // 2. Test 6GB OLED tier initialization
    NEMU_TEST_ASSERT(fm.Initialize(MemoryTier::Oled6GB));
    NEMU_TEST_ASSERT(fm.GetDramSize() == FastmemManager::SIZE_6GB);
    fm.Shutdown();

    // 3. Test 8GB DevKit tier initialization
    NEMU_TEST_ASSERT(fm.Initialize(MemoryTier::DevKit8GB));
    NEMU_TEST_ASSERT(fm.GetDramSize() == FastmemManager::SIZE_8GB);
    fm.Shutdown();

    std::cout << "  PASSED.\n";
}

void TestFastmemExceptionHandler() {
    std::cout << "[TEST] Running TestFastmemExceptionHandler...\n";
    auto& handler = FastmemExceptionHandler::Instance();
    auto& fm = FastmemManager::Instance();

    // Register VEH / SIGSEGV
    NEMU_TEST_ASSERT(handler.Register());
    NEMU_TEST_ASSERT(handler.IsRegistered());

    NEMU_TEST_ASSERT(fm.Initialize(MemoryTier::Retail4GB));
    handler.ResetStats();

    uintptr_t last_guest_addr = 0;
    FastmemAccessType last_type = FastmemAccessType::Read;
    bool callback_called = false;

    handler.SetFaultCallback([&](const FastmemFaultInfo& info) -> bool {
        callback_called = true;
        last_guest_addr = info.guest_address;
        last_type = info.access_type;
        return true; // mark recovered
    });

    // 1. Fault outside fastmem range
    const bool out_handled = handler.SimulateFault(0x1000, FastmemAccessType::Read);
    NEMU_TEST_ASSERT(!out_handled);
    NEMU_TEST_ASSERT(!callback_called);
    auto stats = handler.GetStats();
    NEMU_TEST_ASSERT(stats.total_faults == 1);
    NEMU_TEST_ASSERT(stats.fastmem_faults == 0);

    // 2. Fault inside fastmem range (e.g. base + 0x40000)
    const uintptr_t base = reinterpret_cast<uintptr_t>(fm.GetBase());
    const uintptr_t target_fault = base + 0x40000;
    const bool in_handled = handler.SimulateFault(target_fault, FastmemAccessType::Write);
    NEMU_TEST_ASSERT(in_handled);
    NEMU_TEST_ASSERT(callback_called);
    NEMU_TEST_ASSERT(last_guest_addr == 0x40000);
    NEMU_TEST_ASSERT(last_type == FastmemAccessType::Write);

    stats = handler.GetStats();
    NEMU_TEST_ASSERT(stats.total_faults == 2);
    NEMU_TEST_ASSERT(stats.fastmem_faults == 1);
    NEMU_TEST_ASSERT(stats.recovered_faults == 1);

    // 3. Unregister and cleanup
    handler.ClearFaultCallback();
    handler.Unregister();
    NEMU_TEST_ASSERT(!handler.IsRegistered());
    fm.Shutdown();

    std::cout << "  PASSED.\n";
}

int main() {
    std::cout << "========================================\n";
    std::cout << "   NEMU VIRTUAL MEMORY UNIT TESTS       \n";
    std::cout << "========================================\n";

    TestMappingAndPermissions();
    TestBlockAndBoundary();
    TestFastmemAndTiers();
    TestFastmemExceptionHandler();

    std::cout << "ALL MEMORY UNIT TESTS PASSED SUCCESSFULLY!\n";
    return 0;
}
