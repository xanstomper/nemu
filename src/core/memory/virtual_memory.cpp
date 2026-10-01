#include "virtual_memory.hpp"
#include "platform/logger.hpp"
#include <cstring>
#include <stdexcept>

namespace nemu::core::memory {

VirtualMemory::VirtualMemory() = default;
VirtualMemory::~VirtualMemory() = default;

namespace {
// Log the first few guest memory faults in full, then throttle hard. A guest
// stuck on an unmapped address re-faults millions of times per second; the old
// one-line-per-fault logging produced a 6.2 GB run.log for a single 2000-frame
// probe, which buried the first real error and made `grep` exceed its timeout.
constexpr u64 kFaultLogHeadroom = 32;      // full-detail faults before throttling
constexpr u64 kFaultLogInterval = 100000;  // then only every Nth fault
} // namespace

void VirtualMemory::ReportFault(const char* op, vaddr_t address, bool is_write) {
    if (is_write) {
        faults_.write_faults.fetch_add(1, std::memory_order_relaxed);
    } else {
        faults_.read_faults.fetch_add(1, std::memory_order_relaxed);
    }
    const u64 total = faults_.total_faults.fetch_add(1, std::memory_order_relaxed) + 1;
    faults_.last_fault_address.store(address, std::memory_order_relaxed);

    if (total <= kFaultLogHeadroom) {
        NEMU_LOG_ERROR("Memory", "Unmapped or {} {} at 0x{:016X} (fault #{})",
                       is_write ? "unwritable" : "unreadable", op, address, total);
    } else if (total == kFaultLogHeadroom + 1) {
        NEMU_LOG_ERROR("Memory",
                       "Guest memory faults exceeded {}; throttling further fault logs "
                       "(now every {}th). Last at 0x{:016X}.",
                       kFaultLogHeadroom, kFaultLogInterval, address);
    } else if (total % kFaultLogInterval == 0) {
        NEMU_LOG_ERROR("Memory", "Guest memory fault #{} at 0x{:016X}", total, address);
    }
}

bool VirtualMemory::Map(vaddr_t address, size_t size, MemoryPermission permissions) {
    if (size == 0 || (address & PAGE_MASK) != 0 || (size & PAGE_MASK) != 0) {
        NEMU_LOG_ERROR("Memory", "Map failed: Unaligned address 0x{:016X} or size 0x{:X}", address, size);
        return false;
    }

    std::lock_guard lock(memory_mutex_);
    const size_t num_pages = size / PAGE_SIZE;
    const u64 start_page = address >> PAGE_BITS;

    // Check collision
    for (size_t i = 0; i < num_pages; ++i) {
        if (page_table_.contains(start_page + i)) {
            NEMU_LOG_WARN("Memory", "Map collision at page 0x{:X}", start_page + i);
            return false;
        }
    }

    // Allocate host memory block
    auto host_buffer = std::make_unique<u8[]>(size);
    std::memset(host_buffer.get(), 0, size);
    u8* raw_ptr = host_buffer.get();
    allocated_blocks_.push_back(std::move(host_buffer));

    for (size_t i = 0; i < num_pages; ++i) {
        page_table_[start_page + i] = PageInfo{
            .host_ptr = raw_ptr + (i * PAGE_SIZE),
            .permissions = permissions,
            .is_owned = true
        };
    }

    NEMU_LOG_DEBUG("Memory", "Mapped 0x{:016X} - 0x{:016X} ({} pages)", address, address + size, num_pages);
    return true;
}

bool VirtualMemory::MapBacking(vaddr_t address, size_t size, u8* host_backing, MemoryPermission permissions) {
    if (!host_backing || size == 0 || (address & PAGE_MASK) != 0 || (size & PAGE_MASK) != 0) {
        return false;
    }

    std::lock_guard lock(memory_mutex_);
    const size_t num_pages = size / PAGE_SIZE;
    const u64 start_page = address >> PAGE_BITS;

    for (size_t i = 0; i < num_pages; ++i) {
        if (page_table_.contains(start_page + i)) {
            return false;
        }
    }

    for (size_t i = 0; i < num_pages; ++i) {
        page_table_[start_page + i] = PageInfo{
            .host_ptr = host_backing + (i * PAGE_SIZE),
            .permissions = permissions,
            .is_owned = false
        };
    }

    return true;
}

bool VirtualMemory::Unmap(vaddr_t address, size_t size) {
    if (size == 0 || (address & PAGE_MASK) != 0 || (size & PAGE_MASK) != 0) {
        return false;
    }

    std::lock_guard lock(memory_mutex_);
    const size_t num_pages = size / PAGE_SIZE;
    const u64 start_page = address >> PAGE_BITS;

    for (size_t i = 0; i < num_pages; ++i) {
        page_table_.erase(start_page + i);
    }

    return true;
}

bool VirtualMemory::Reprotect(vaddr_t address, size_t size, MemoryPermission permissions) {
    if (size == 0 || (address & PAGE_MASK) != 0 || (size & PAGE_MASK) != 0) {
        return false;
    }

    std::lock_guard lock(memory_mutex_);
    const size_t num_pages = size / PAGE_SIZE;
    const u64 start_page = address >> PAGE_BITS;

    for (size_t i = 0; i < num_pages; ++i) {
        auto it = page_table_.find(start_page + i);
        if (it != page_table_.end()) {
            it->second.permissions = permissions;
        }
    }

    return true;
}

const VirtualMemory::PageInfo* VirtualMemory::LookupPage(vaddr_t address) const {
    const u64 page_idx = address >> PAGE_BITS;
    auto it = page_table_.find(page_idx);
    if (it != page_table_.end()) {
        return &it->second;
    }
    return nullptr;
}

VirtualMemory::PageInfo* VirtualMemory::LookupPage(vaddr_t address) {
    const u64 page_idx = address >> PAGE_BITS;
    auto it = page_table_.find(page_idx);
    if (it != page_table_.end()) {
        return &it->second;
    }
    return nullptr;
}

u8 VirtualMemory::Read8(vaddr_t address) {
    std::lock_guard lock(memory_mutex_);
    const PageInfo* page = LookupPage(address);
    if (!page || !HasPermission(page->permissions, MemoryPermission::Read)) {
        ReportFault("Read8", address, /*is_write=*/false);
        return 0;
    }
    return page->host_ptr[address & PAGE_MASK];
}

u16 VirtualMemory::Read16(vaddr_t address) {
    u16 val = 0;
    ReadBlock(address, &val, sizeof(val));
    return val;
}

u32 VirtualMemory::Read32(vaddr_t address) {
    u32 val = 0;
    ReadBlock(address, &val, sizeof(val));
    return val;
}

u64 VirtualMemory::Read64(vaddr_t address) {
    u64 val = 0;
    ReadBlock(address, &val, sizeof(val));
    return val;
}

void VirtualMemory::Write8(vaddr_t address, u8 value) {
    std::lock_guard lock(memory_mutex_);
    PageInfo* page = LookupPage(address);
    if (!page || !HasPermission(page->permissions, MemoryPermission::Write)) {
        ReportFault("Write8", address, /*is_write=*/true);
        return;
    }
    page->host_ptr[address & PAGE_MASK] = value;
}

void VirtualMemory::Write16(vaddr_t address, u16 value) {
    WriteBlock(address, &value, sizeof(value));
}

void VirtualMemory::Write32(vaddr_t address, u32 value) {
    WriteBlock(address, &value, sizeof(value));
}

void VirtualMemory::Write64(vaddr_t address, u64 value) {
    WriteBlock(address, &value, sizeof(value));
}

bool VirtualMemory::ReadBlock(vaddr_t address, void* dest, size_t size) {
    if (!dest || size == 0) return false;
    std::lock_guard lock(memory_mutex_);

    u8* out = static_cast<u8*>(dest);
    size_t bytes_left = size;
    vaddr_t curr_addr = address;

    while (bytes_left > 0) {
        const PageInfo* page = LookupPage(curr_addr);
        if (!page || !HasPermission(page->permissions, MemoryPermission::Read)) {
            ReportFault("ReadBlock", curr_addr, /*is_write=*/false);
            return false;
        }

        const size_t offset_in_page = curr_addr & PAGE_MASK;
        const size_t chunk_size = std::min(bytes_left, PAGE_SIZE - offset_in_page);
        std::memcpy(out, page->host_ptr + offset_in_page, chunk_size);

        out += chunk_size;
        curr_addr += chunk_size;
        bytes_left -= chunk_size;
    }

    return true;
}

bool VirtualMemory::WriteBlock(vaddr_t address, const void* src, size_t size) {
    if (!src || size == 0) return false;
    std::lock_guard lock(memory_mutex_);

    const u8* in = static_cast<const u8*>(src);
    size_t bytes_left = size;
    vaddr_t curr_addr = address;

    while (bytes_left > 0) {
        PageInfo* page = LookupPage(curr_addr);
        if (!page || !HasPermission(page->permissions, MemoryPermission::Write)) {
            ReportFault("WriteBlock", curr_addr, /*is_write=*/true);
            return false;
        }

        const size_t offset_in_page = curr_addr & PAGE_MASK;
        const size_t chunk_size = std::min(bytes_left, PAGE_SIZE - offset_in_page);
        std::memcpy(page->host_ptr + offset_in_page, in, chunk_size);

        in += chunk_size;
        curr_addr += chunk_size;
        bytes_left -= chunk_size;
    }

        if (write_hook_) {
        write_hook_(address, size);  // SMC: notify JIT of executable-range write
    }
return true;
}

u8* VirtualMemory::GetPointer(vaddr_t address) {
    std::lock_guard lock(memory_mutex_);
    PageInfo* page = LookupPage(address);
    if (!page) return nullptr;
    return page->host_ptr + (address & PAGE_MASK);
}

const u8* VirtualMemory::GetPointer(vaddr_t address) const {
    std::lock_guard lock(memory_mutex_);
    const PageInfo* page = LookupPage(address);
    if (!page) return nullptr;
    return page->host_ptr + (address & PAGE_MASK);
}

bool VirtualMemory::IsValidAddress(vaddr_t address, size_t size) const {
    std::lock_guard lock(memory_mutex_);
    size_t bytes_left = size;
    vaddr_t curr_addr = address;

    while (bytes_left > 0) {
        const PageInfo* page = LookupPage(curr_addr);
        if (!page) return false;

        const size_t offset_in_page = curr_addr & PAGE_MASK;
        const size_t chunk_size = std::min(bytes_left, PAGE_SIZE - offset_in_page);
        curr_addr += chunk_size;
        bytes_left -= chunk_size;
    }

    return true;
}

std::optional<MemoryPermission> VirtualMemory::GetPagePermissions(vaddr_t address) const {
    std::lock_guard lock(memory_mutex_);
    const PageInfo* page = LookupPage(address);
    if (!page) return std::nullopt;
    return page->permissions;
}

} // namespace nemu::core::memory
