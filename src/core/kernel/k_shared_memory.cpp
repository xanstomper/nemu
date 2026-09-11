#include "k_shared_memory.hpp"
#include "platform/logger.hpp"

namespace nemu::core::kernel {

KSharedMemory::KSharedMemory(size_t size, memory::MemoryPermission owner_perm, memory::MemoryPermission user_perm)
    : KAutoObject(HandleType::SharedMemory),
      size_(size),
      owner_perm_(owner_perm),
      user_perm_(user_perm),
      backing_(size, 0) {}

bool KSharedMemory::MapInto(memory::VirtualMemory& vm, vaddr_t address, memory::MemoryPermission perm) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!vm.MapBacking(address, size_, backing_.data(), perm)) {
        NEMU_LOG_ERROR("Kernel", "KSharedMemory: Failed to map backing at 0x{:016X}", address);
        return false;
    }
    NEMU_LOG_DEBUG("Kernel", "KSharedMemory: Mapped 0x{:X} bytes at 0x{:016X}", size_, address);
    return true;
}

bool KSharedMemory::UnmapFrom(memory::VirtualMemory& vm, vaddr_t address) {
    std::lock_guard<std::mutex> lock(mutex_);
    return vm.Unmap(address, size_);
}

} // namespace nemu::core::kernel
