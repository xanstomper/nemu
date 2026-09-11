#include "bench.hpp"
#include "core/memory/virtual_memory.hpp"
#include <iostream>
#include <vector>

using namespace nemu;
using namespace nemu::core;

int main() {
    std::cout << "=========================================================" << std::endl;
    std::cout << "     NEMU BENCHMARK: Virtual Memory Layer Throughput      " << std::endl;
    std::cout << "=========================================================" << std::endl;

    memory::VirtualMemory memory;
    constexpr vaddr_t MEM_BASE = 0x0080000000ULL;
    constexpr size_t REGION_SIZE = 16 * 1024 * 1024; // 16 MiB
    memory.Map(MEM_BASE, REGION_SIZE, memory::MemoryPermission::All);

    constexpr u64 ITERATIONS = 10'000'000;

    // 1. 64-bit Sequential Read/Write
    {
        bench::BenchmarkTimer timer("VirtualMemory 64-bit Random-Access Ops", ITERATIONS);
        u64 acc = 0;
        vaddr_t addr = MEM_BASE;

        for (u64 i = 0; i < ITERATIONS; ++i) {
            memory.Write64(addr, i);
            acc += memory.Read64(addr);
            addr += 8;
            if (addr >= MEM_BASE + REGION_SIZE - 8) {
                addr = MEM_BASE;
            }
        }
        (void)acc;
    }

    // 2. Block Copy (Memcpy through VFS/Memory across 4 KiB boundaries)
    constexpr size_t BLOCK_SIZE = 64 * 1024; // 64 KiB
    constexpr u64 BLOCK_TRANSFERS = 20'000;
    std::vector<u8> src_buf(BLOCK_SIZE, 0xAB);
    std::vector<u8> dst_buf(BLOCK_SIZE, 0x00);

    {
        bench::BenchmarkTimer timer("VirtualMemory Cross-Page Block Transfers", BLOCK_TRANSFERS);
        vaddr_t block_addr = MEM_BASE;

        for (u64 i = 0; i < BLOCK_TRANSFERS; ++i) {
            memory.WriteBlock(block_addr, src_buf.data(), BLOCK_SIZE);
            memory.ReadBlock(block_addr, dst_buf.data(), BLOCK_SIZE);
            block_addr += 4096;
            if (block_addr + BLOCK_SIZE >= MEM_BASE + REGION_SIZE) {
                block_addr = MEM_BASE;
            }
        }
    }

    const double total_gb = static_cast<double>(BLOCK_TRANSFERS * BLOCK_SIZE * 2) / (1024.0 * 1024.0 * 1024.0);
    std::cout << "  Total Data Transferred: " << std::fixed << std::setprecision(2) << total_gb << " GB" << std::endl;
    std::cout << "=========================================================" << std::endl;

    return 0;
}
