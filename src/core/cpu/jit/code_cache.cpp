#include "code_cache.hpp"
#include "platform/logger.hpp"

#ifdef _WIN32
#include <windows.h>
#else
#include <sys/mman.h>
#include <unistd.h>
#endif

namespace nemu::core::cpu::jit {

CodeCache::CodeCache(size_t total_size)
    : total_size_(total_size) {
#ifdef _WIN32
    base_ptr_ = static_cast<u8*>(VirtualAlloc(
        nullptr,
        total_size_,
        MEM_COMMIT | MEM_RESERVE,
        PAGE_EXECUTE_READWRITE));
    if (!base_ptr_) {
        NEMU_LOG_FATAL("JIT", "VirtualAlloc failed to allocate {} bytes of executable memory", total_size_);
    }
#else
    void* ptr = mmap(
        nullptr,
        total_size_,
        PROT_READ | PROT_WRITE | PROT_EXEC,
        MAP_PRIVATE | MAP_ANONYMOUS,
        -1,
        0);
    if (ptr == MAP_FAILED) {
        base_ptr_ = nullptr;
        NEMU_LOG_FATAL("JIT", "mmap failed to allocate {} bytes of executable memory", total_size_);
    } else {
        base_ptr_ = static_cast<u8*>(ptr);
    }
#endif
    if (base_ptr_) {
        NEMU_LOG_INFO("JIT", "Allocated {} MiB executable code cache at {:p}", total_size_ / (1024 * 1024), static_cast<void*>(base_ptr_));
    }
}

CodeCache::~CodeCache() {
    if (base_ptr_) {
#ifdef _WIN32
        VirtualFree(base_ptr_, 0, MEM_RELEASE);
#else
        munmap(base_ptr_, total_size_);
#endif
        base_ptr_ = nullptr;
    }
}

u8* CodeCache::Allocate(size_t byte_count) {
    // 16-byte align each block
    const size_t aligned_offset = (current_offset_ + 15) & ~size_t(15);
    if (aligned_offset + byte_count > total_size_) {
        NEMU_LOG_WARN("JIT", "CodeCache exhausted (used {} / {} bytes)", aligned_offset, total_size_);
        return nullptr;
    }

    u8* result = base_ptr_ + aligned_offset;
    current_offset_ = aligned_offset + byte_count;
    return result;
}

void CodeCache::Flush(const void* ptr, size_t size) const {
#ifdef _WIN32
    FlushInstructionCache(GetCurrentProcess(), ptr, size);
#else
    __builtin___clear_cache(
        reinterpret_cast<char*>(const_cast<void*>(ptr)),
        reinterpret_cast<char*>(const_cast<void*>(ptr)) + size);
#endif
}

void CodeCache::Reset() {
    current_offset_ = 0;
    NEMU_LOG_INFO("JIT", "Code cache reset");
}

} // namespace nemu::core::cpu::jit
