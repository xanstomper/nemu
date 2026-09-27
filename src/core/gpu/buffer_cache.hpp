#pragma once

#include "core/types.hpp"
#include "gmmu.hpp"
#include <array>
#include <memory>
#include <shared_mutex>
#include <unordered_map>
#include <vector>

namespace nemu::core::gpu {

// ---------------------------------------------------------------------------
// BufferCache: guest GPU buffer management with dirty-range tracking.
//
// Mirrors yuzu's BufferCache (src/video_core/buffer_cache/buffer_cache.h)
// design in simplified form: guest games bind vertex/index/uniform/storage
// buffers by GPU address + size. The cache keeps a host-side upload buffer per
// guest range, tracks which byte ranges the guest dirtied since the last
// backend upload, and re-uploads only those ranges (streaming upload, no
// per-draw full copies).
//
// Backends hook in through a small interface so Null/SDL2/D3D12 all share the
// same caching layer.
// ---------------------------------------------------------------------------
class BufferCache {
public:
    // Buffer types match the guest bind groups.
    enum class Type : u32 {
        Vertex = 0,
        Index = 1,
        Uniform = 2,   // UBO / constant buffer
        Storage = 3,   // SSBO
        Count = 4
    };

    struct Stats {
        u64 hits{0};
        u64 misses{0};
        u64 partial_updates{0};
        u64 full_uploads{0};
        u64 evictions{0};
        u64 bytes_uploaded{0};
        u64 dedup_saves{0};   // uploads skipped via covering-buffer coalescing
    };

    explicit BufferCache(std::shared_ptr<GpuMemoryManager> gmmu);
    ~BufferCache();

    // Bind/return the cache entry for a guest buffer at [gpu_addr, gpu_addr+size).
    // Creates + uploads the buffer on first use (miss) and applies any dirty
    // ranges the guest wrote since the last bind (partial update).
    // Returns the entry id (stable across binds while cached).
    [[nodiscard]] u64 Acquire(Type type, u64 gpu_addr, u64 size);

    // Mark a guest byte range dirty (the guest wrote to it via CPU/GPU).
    // The next Acquire() re-uploads exactly these ranges.
    void MarkDirty(u64 gpu_addr, u64 size);

    // Invalidate (e.g. on GMMU remap).
    void InvalidateRange(u64 gpu_addr, u64 size);

    // Direct read of the current cached bytes (for backends that need a pointer).
    [[nodiscard]] bool ReadEntry(u64 entry_id, u64 offset, void* out, u64 bytes) const;

    [[nodiscard]] const Stats& GetStats() const noexcept { return stats_; }
    void ResetStats() { stats_ = {}; }

private:
    struct Entry {
        Type type;
        u64 gpu_addr;
        u64 size;
        std::vector<u8> data;
        // Dirty ranges as [start, end) pairs relative to buffer start,
        // merged on insert so upload passes stay O(dirty ranges).
        std::vector<std::pair<u64, u64>> dirty;
        u64 last_used_frame{0};
    };

    Entry* Find(Type type, u64 gpu_addr, u64 size);
    Entry* FindCovering(Type type, u64 gpu_addr, u64 size, const Entry* ignore);
    Entry& Create(Type type, u64 gpu_addr, u64 size);
    void UploadDirty(Entry& e);
    void EvictFrame(u64 current_frame);

    std::shared_ptr<GpuMemoryManager> gmmu_;
    std::unordered_map<u64, Entry> entries_;
    u64 next_entry_id_{1};
    u64 frame_{0};
    // LRU bound: entries beyond this count are evicted oldest-first.
    static constexpr size_t kMaxEntries = 512;
    Stats stats_;
    mutable std::shared_mutex mutex_;
};

} // namespace nemu::core::gpu
