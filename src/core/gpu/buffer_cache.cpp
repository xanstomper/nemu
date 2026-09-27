#include "buffer_cache.hpp"
#include "platform/logger.hpp"
#include <algorithm>
#include <cstring>

namespace nemu::core::gpu {

BufferCache::BufferCache(std::shared_ptr<GpuMemoryManager> gmmu)
    : gmmu_(std::move(gmmu)) {}

BufferCache::~BufferCache() = default;

BufferCache::Entry* BufferCache::Find(Type type, u64 gpu_addr, u64 size) {
    // Key includes type so vertex and uniform views of the same address stay
    // separate (different format interpret daylight is fine to re-upload).
    const u64 key = (static_cast<u64>(type) << 58) ^ gpu_addr;
    auto it = entries_.find(key);
    if (it == entries_.end()) return nullptr;

    Entry& e = it->second;
    if (e.gpu_addr != gpu_addr || e.size < size) {
        // Address collision with different extent: recreate.
        entries_.erase(it);
        return nullptr;
    }
    return &e;
}

BufferCache::Entry* BufferCache::FindCovering(Type type, u64 gpu_addr, u64 size,
                                              const Entry* ignore) {
    // Coalescing search (Ryujinx CreateBufferAligned/CheckModified overlap):
    // find any buffer of the same type whose range already fully contains the
    // requested guest range. Cheap linear scan — the map is bounded by
    // kMaxEntries and the common case is few covering candidates per bind.
    for (auto& [key, e] : entries_) {
        if (&e == ignore) continue;
        if (e.type != type) continue;
        if (e.gpu_addr <= gpu_addr && (e.gpu_addr + e.size) >= (gpu_addr + size)) {
            // Avoid self (exact-address hit is handled by Find already).
            if (e.gpu_addr == gpu_addr && e.size == size) continue;
            return &e;
        }
    }
    return nullptr;
}

BufferCache::Entry& BufferCache::Create(Type type, u64 gpu_addr, u64 size) {
    const u64 key = (static_cast<u64>(type) << 58) ^ gpu_addr;
    Entry e{};
    e.type = type;
    e.gpu_addr = gpu_addr;
    e.size = size;
    e.data.resize(size);
    e.last_used_frame = frame_;
    auto [it, inserted] = entries_.emplace(key, std::move(e));
    return it->second;
}

u64 BufferCache::Acquire(Type type, u64 gpu_addr, u64 size) {
    if (size == 0 || !gmmu_) return 0;

    std::unique_lock lock(mutex_);
    frame_++;

    Entry* e = Find(type, gpu_addr, size);
    if (!e) {
        stats_.misses++;
        if (entries_.size() >= kMaxEntries) {
            EvictFrame(frame_);
        }
        e = &Create(type, gpu_addr, size);
        // Full upload from GMMU.
        const size_t got = gmmu_->Read(gpu_addr, e->data.data(), size);
        if (got != size) {
            NEMU_LOG_DEBUG("GPU", "BufferCache: partial first read {} / {} B @ 0x{:X}",
                           got, size, gpu_addr);
        }
        e->dirty.emplace_back(0, got);
        stats_.full_uploads++;
    } else {
        stats_.hits++;
        e->last_used_frame = frame_;
        // Coalescing fast path (Ryujinx CheckModified): if a sibling buffer
        // already fully covers this guest range AND it is not dirty, serve the
        // request from that buffer's data instead of re-uploading. This kills
        // redundant uploads of the same guest texture/vertex memory when a
        // larger backing buffer already holds it — a big win under the 5 GiB
        // cap (streaming games touch the same vertex/texture ranges every frame).
        Entry* covering = FindCovering(type, gpu_addr, size, e);
        if (covering && covering->dirty.empty()) {
            // Copy from the covering buffer (hot host-side, no GMMU re-read).
            std::memcpy(e->data.data(), covering->data.data() +
                       (gpu_addr - covering->gpu_addr), size);
            e->dirty.clear();
            stats_.dedup_saves++;
            return (static_cast<u64>(type) << 58) ^ gpu_addr;
        }
    }

    UploadDirty(*e);
    return (static_cast<u64>(type) << 58) ^ gpu_addr; // stable id == hash key
}

void BufferCache::UploadDirty(Entry& e) {
    if (e.dirty.empty()) return;

    // Merge dirty ranges, then upload each (streaming partial upload).
    std::sort(e.dirty.begin(), e.dirty.end());
    std::vector<std::pair<u64, u64>> merged;
    for (auto& [s, en] : e.dirty) {
        if (!merged.empty() && s <= merged.back().second) {
            merged.back().second = std::max(merged.back().second, en);
        } else {
            merged.emplace_back(s, en);
        }
    }
    e.dirty.clear();

    for (const auto& [s, en] : merged) {
        if (en <= s || s >= e.size) continue;
        const u64 end = std::min(en, e.size);
        // The GMMU is authoritative: re-read just these bytes.
        if (gmmu_) {
            const size_t got = gmmu_->Read(e.gpu_addr + s, e.data.data() + s, end - s);
            (void)got;
        }
        stats_.bytes_uploaded += end - s;
        stats_.partial_updates++;
    }
}

void BufferCache::MarkDirty(u64 gpu_addr, u64 size) {
    std::unique_lock lock(mutex_);
    for (auto& [key, e] : entries_) {
        const u64 e_end = e.gpu_addr + e.size;
        const u64 d_end = gpu_addr + size;
        if (gpu_addr < e_end && d_end > e.gpu_addr) {
            const u64 s = std::max(gpu_addr, e.gpu_addr) - e.gpu_addr;
            const u64 en = std::min(d_end, e_end) - e.gpu_addr;
            e.dirty.emplace_back(s, en);
        }
    }
}

void BufferCache::InvalidateRange(u64 gpu_addr, u64 size) {
    std::unique_lock lock(mutex_);
    for (auto it = entries_.begin(); it != entries_.end();) {
        const auto& e = it->second;
        const u64 e_end = e.gpu_addr + e.size;
        if (gpu_addr < e_end && gpu_addr + size > e.gpu_addr) {
            it = entries_.erase(it);
            stats_.evictions++;
        } else {
            ++it;
        }
    }
}

bool BufferCache::ReadEntry(u64 entry_id, u64 offset, void* out, u64 bytes) const {
    std::shared_lock lock(mutex_);
    auto it = entries_.find(entry_id);
    if (it == entries_.end() || offset + bytes > it->second.size) return false;
    std::memcpy(out, it->second.data.data() + offset, bytes);
    return true;
}

void BufferCache::EvictFrame(u64 current_frame) {
    // Evict the least recently used entry.
    auto oldest = entries_.begin();
    for (auto it = entries_.begin(); it != entries_.end(); ++it) {
        if (it->second.last_used_frame < oldest->second.last_used_frame) {
            oldest = it;
        }
    }
    if (oldest != entries_.end()) {
        (void)current_frame;
        oldest = entries_.erase(oldest);
        stats_.evictions++;
    }
}

} // namespace nemu::core::gpu
