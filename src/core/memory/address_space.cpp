#include "core/memory/address_space.hpp"
#include "platform/logger.hpp"
#include <algorithm>
#include <cstring>
#include <vector>

#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#else
#include <sys/mman.h>
#include <unistd.h>
#endif

namespace nemu::core::memory {

namespace {
constexpr size_t PageSize = 0x1000;
constexpr size_t PageMask = PageSize - 1;
} // namespace

AddressSpace::~AddressSpace() {
    if (base_) {
#ifdef _WIN32
        VirtualFree(base_, 0, MEM_RELEASE);
#else
        ::munmap(base_, reservation_size_);
#endif
        base_ = nullptr;
        reservation_size_ = 0;
    }
}

bool AddressSpace::Reserve(size_t reservation_size) {
    if (base_) {
        return false; // already reserved
    }
    if (reservation_size == 0 || (reservation_size & PageMask) != 0) {
        return false;
    }
#ifdef _WIN32
    base_ = static_cast<u8*>(VirtualAlloc(nullptr, reservation_size, MEM_RESERVE,
                                          PAGE_NOACCESS));
    if (!base_) {
        NEMU_LOG_ERROR("Memory", "AddressSpace: VirtualAlloc reserve failed ({} bytes)", reservation_size);
        return false;
    }
#else
    base_ = static_cast<u8*>(::mmap(nullptr, reservation_size, PROT_NONE,
                                    MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0));
    if (base_ == MAP_FAILED) {
        base_ = nullptr;
        NEMU_LOG_ERROR("Memory", "AddressSpace: mmap reserve failed ({} bytes)", reservation_size);
        return false;
    }
#endif
    reservation_size_ = reservation_size;
    NEMU_LOG_INFO("Memory", "AddressSpace reserved {} bytes at host {}", reservation_size,
                  reinterpret_cast<void*>(base_));
    return true;
}

bool AddressSpace::Commit(vaddr_t offset, size_t size) {
    if (!base_) {
        return false;
    }
    const vaddr_t start = offset;
    const vaddr_t end = offset + size;
    if (end < start || end > reservation_size_) {
        return false; // out of reservation
    }
    if (size == 0) {
        return true;
    }
#ifdef _WIN32
    void* p = VirtualAlloc(base_ + offset, size, MEM_COMMIT, PAGE_READWRITE);
    if (!p) {
        NEMU_LOG_ERROR("Memory", "AddressSpace: VirtualAlloc commit failed @0x{:X} size 0x{:X}", offset, size);
        return false;
    }
#else
    void* p = ::mmap(base_ + offset, size, PROT_READ | PROT_WRITE,
                     MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0);
    if (p == MAP_FAILED) {
        NEMU_LOG_ERROR("Memory", "AddressSpace: mmap commit failed @0x{:X} size 0x{:X}", offset, size);
        return false;
    }
#endif
    std::memset(base_ + offset, 0, size);
    committed_ranges_.push_back({offset, size});
    return true;
}

bool AddressSpace::Decommit(vaddr_t offset, size_t size) {
    if (!base_) {
        return false;
    }
    const vaddr_t end = offset + size;
    if (end < offset || end > reservation_size_) {
        return false;
    }
#ifdef _WIN32
    VirtualFree(base_ + offset, size, MEM_DECOMMIT);
#else
    ::madvise(base_ + offset, size, MADV_DONTNEED); // hint-only on POSIX
#endif
    // Remove the committed range from the tracking list.
    auto it = std::remove_if(committed_ranges_.begin(), committed_ranges_.end(),
                             [&](const Range& r) { return r.offset >= offset && r.begin_end() <= end; });
    committed_ranges_.erase(it, committed_ranges_.end());
    return true;
}

u8* AddressSpace::GetPointer(vaddr_t offset) const noexcept {
    if (!base_ || offset >= reservation_size_) {
        return nullptr;
    }
    // Only hand out pointers to pages we have actually committed.
    for (const Range& r : committed_ranges_) {
        if (offset >= r.offset && offset < r.begin_end()) {
            return base_ + offset;
        }
    }
    return nullptr;
}

u8* AddressSpace::GetRangePointer(vaddr_t offset, size_t size) const noexcept {
    if (!base_ || size == 0 || offset + size > reservation_size_ || offset + size < offset) {
        return nullptr;
    }
    const vaddr_t end = offset + size;
    for (const Range& r : committed_ranges_) {
        if (offset >= r.offset && end <= r.begin_end()) {
            return base_ + offset;
        }
    }
    return nullptr;
}

} // namespace nemu::core::memory