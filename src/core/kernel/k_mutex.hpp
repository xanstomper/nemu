#pragma once

#include "k_auto_object.hpp"
#include <mutex>
#include <condition_variable>

namespace nemu::core::kernel {

class KMutex final : public KAutoObject {
public:
    KMutex();
    ~KMutex() override = default;

    [[nodiscard]] std::string_view GetTypeName() const noexcept override { return "KMutex"; }

    bool TryLock(u64 tid);
    void Lock(u64 tid);
    bool Unlock(u64 tid);

    [[nodiscard]] bool IsLocked() const noexcept;
    [[nodiscard]] u64 GetOwnerTid() const noexcept;
    [[nodiscard]] u32 GetRecursiveCount() const noexcept;

private:
    mutable std::mutex mutex_;
    std::condition_variable cv_;
    u64 owner_tid_{0};
    u32 recursive_count_{0};
};

} // namespace nemu::core::kernel
