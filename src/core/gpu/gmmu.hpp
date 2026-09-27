#pragma once

#include "core/types.hpp"
#include <array>
#include <memory>
#include <shared_mutex>
#include <unordered_map>
#include <vector>

namespace nemu::core::memory { class VirtualMemory; }

namespace nemu::core::gpu {

// ---------------------------------------------------------------------------
// GpuMemoryManager (GMMU): guest global GPU address space.
//
// Real Switch games address GPU buffers/textures by GPU *virtual* addresses
// (big-VA space separate from CPU pointers, mapped via nvhost-as-gpu
// Remap/AllocAsEx). This manager models that space so LDG/STG shader access,
// compute, and buffer caches can all resolve a guest GPU address to bytes
// without host pointer casts.
//
// Design (mirrors yuzu src/video_core/memory and Ryujinx
// Ryujinx.Graphics.Gpu.Memory.MemoryManager in simplified form):
//   - Sparse multi-granularity page table: 64 KiB (big) pages carve the 40-bit
//     GPU VA space; each mapped big page owns a backing host buffer.
//   - Region coalescing: Map()/Unmap() track ranges so Read/Write crossing
//     several mappings works like the real card's cross-page path.
//   - Thread-safe: the GPU thread and IPC nvhost calls race.
//
// Physical ( carveout) addresses: guest nvmap handles hand out an address
// whose high bits mark it. We model the two standard regions:
//   - 0x0000'0000'00 region = "small page" heap
//   - large region = big pages
// but internally we just key on 64 KiB pages everywhere for simplicity; the
// real GM20B mixed-page split is a perf optimization, not correctness.
// ---------------------------------------------------------------------------
class GpuMemoryManager {
public:
    // 40-bit GPU VA space (nvhost-as-gpu allocates up to 1 TB on Tegra X1).
    static constexpr u64 kGpuVaBits = 40;
    static constexpr u64 kGpuVaLimit = 1ULL << kGpuVaBits;
    // Big page granularity used by this GMMU (Tegra supports 64 KiB/128 KiB;
    // 64 KiB covers real workloads without bloating the page table).
    static constexpr u64 kBigPageSize = 64 * 1024;
    static constexpr u64 kBigPageCount = kGpuVaLimit / kBigPageSize;
    // GPU addresses are sparse; we only allocate page state lazily.

    explicit GpuMemoryManager(memory::VirtualMemory* cpu_as);

    // Map a GPU VA range [addr, addr+size) to a host CPU address range
    // (the backing guest memory nvmap exported). Returns true on success.
    // Overlapping an existing mapping replaces it (games remap constantly).
    bool Map(u64 gpu_addr, u64 size, u8* host_backing);

    // Unmap + free page state for [addr, addr+size).
    void Unmap(u64 gpu_addr, u64 size);

    [[nodiscard]] bool IsMapped(u64 gpu_addr, u64 size) const;

    // Read/Write bytes at a GPU virtual address. Cross-page safe. Returns the
    // number of bytes actually transferred (0 if the range is unmapped).
    [[nodiscard]] size_t Read(u64 gpu_addr, void* out, u64 size) const;
    [[nodiscard]] size_t Write(u64 gpu_addr, const void* in, u64 size);

    // Guest physical<->host pointer translation for a single byte. Returns
    // nullptr if unmapped; the caller may only use the pointer for the page
    // extent they verified (IsMapped/size clamps).
    [[nodiscard]] u8* GetHostPointer(u64 gpu_addr) const;

    // Flush stats for diagnostics.
    struct Stats {
        u64 mapped_pages{0};
        u64 read_calls{0};
        u64 write_calls{0};
        u64 bytes_read{0};
        u64 bytes_written{0};
    };
    [[nodiscard]] const Stats& GetStats() const noexcept { return stats_; }

private:
    struct BigPage {
        u8* backing{nullptr}; // host pointer; null = unmapped
        u32 page_offset{0};   // offset of this big page inside the mapping
        u64 map_size{0};      // size of the owning mapping (for range checks)
    };

    mutable std::shared_mutex mutex_;
    memory::VirtualMemory* cpu_as_;
    // Sparse page table: only mapped pages exist.
    std::unordered_map<u64, BigPage> pages_;
    // Mutable so const Read() can record stats.
    mutable Stats stats_;
};

} // namespace nemu::core::gpu
