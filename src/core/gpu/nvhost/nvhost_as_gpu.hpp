#pragma once

#include "core/types.hpp"
#include "nvmap.hpp"
#include <mutex>
#include <map>
#include <memory>
#include <optional>

namespace nemu::core::gpu::nvhost {

struct GpuMapping {
    u64 gpu_va{0};
    u64 size{0};
    u32 nvmap_handle{0};
    vaddr_t guest_va{0};
    u32 flags{0};
    u32 page_size{4096};
};

class AddressSpace {
public:
    explicit AddressSpace(std::shared_ptr<NvMap> nvmap);
    ~AddressSpace() = default;

    AddressSpace(const AddressSpace&) = delete;
    AddressSpace& operator=(const AddressSpace&) = delete;

    /// Allocate virtual GPU space range. Returns the assigned gpu_va offset.
    u64 AllocSpace(u32 pages, u32 page_size, u32 flags, u64 fixed_offset = 0);

    /// Free virtual GPU space range.
    bool FreeSpace(u64 offset, u32 pages, u32 page_size);

    /// Map an nvmap handle into GPU address space.
    /// If offset != 0 and flags specify fixed, uses that offset; otherwise assigns dynamically.
    u64 MapBufferEx(u32 nvmap_handle, u32 flags, u32 page_size, u64 fixed_offset = 0);

    /// Unmap GPU buffer at offset.
    bool UnmapBuffer(u64 offset);

    /// Translate a GPU virtual address to a guest virtual address.
    [[nodiscard]] std::optional<vaddr_t> GpuVaToGuestVa(u64 gpu_va) const;

    /// Look up mapping containing gpu_va.
    [[nodiscard]] std::optional<GpuMapping> FindMapping(u64 gpu_va) const;

    [[nodiscard]] std::shared_ptr<NvMap> GetNvMap() const noexcept { return nvmap_; }

private:
    std::shared_ptr<NvMap> nvmap_;
    mutable std::mutex mutex_;
    u64 current_gpu_va_{0x1000'0000ULL}; // Start at 256 MiB mark
    std::map<u64, GpuMapping> mappings_;  // keyed by gpu_va
};

} // namespace nemu::core::gpu::nvhost
