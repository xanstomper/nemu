#pragma once

#include "core/types.hpp"
#include <mutex>
#include <unordered_map>
#include <memory>
#include <vector>

namespace nemu::core::gpu::nvhost {

struct NvMapObject {
    u32 handle{0};
    u32 id{0};
    u64 size{0};
    u32 flags{0};
    u32 align{4096};
    u8  kind{0};
    vaddr_t guest_va{0};
    bool is_allocated{false};
};

class NvMap {
public:
    NvMap() = default;
    ~NvMap() = default;

    NvMap(const NvMap&) = delete;
    NvMap& operator=(const NvMap&) = delete;

    /// Create an nvmap handle with a specified size. Returns the allocated handle (>0) or 0 on failure.
    u32 Create(u64 size);

    /// Associate guest virtual address and allocation attributes with a handle.
    bool Alloc(u32 handle, u32 heap_mask, u32 flags, u32 align, u8 kind, vaddr_t guest_va);

    /// Release an nvmap handle. Returns true if the handle was valid and freed.
    bool Free(u32 handle);

    /// Query parameter: 1=size, 2=align, 3=base, 4=heap, 5=kind.
    bool GetParam(u32 handle, u32 param, u32& out_result) const;

    /// Get a global ID for sharing the handle across channels.
    u32 GetId(u32 handle);

    /// Look up an object by handle. Returns nullptr if not found.
    [[nodiscard]] std::shared_ptr<NvMapObject> GetObject(u32 handle) const;

    /// Look up an object by global ID. Returns nullptr if not found.
    [[nodiscard]] std::shared_ptr<NvMapObject> GetObjectById(u32 id) const;

    [[nodiscard]] size_t GetObjectCount() const;

private:
    mutable std::mutex mutex_;
    u32 next_handle_{1};
    u32 next_id_{1};
    std::unordered_map<u32, std::shared_ptr<NvMapObject>> handles_;
    std::unordered_map<u32, std::shared_ptr<NvMapObject>> ids_;
};

} // namespace nemu::core::gpu::nvhost
