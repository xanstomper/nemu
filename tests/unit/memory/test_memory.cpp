#include "core/memory/virtual_memory.hpp"
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

int main() {
    std::cout << "========================================\n";
    std::cout << "   NEMU VIRTUAL MEMORY UNIT TESTS       \n";
    std::cout << "========================================\n";

    TestMappingAndPermissions();
    TestBlockAndBoundary();

    std::cout << "ALL MEMORY UNIT TESTS PASSED SUCCESSFULLY!\n";
    return 0;
}
