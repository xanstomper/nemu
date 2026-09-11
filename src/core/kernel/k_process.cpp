#include "k_process.hpp"
#include "platform/logger.hpp"

namespace nemu::core::kernel {

KProcess::KProcess(u64 pid, std::string name)
    : KAutoObject(HandleType::Process), pid_(pid), name_(std::move(name)) {
    // Map default process stack region
    const vaddr_t stack_base = DEFAULT_STACK_TOP - DEFAULT_STACK_SIZE;
    memory_.Map(stack_base, DEFAULT_STACK_SIZE, memory::MemoryPermission::ReadWrite);

    // Map default TLS base
    memory_.Map(DEFAULT_TLS_BASE, memory::VirtualMemory::PAGE_SIZE, memory::MemoryPermission::ReadWrite);

    NEMU_LOG_DEBUG("Kernel", "Initialized KProcess '{}' (PID {})", name_, pid_);
}

vaddr_t KProcess::SetHeapSize(size_t size) {
    std::lock_guard lock(process_mutex_);

    // Round up size to page boundary
    const size_t aligned_size = (size + memory::VirtualMemory::PAGE_MASK) & ~memory::VirtualMemory::PAGE_MASK;

    if (aligned_size == current_heap_size_) {
        return heap_base_;
    }

    if (aligned_size > current_heap_size_) {
        // Expand heap
        const size_t bytes_to_add = aligned_size - current_heap_size_;
        const vaddr_t map_address = heap_base_ + current_heap_size_;

        if (!memory_.Map(map_address, bytes_to_add, memory::MemoryPermission::ReadWrite)) {
            NEMU_LOG_ERROR("Kernel", "Failed to expand heap for PID {} by {} bytes", pid_, bytes_to_add);
            return 0;
        }
        current_heap_size_ = aligned_size;
        NEMU_LOG_DEBUG("Kernel", "Expanded heap for PID {} to {} bytes", pid_, current_heap_size_);
    } else {
        // Shrink heap
        const size_t bytes_to_remove = current_heap_size_ - aligned_size;
        const vaddr_t unmap_address = heap_base_ + aligned_size;

        if (!memory_.Unmap(unmap_address, bytes_to_remove)) {
            NEMU_LOG_ERROR("Kernel", "Failed to shrink heap for PID {} by {} bytes", pid_, bytes_to_remove);
            return 0;
        }
        current_heap_size_ = aligned_size;
        NEMU_LOG_DEBUG("Kernel", "Shrank heap for PID {} to {} bytes", pid_, current_heap_size_);
    }

    return heap_base_;
}

} // namespace nemu::core::kernel
