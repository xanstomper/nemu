#include "nvhost_as_gpu.hpp"
#include "platform/logger.hpp"

namespace nemu::core::gpu::nvhost {

AddressSpace::AddressSpace(std::shared_ptr<NvMap> nvmap)
    : nvmap_(std::move(nvmap)) {}

u64 AddressSpace::AllocSpace(u32 pages, u32 page_size, u32 flags, u64 fixed_offset) {
    std::unique_lock lock(mutex_);
    const u64 size = static_cast<u64>(pages) * (page_size == 0 ? 4096 : page_size);
    u64 assigned_va = fixed_offset;
    if (assigned_va == 0) {
        assigned_va = current_gpu_va_;
        current_gpu_va_ = (current_gpu_va_ + size + 0xFFFFULL) & ~0xFFFFULL;
    }
    (void)flags;
    NEMU_LOG_DEBUG("AddressSpace", "AllocSpace: pages={}, size=0x{:X}, assigned gpu_va=0x{:X}",
                   pages, size, assigned_va);
    return assigned_va;
}

bool AddressSpace::FreeSpace(u64 offset, u32 pages, u32 page_size) {
    (void)offset; (void)pages; (void)page_size;
    return true;
}

u64 AddressSpace::MapBufferEx(u32 nvmap_handle, u32 flags, u32 page_size, u64 fixed_offset) {
    if (!nvmap_) return 0;
    auto obj = nvmap_->GetObject(nvmap_handle);
    if (!obj) {
        NEMU_LOG_WARN("AddressSpace", "MapBufferEx: nvmap handle 0x{:X} not found", nvmap_handle);
        return 0;
    }

    std::unique_lock lock(mutex_);
    const u64 size = (obj->size + 0xFFFULL) & ~0xFFFULL;
    u64 assigned_va = fixed_offset;
    if (assigned_va == 0) {
        assigned_va = current_gpu_va_;
        current_gpu_va_ = (current_gpu_va_ + size + 0xFFFFULL) & ~0xFFFFULL;
    }

    GpuMapping mapping{};
    mapping.gpu_va = assigned_va;
    mapping.size = size;
    mapping.nvmap_handle = nvmap_handle;
    mapping.guest_va = obj->guest_va;
    mapping.flags = flags;
    mapping.page_size = page_size == 0 ? 4096 : page_size;

    mappings_[assigned_va] = mapping;
    NEMU_LOG_DEBUG("AddressSpace", "Mapped nvmap 0x{:X} (guest_va=0x{:X}, size=0x{:X}) -> gpu_va=0x{:X}",
                   nvmap_handle, obj->guest_va, size, assigned_va);
    return assigned_va;
}

bool AddressSpace::UnmapBuffer(u64 offset) {
    std::unique_lock lock(mutex_);
    auto it = mappings_.find(offset);
    if (it == mappings_.end()) return false;
    mappings_.erase(it);
    NEMU_LOG_DEBUG("AddressSpace", "Unmapped buffer at gpu_va=0x{:X}", offset);
    return true;
}

std::optional<vaddr_t> AddressSpace::GpuVaToGuestVa(u64 gpu_va) const {
    std::unique_lock lock(mutex_);
    if (mappings_.empty()) return std::nullopt;

    auto it = mappings_.upper_bound(gpu_va);
    if (it != mappings_.begin()) {
        --it;
        const auto& m = it->second;
        if (gpu_va >= m.gpu_va && gpu_va < m.gpu_va + m.size) {
            const u64 offset = gpu_va - m.gpu_va;
            return m.guest_va + offset;
        }
    }
    return std::nullopt;
}

std::optional<GpuMapping> AddressSpace::FindMapping(u64 gpu_va) const {
    std::unique_lock lock(mutex_);
    if (mappings_.empty()) return std::nullopt;

    auto it = mappings_.upper_bound(gpu_va);
    if (it != mappings_.begin()) {
        --it;
        const auto& m = it->second;
        if (gpu_va >= m.gpu_va && gpu_va < m.gpu_va + m.size) {
            return m;
        }
    }
    return std::nullopt;
}

} // namespace nemu::core::gpu::nvhost
