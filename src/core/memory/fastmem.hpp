#pragma once

#include "core/types.hpp"
#include "memory_interface.hpp"
#include <cstddef>
#include <string>
#include <mutex>

namespace nemu::core::memory {

enum class MemoryTier : u8 {
    Retail4GB = 0, // 4 GiB standard Switch DRAM
    Oled6GB   = 1, // 6 GiB OLED / enhanced memory
    DevKit8GB = 2, // 8 GiB SDEV hardware unit
};

class FastmemManager {
public:
    static constexpr size_t SIZE_4GB = 0x100000000ULL;      // 4 GiB
    static constexpr size_t SIZE_6GB = 0x180000000ULL;      // 6 GiB
    static constexpr size_t SIZE_8GB = 0x200000000ULL;      // 8 GiB
    static constexpr size_t VIRTUAL_SPACE_39BIT = 1ULL << 39; // 512 GiB virtual space

    FastmemManager();
    ~FastmemManager();

    FastmemManager(const FastmemManager&) = delete;
    FastmemManager& operator=(const FastmemManager&) = delete;

    /// Reserve contiguous virtual address space on host
    bool Initialize(MemoryTier tier = MemoryTier::Retail4GB, bool reserve_full_39bit = false);

    /// Release host reservation
    void Shutdown();

    /// Commit memory pages at guest address
    bool Commit(vaddr_t address, size_t size, MemoryPermission perms);

    /// Decommit memory pages at guest address
    bool Decommit(vaddr_t address, size_t size);

    /// Change protection on committed pages
    bool Protect(vaddr_t address, size_t size, MemoryPermission perms);

    /// Translate guest virtual address to direct host pointer
    [[nodiscard]] u8* GetPointer(vaddr_t guest_address) const noexcept;

    /// Check if an address range is entirely within the committed fastmem region
    [[nodiscard]] bool IsValidRange(vaddr_t address, size_t size = 1) const noexcept;

    [[nodiscard]] bool IsEnabled() const noexcept { return is_initialized_; }
    [[nodiscard]] u8* GetBase() const noexcept { return base_pointer_; }
    [[nodiscard]] size_t GetTotalReservationSize() const noexcept { return reservation_size_; }
    [[nodiscard]] size_t GetDramSize() const noexcept { return dram_size_; }
    [[nodiscard]] MemoryTier GetTier() const noexcept { return tier_; }

    static FastmemManager& Instance();

private:
    u8* base_pointer_{nullptr};
    size_t reservation_size_{0};
    size_t dram_size_{0};
    MemoryTier tier_{MemoryTier::Retail4GB};
    bool is_initialized_{false};
    mutable std::mutex mutex_;
};

} // namespace nemu::core::memory
