#include "k_handle_table.hpp"

namespace nemu::core::kernel {

KHandleTable::KHandleTable() = default;
KHandleTable::~KHandleTable() {
    Clear();
}

Handle KHandleTable::CreateHandle(std::shared_ptr<KAutoObject> obj) {
    if (!obj) return InvalidHandle;

    std::lock_guard lock(mutex_);
    if (handles_.size() >= MAX_HANDLES) {
        return InvalidHandle;
    }

    // Find next available handle id
    for (size_t i = 0; i < MAX_HANDLES; ++i) {
        Handle h = next_handle_++;
        if (h == InvalidHandle) {
            h = next_handle_++;
        }
        if (!handles_.contains(h)) {
            handles_[h] = std::move(obj);
            return h;
        }
    }

    return InvalidHandle;
}

bool KHandleTable::CloseHandle(Handle handle) {
    if (handle == InvalidHandle) return false;

    std::shared_ptr<KAutoObject> obj;
    {
        std::lock_guard lock(mutex_);
        auto it = handles_.find(handle);
        if (it == handles_.end()) {
            return false;
        }
        obj = std::move(it->second);
        handles_.erase(it);
    }

    if (obj) {
        obj->OnClose();
    }
    return true;
}

std::shared_ptr<KAutoObject> KHandleTable::GetObject(Handle handle) const {
    if (handle == InvalidHandle) return nullptr;

    std::lock_guard lock(mutex_);
    auto it = handles_.find(handle);
    if (it != handles_.end()) {
        return it->second;
    }
    return nullptr;
}

bool KHandleTable::IsValid(Handle handle) const {
    if (handle == InvalidHandle) return false;

    std::lock_guard lock(mutex_);
    return handles_.contains(handle);
}

void KHandleTable::Clear() {
    std::unordered_map<Handle, std::shared_ptr<KAutoObject>> old_handles;
    {
        std::lock_guard lock(mutex_);
        old_handles = std::move(handles_);
        handles_.clear();
    }

    for (auto& [h, obj] : old_handles) {
        if (obj) {
            obj->OnClose();
        }
    }
}

} // namespace nemu::core::kernel
