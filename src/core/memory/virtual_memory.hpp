#pragma once

#include "memory_interface.hpp"
#include <vector>
#include <unordered_map>
#include <memory>
#include <mutex>
#include <functional>
#include <atomic>

namespace nemu::core::memory {

class VirtualMemory final : public IMemory {
public:
    // --- Self-modifying-code support (yuzu-exclusive until now): guest writes
    // to executable pages notify registered listeners so the JIT can drop
    // stale blocks. Listener = (start, size) of the written guest range.
    using WriteHook = std::function<void(vaddr_t, size_t)>;
    void SetWriteHook(WriteHook hook) { write_hook_ = std::move(hook); }
    static constexpr size_t PAGE_BITS = 12;
    static constexpr size_t PAGE_SIZE = 1ULL << PAGE_BITS;
    static constexpr size_t PAGE_MASK = PAGE_SIZE - 1;

    struct PageInfo {
        u8* host_ptr{nullptr};
        MemoryPermission permissions{MemoryPermission::None};
        bool is_owned{false};
    };

    /// Guest memory-fault counters. The headless boot probe reads these to tell
    /// a genuinely running guest from one that is spinning on an unmapped page.
    struct FaultStats {
        std::atomic<u64> read_faults{0};
        std::atomic<u64> write_faults{0};
        std::atomic<u64> total_faults{0};
        std::atomic<vaddr_t> last_fault_address{0};
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

    /// Address of the first mapped page at or above `address` (page-aligned),
    /// or 0 if none. O(mapped pages) under a single lock — used to terminate
    /// address-space walks over huge unmapped gaps without stepping page by
    /// page (which took billions of lock/unlock iterations and hung the guest).
    [[nodiscard]] vaddr_t NextMappedAddress(vaddr_t address) const;

    [[nodiscard]] const FaultStats& GetFaultStats() const noexcept { return faults_; }
    [[nodiscard]] u64 GetTotalFaults() const noexcept {
        return faults_.total_faults.load(std::memory_order_relaxed);
    }
    void ResetFaultStats() noexcept {
        faults_.read_faults.store(0, std::memory_order_relaxed);
        faults_.write_faults.store(0, std::memory_order_relaxed);
        faults_.total_faults.store(0, std::memory_order_relaxed);
        faults_.last_fault_address.store(0, std::memory_order_relaxed);
    }

    WriteHook write_hook_;  // SMC invalidation hook (set by the JIT)
private:
    const PageInfo* LookupPage(vaddr_t address) const;
    PageInfo* LookupPage(vaddr_t address);

    /// Count + throttle a guest memory fault. Guest code that walks off a mapped
    /// page (a half-resolved GOT slot, an unfinished relocation loop, a null
    /// vtable) faults on *every* retry; logging each one produced a 6.2 GB
    /// run.log that buried the first real error and made `grep` time out. The
    /// counts also drive the headless boot probe so a guest that dies this way
    /// reports FAILED instead of "BOOTED".
    void ReportFault(const char* op, vaddr_t address, bool is_write);
    FaultStats faults_{};

    // Page index -> PageInfo
    std::unordered_map<u64, PageInfo> page_table_;
    std::vector<std::unique_ptr<u8[]>> allocated_blocks_;
    mutable std::mutex memory_mutex_;
};

} // namespace nemu::core::memory
