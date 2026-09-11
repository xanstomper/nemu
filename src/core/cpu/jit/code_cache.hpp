#pragma once

#include "core/types.hpp"
#include <cstddef>
#include <vector>
#include <span>

namespace nemu::core::cpu::jit {

class CodeCache {
public:
    static constexpr size_t DEFAULT_CACHE_SIZE = 16 * 1024 * 1024; // 16 MiB code cache

    explicit CodeCache(size_t total_size = DEFAULT_CACHE_SIZE);
    ~CodeCache();

    CodeCache(const CodeCache&) = delete;
    CodeCache& operator=(const CodeCache&) = delete;

    /// Allocate executable buffer of `byte_count` bytes
    [[nodiscard]] u8* Allocate(size_t byte_count);

    /// Flush CPU instruction caches for the written range
    void Flush(const void* ptr, size_t size) const;

    /// Reset allocation pointer (clears compiled blocks)
    void Reset();

    [[nodiscard]] size_t GetUsedBytes() const noexcept { return current_offset_; }
    [[nodiscard]] size_t GetTotalCapacity() const noexcept { return total_size_; }
    [[nodiscard]] bool IsValid() const noexcept { return base_ptr_ != nullptr; }

private:
    u8* base_ptr_{nullptr};
    size_t total_size_{0};
    size_t current_offset_{0};
};

} // namespace nemu::core::cpu::jit
