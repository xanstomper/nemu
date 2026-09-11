#pragma once

#include "memory_interface.hpp"
#include <vector>
#include <unordered_map>
#include <memory>
#include <mutex>

namespace nemu::core::memory {

class VirtualMemory final : public IMemory {
public:
    static constexpr size_t PAGE_BITS = 12;
    static constexpr size_t PAGE_SIZE = 1ULL << PAGE_BITS;
    static constexpr size_t PAGE_MASK = PAGE_SIZE - 1;

    struct PageInfo {
        u8* host_ptr{nullptr};
        MemoryPermission permissions{MemoryPermission::None};
        bool is_owned{false};
    };

    VirtualMemory();
    ~VirtualMemory() override;

    VirtualMemory(const VirtualMemory&) = delete;
    VirtualMemory& operator=(const VirtualMemory&) = delete;

    bool Map(vaddr_t address, size_t size, MemoryPermission permissions);
    bool MapBacking(vaddr_t address, size_t size, u8* host_backing, MemoryPermission permissions);
    bool Unmap(vaddr_t address, size_t size);
    bool Reprotect(vaddr_t address, size_t size, MemoryPermission permissions);

    u8 Read8(vaddr_t address) override;
    u16 Read16(vaddr_t address) override;
    u32 Read32(vaddr_t address) override;
    u64 Read64(vaddr_t address) override;

    void Write8(vaddr_t address, u8 value) override;
    void Write16(vaddr_t address, u16 value) override;
    void Write32(vaddr_t address, u32 value) override;
    void Write64(vaddr_t address, u64 value) override;

    bool ReadBlock(vaddr_t address, void* dest, size_t size) override;
    bool WriteBlock(vaddr_t address, const void* src, size_t size) override;

    u8* GetPointer(vaddr_t address) override;
    const u8* GetPointer(vaddr_t address) const override;

    bool IsValidAddress(vaddr_t address, size_t size = 1) const override;
    std::optional<MemoryPermission> GetPagePermissions(vaddr_t address) const;

private:
    const PageInfo* LookupPage(vaddr_t address) const;
    PageInfo* LookupPage(vaddr_t address);

    // Page index -> PageInfo
    std::unordered_map<u64, PageInfo> page_table_;
    std::vector<std::unique_ptr<u8[]>> allocated_blocks_;
    mutable std::mutex memory_mutex_;
};

} // namespace nemu::core::memory
