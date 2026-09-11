#pragma once

#include "k_auto_object.hpp"
#include <mutex>
#include <condition_variable>
#include <chrono>

namespace nemu::core::kernel {

class KEvent final : public KAutoObject {
public:
    explicit KEvent(bool auto_clear = true);
    ~KEvent() override = default;

    [[nodiscard]] std::string_view GetTypeName() const noexcept override { return "KEvent"; }

    void Signal();
    void Clear();
    [[nodiscard]] bool IsSignaled() const;

    bool Wait(std::chrono::nanoseconds timeout = std::chrono::nanoseconds::max());

private:
    bool auto_clear_{true};
    bool signaled_{false};
    mutable std::mutex mutex_;
    std::condition_variable cv_;
};

} // namespace nemu::core::kernel
