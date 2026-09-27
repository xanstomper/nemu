#include "gmmu.hpp"
#include "core/memory/virtual_memory.hpp"
#include <algorithm>
#include <cstring>

namespace nemu::core::gpu {

GpuMemoryManager::GpuMemoryManager(memory::VirtualMemory* cpu_as) : cpu_as_(cpu_as) {}

bool GpuMemoryManager::Map(u64 gpu_addr, u64 size, u8* host_backing) {
    if (size == 0 || !host_backing) return false;
    if (gpu_addr + size > kGpuVaLimit) return false;

    std::unique_lock lock(mutex_);

    const u64 first_page = gpu_addr / kBigPageSize;
    const u64 last_page = (gpu_addr + size - 1) / kBigPageSize;

    for (u64 p = first_page; p <= last_page; ++p) {
        BigPage& page = pages_[p];
        page.backing = host_backing + (p * kBigPageSize - gpu_addr);
        page.page_offset = static_cast<u32>(p * kBigPageSize - gpu_addr);
        page.map_size = static_cast<u32>(
            std::min<u64>(kBigPageSize, size - (p * kBigPageSize - gpu_addr)));
    }
    stats_.mapped_pages = pages_.size();
    return true;
}

void GpuMemoryManager::Unmap(u64 gpu_addr, u64 size) {
    if (size == 0) return;
    std::unique_lock lock(mutex_);

    const u64 first_page = gpu_addr / kBigPageSize;
    const u64 last_page = (gpu_addr + size - 1) / kBigPageSize;

    for (u64 p = first_page; p <= last_page; ++p) {
        pages_.erase(p);
    }
    stats_.mapped_pages = pages_.size();
}

bool GpuMemoryManager::IsMapped(u64 gpu_addr, u64 size) const {
    if (size == 0) return true;
    if (gpu_addr + size > kGpuVaLimit) return false;

    std::shared_lock lock(mutex_);

    const u64 first_page = gpu_addr / kBigPageSize;
    const u64 last_page = (gpu_addr + size - 1) / kBigPageSize;

    for (u64 p = first_page; p <= last_page; ++p) {
        const auto it = pages_.find(p);
        if (it == pages_.end()) return false;
    }
    return true;
}

size_t GpuMemoryManager::Read(u64 gpu_addr, void* out, u64 size) const {
    if (size == 0 || !out) return 0;
    std::shared_lock lock(mutex_);

    u8* dst = static_cast<u8*>(out);
    u64 transferred = 0;
    u64 cur = gpu_addr;
    u64 remaining = size;

    while (remaining > 0) {
        const u64 page = cur / kBigPageSize;
        const u64 page_off = cur % kBigPageSize;
        const auto it = pages_.find(page);
        if (it == pages_.end()) break; // unmapped: stop like the real card faults

        const u64 in_page = std::min<u64>(remaining, kBigPageSize - page_off);
        std::memcpy(dst + transferred, it->second.backing + page_off, in_page);
        transferred += in_page;
        cur += in_page;
        remaining -= in_page;
    }

    stats_.read_calls++;
    stats_.bytes_read += transferred;
    return transferred;
}

size_t GpuMemoryManager::Write(u64 gpu_addr, const void* in, u64 size) {
    if (size == 0 || !in) return 0;
    std::unique_lock lock(mutex_);

    const u8* src = static_cast<const u8*>(in);
    u64 transferred = 0;
    u64 cur = gpu_addr;
    u64 remaining = size;

    while (remaining > 0) {
        const u64 page = cur / kBigPageSize;
        const u64 page_off = cur % kBigPageSize;
        const auto it = pages_.find(page);
        if (it == pages_.end()) break;

        const u64 in_page = std::min<u64>(remaining, kBigPageSize - page_off);
        std::memcpy(it->second.backing + page_off, src + transferred, in_page);
        transferred += in_page;
        cur += in_page;
        remaining -= in_page;
    }

    stats_.write_calls++;
    stats_.bytes_written += transferred;
    return transferred;
}

u8* GpuMemoryManager::GetHostPointer(u64 gpu_addr) const {
    std::shared_lock lock(mutex_);
    const auto it = pages_.find(gpu_addr / kBigPageSize);
    if (it == pages_.end()) return nullptr;
    return it->second.backing + (gpu_addr % kBigPageSize);
}

} // namespace nemu::core::gpu
