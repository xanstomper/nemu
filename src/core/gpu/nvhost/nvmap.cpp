#include "nvmap.hpp"
#include "platform/logger.hpp"

namespace nemu::core::gpu::nvhost {

u32 NvMap::Create(u64 size) {
    if (size == 0) return 0;
    std::unique_lock lock(mutex_);
    const u32 handle = next_handle_++;
    auto obj = std::make_shared<NvMapObject>();
    obj->handle = handle;
    obj->size = size;
    handles_[handle] = obj;
    NEMU_LOG_DEBUG("NvMap", "Created nvmap handle=0x{:X}, size=0x{:X}", handle, size);
    return handle;
}

bool NvMap::Alloc(u32 handle, u32 heap_mask, u32 flags, u32 align, u8 kind, vaddr_t guest_va) {
    std::unique_lock lock(mutex_);
    auto it = handles_.find(handle);
    if (it == handles_.end()) {
        NEMU_LOG_WARN("NvMap", "Alloc: handle 0x{:X} not found", handle);
        return false;
    }
    auto& obj = it->second;
    obj->flags = flags;
    obj->align = (align == 0) ? 4096 : align;
    obj->kind = kind;
    obj->guest_va = guest_va;
    obj->is_allocated = true;
    (void)heap_mask;
    NEMU_LOG_DEBUG("NvMap", "Allocated nvmap handle=0x{:X}, va=0x{:X}, align=0x{:X}, kind={}",
                   handle, guest_va, obj->align, kind);
    return true;
}

bool NvMap::Free(u32 handle) {
    std::unique_lock lock(mutex_);
    auto it = handles_.find(handle);
    if (it == handles_.end()) {
        return false;
    }
    if (it->second->id != 0) {
        ids_.erase(it->second->id);
    }
    handles_.erase(it);
    NEMU_LOG_DEBUG("NvMap", "Freed nvmap handle 0x{:X}", handle);
    return true;
}

bool NvMap::GetParam(u32 handle, u32 param, u32& out_result) const {
    std::unique_lock lock(mutex_);
    auto it = handles_.find(handle);
    if (it == handles_.end()) {
        return false;
    }
    const auto& obj = it->second;
    switch (param) {
        case 1: // Size
            out_result = static_cast<u32>(obj->size);
            return true;
        case 2: // Alignment
            out_result = obj->align;
            return true;
        case 3: // Base guest VA (low 32-bits)
            out_result = static_cast<u32>(obj->guest_va);
            return true;
        case 4: // Heap
            out_result = 0; // Default heap
            return true;
        case 5: // Kind
            out_result = obj->kind;
            return true;
        default:
            return false;
    }
}

u32 NvMap::GetId(u32 handle) {
    std::unique_lock lock(mutex_);
    auto it = handles_.find(handle);
    if (it == handles_.end()) return 0;
    auto& obj = it->second;
    if (obj->id == 0) {
        obj->id = next_id_++;
        ids_[obj->id] = obj;
    }
    return obj->id;
}

std::shared_ptr<NvMapObject> NvMap::GetObject(u32 handle) const {
    std::unique_lock lock(mutex_);
    auto it = handles_.find(handle);
    return (it != handles_.end()) ? it->second : nullptr;
}

std::shared_ptr<NvMapObject> NvMap::GetObjectById(u32 id) const {
    std::unique_lock lock(mutex_);
    auto it = ids_.find(id);
    return (it != ids_.end()) ? it->second : nullptr;
}

size_t NvMap::GetObjectCount() const {
    std::unique_lock lock(mutex_);
    return handles_.size();
}

} // namespace nemu::core::gpu::nvhost
