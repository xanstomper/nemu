#pragma once

#include "k_auto_object.hpp"
#include <unordered_map>
#include <mutex>
#include <optional>

namespace nemu::core::kernel {

class KHandleTable {
public:
    static constexpr size_t MAX_HANDLES = 1024;

    KHandleTable();
    ~KHandleTable();

    KHandleTable(const KHandleTable&) = delete;
    KHandleTable& operator=(const KHandleTable&) = delete;

    Handle CreateHandle(std::shared_ptr<KAutoObject> obj);
    bool CloseHandle(Handle handle);

    std::shared_ptr<KAutoObject> GetObject(Handle handle) const;

    template <typename T>
    std::shared_ptr<T> GetObject(Handle handle) const {
        auto obj = GetObject(handle);
        if (!obj) return nullptr;
        return std::dynamic_pointer_cast<T>(obj);
    }

    [[nodiscard]] bool IsValid(Handle handle) const;
    void Clear();

private:
    mutable std::mutex mutex_;
    std::unordered_map<Handle, std::shared_ptr<KAutoObject>> handles_;
    Handle next_handle_{1};
};

} // namespace nemu::core::kernel
