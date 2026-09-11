#include "fastmem.hpp"
#include "platform/logger.hpp"

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <sys/mman.h>
#include <unistd.h>
#endif

namespace nemu::core::memory {

FastmemManager::FastmemManager() = default;

FastmemManager::~FastmemManager() {
    Shutdown();
}

FastmemManager& FastmemManager::Instance() {
    static FastmemManager s_instance;
    return s_instance;
}

bool FastmemManager::Initialize(MemoryTier tier, bool reserve_full_39bit) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (is_initialized_) {
        return true;
    }

    tier_ = tier;
    switch (tier) {
    case MemoryTier::Retail4GB:
        dram_size_ = SIZE_4GB;
        break;
    case MemoryTier::Oled6GB:
        dram_size_ = SIZE_6GB;
        break;
    case MemoryTier::DevKit8GB:
        dram_size_ = SIZE_8GB;
        break;
    }

    reservation_size_ = reserve_full_39bit ? VIRTUAL_SPACE_39BIT : dram_size_;

#if defined(_WIN32)
    base_pointer_ = static_cast<u8*>(VirtualAlloc(
        nullptr,
        reservation_size_,
        MEM_RESERVE,
        PAGE_NOACCESS
    ));
    if (!base_pointer_) {
        NEMU_LOG_ERROR("Fastmem", "VirtualAlloc reservation failed for size 0x{:016X}, error: {}",
                       reservation_size_, GetLastError());
        return false;
    }
#else
    base_pointer_ = static_cast<u8*>(mmap(
        nullptr,
        reservation_size_,
        PROT_NONE,
        MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE,
        -1,
        0
    ));
    if (base_pointer_ == MAP_FAILED || !base_pointer_) {
        NEMU_LOG_ERROR("Fastmem", "mmap reservation failed for size 0x{:016X}", reservation_size_);
        base_pointer_ = nullptr;
        return false;
    }
#endif

    is_initialized_ = true;
    NEMU_LOG_INFO("Fastmem", "Initialized Fastmem at host base {:p} (Reserved: {} GiB, DRAM Tier: {} GiB)",
                  static_cast<void*>(base_pointer_),
                  reservation_size_ / (1024 * 1024 * 1024ULL),
                  dram_size_ / (1024 * 1024 * 1024ULL));
    return true;
}

void FastmemManager::Shutdown() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!is_initialized_ || !base_pointer_) {
        return;
    }

#if defined(_WIN32)
    VirtualFree(base_pointer_, 0, MEM_RELEASE);
#else
    munmap(base_pointer_, reservation_size_);
#endif

    base_pointer_ = nullptr;
    reservation_size_ = 0;
    dram_size_ = 0;
    is_initialized_ = false;
    NEMU_LOG_INFO("Fastmem", "Shutdown Fastmem address space");
}

bool FastmemManager::Commit(vaddr_t address, size_t size, MemoryPermission perms) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!is_initialized_ || !base_pointer_) {
        return false;
    }

    if (address + size > reservation_size_) {
        NEMU_LOG_ERROR("Fastmem", "Commit range [0x{:X}, 0x{:X}] exceeds reservation 0x{:X}",
                       address, address + size, reservation_size_);
        return false;
    }

#if defined(_WIN32)
    DWORD win_prot = PAGE_NOACCESS;
    const bool r = HasPermission(perms, MemoryPermission::Read);
    const bool w = HasPermission(perms, MemoryPermission::Write);
    const bool x = HasPermission(perms, MemoryPermission::Execute);

    if (x) {
        win_prot = (w ? PAGE_EXECUTE_READWRITE : (r ? PAGE_EXECUTE_READ : PAGE_EXECUTE));
    } else if (w) {
        win_prot = PAGE_READWRITE;
    } else if (r) {
        win_prot = PAGE_READONLY;
    }

    void* ptr = VirtualAlloc(base_pointer_ + address, size, MEM_COMMIT, win_prot);
    return ptr != nullptr;
#else
    int prot = PROT_NONE;
    if (HasPermission(perms, MemoryPermission::Read))    prot |= PROT_READ;
    if (HasPermission(perms, MemoryPermission::Write))   prot |= PROT_WRITE;
    if (HasPermission(perms, MemoryPermission::Execute)) prot |= PROT_EXEC;

    return mprotect(base_pointer_ + address, size, prot) == 0;
#endif
}

bool FastmemManager::Decommit(vaddr_t address, size_t size) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!is_initialized_ || !base_pointer_) {
        return false;
    }

    if (address + size > reservation_size_) {
        return false;
    }

#if defined(_WIN32)
    return VirtualFree(base_pointer_ + address, size, MEM_DECOMMIT) != 0;
#else
    mprotect(base_pointer_ + address, size, PROT_NONE);
    madvise(base_pointer_ + address, size, MADV_DONTNEED);
    return true;
#endif
}

bool FastmemManager::Protect(vaddr_t address, size_t size, MemoryPermission perms) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!is_initialized_ || !base_pointer_) {
        return false;
    }

    if (address + size > reservation_size_) {
        return false;
    }

#if defined(_WIN32)
    DWORD win_prot = PAGE_NOACCESS;
    const bool r = HasPermission(perms, MemoryPermission::Read);
    const bool w = HasPermission(perms, MemoryPermission::Write);
    const bool x = HasPermission(perms, MemoryPermission::Execute);

    if (x) {
        win_prot = (w ? PAGE_EXECUTE_READWRITE : (r ? PAGE_EXECUTE_READ : PAGE_EXECUTE));
    } else if (w) {
        win_prot = PAGE_READWRITE;
    } else if (r) {
        win_prot = PAGE_READONLY;
    }

    DWORD old_prot = 0;
    return VirtualProtect(base_pointer_ + address, size, win_prot, &old_prot) != 0;
#else
    int prot = PROT_NONE;
    if (HasPermission(perms, MemoryPermission::Read))    prot |= PROT_READ;
    if (HasPermission(perms, MemoryPermission::Write))   prot |= PROT_WRITE;
    if (HasPermission(perms, MemoryPermission::Execute)) prot |= PROT_EXEC;

    return mprotect(base_pointer_ + address, size, prot) == 0;
#endif
}

u8* FastmemManager::GetPointer(vaddr_t guest_address) const noexcept {
    if (!is_initialized_ || !base_pointer_) {
        return nullptr;
    }
    if (guest_address >= reservation_size_) {
        return nullptr;
    }
    return base_pointer_ + guest_address;
}

bool FastmemManager::IsValidRange(vaddr_t address, size_t size) const noexcept {
    if (!is_initialized_ || !base_pointer_) {
        return false;
    }
    return (address + size <= reservation_size_) && (address + size >= address);
}

} // namespace nemu::core::memory
