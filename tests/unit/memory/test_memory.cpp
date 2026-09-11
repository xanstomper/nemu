#include "core/memory/virtual_memory.hpp"
#include "core/memory/fastmem.hpp"
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

int main() {
    std::cout << "========================================\n";
    std::cout << "   NEMU VIRTUAL MEMORY UNIT TESTS       \n";
    std::cout << "========================================\n";

    TestMappingAndPermissions();
    TestBlockAndBoundary();
    TestFastmemAndTiers();

    std::cout << "ALL MEMORY UNIT TESTS PASSED SUCCESSFULLY!\n";
    return 0;
}
