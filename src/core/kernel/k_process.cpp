#include "k_process.hpp"
#include "k_thread.hpp"
#include "platform/logger.hpp"
#include <algorithm>

namespace nemu::core::kernel {

KProcess::KProcess(u64 pid, std::string name)
    : KAutoObject(HandleType::Process), pid_(pid), name_(std::move(name)) {
    // Map default process stack region
    const vaddr_t stack_base = DEFAULT_STACK_TOP - DEFAULT_STACK_SIZE;
    memory_.Map(stack_base, DEFAULT_STACK_SIZE, memory::MemoryPermission::ReadWrite);

    // Map default TLS base
    memory_.Map(DEFAULT_TLS_BASE, memory::VirtualMemory::PAGE_SIZE, memory::MemoryPermission::ReadWrite);

    // Map the low process/thread-context region. libnx crt0 reads and writes a
    // handful of descriptors just above address 0 (e.g. boot/thread context at
    // 0x2E0..0x340) during app init. Without it those accesses fault and the
    // game branches to a null handler. Real Switch reserves this window.
    constexpr vaddr_t kLowRegionBase = 0x0;
    constexpr size_t kLowRegionSize = 0x4000; // 16 KiB
    memory_.Map(kLowRegionBase, kLowRegionSize, memory::MemoryPermission::ReadWrite);

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

vaddr_t KProcess::SetHeapBase(vaddr_t base, size_t size) {
    std::lock_guard lock(process_mutex_);

    const size_t aligned_size =
        (size + memory::VirtualMemory::PAGE_MASK) & ~static_cast<size_t>(memory::VirtualMemory::PAGE_MASK);

    if (aligned_size == 0) {
        return heap_base_;
    }

    // A non-NULL base means the caller (or the kernel's own layout) picked the
    // region; we only need to map it. A NULL base means "you choose".
    if (base != 0 && base != heap_base_) {
        // Drop the previous mapping so we do not leak a region on re-init.
        if (current_heap_size_ != 0) {
            memory_.Unmap(heap_base_, current_heap_size_);
        }
        heap_base_ = base;
        current_heap_size_ = 0;
    }

    if (aligned_size > current_heap_size_) {
        const size_t bytes_to_add = aligned_size - current_heap_size_;
        const vaddr_t map_address = heap_base_ + current_heap_size_;
        if (!memory_.Map(map_address, bytes_to_add, memory::MemoryPermission::ReadWrite)) {
            NEMU_LOG_ERROR("Kernel", "Failed to map heap for PID {} (base 0x{:016X}, {} bytes)",
                           pid_, map_address, bytes_to_add);
            return 0;
        }
        current_heap_size_ = aligned_size;
    }

    NEMU_LOG_INFO("Kernel", "Process {} heap: base 0x{:016X} size 0x{:X} bytes",
                   pid_, heap_base_, current_heap_size_);
    return heap_base_;
}

void KProcess::AddThread(std::shared_ptr<KThread> thread) {
    if (!thread) return;
    std::lock_guard lock(process_mutex_);
    for (const auto& t : threads_) {
        if (t && t->GetTid() == thread->GetTid()) {
            return;
        }
    }
    threads_.push_back(std::move(thread));
}

void KProcess::RemoveThread(u64 tid) {
    std::lock_guard lock(process_mutex_);
    threads_.erase(
        std::remove_if(threads_.begin(), threads_.end(),
                       [tid](const std::shared_ptr<KThread>& t) {
                           return !t || t->GetTid() == tid;
                       }),
        threads_.end());
}

std::vector<std::shared_ptr<KThread>> KProcess::GetThreads() const {
    std::lock_guard lock(const_cast<std::mutex&>(process_mutex_));
    return threads_;
}

std::shared_ptr<KThread> KProcess::GetThread(u64 tid) const {
    std::lock_guard lock(const_cast<std::mutex&>(process_mutex_));
    for (const auto& t : threads_) {
        if (t && t->GetTid() == tid) {
            return t;
        }
    }
    return nullptr;
}

} // namespace nemu::core::kernel
