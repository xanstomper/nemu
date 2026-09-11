#pragma once

#include "core/types.hpp"
#include <memory>
#include <string_view>
#include <atomic>

namespace nemu::core::kernel {

enum class HandleType : u8 {
    Unknown,
    Process,
    Thread,
    Event,
    SharedMemory,
    Port,
    Session
};

class KAutoObject : public std::enable_shared_from_this<KAutoObject> {
public:
    explicit KAutoObject(HandleType type) : type_(type) {}
    virtual ~KAutoObject() = default;

    [[nodiscard]] HandleType GetType() const noexcept { return type_; }
    [[nodiscard]] virtual std::string_view GetTypeName() const noexcept { return "KAutoObject"; }

    virtual void OnClose() {}

private:
    HandleType type_{HandleType::Unknown};
};

using Handle = u32;
constexpr Handle InvalidHandle = 0;

} // namespace nemu::core::kernel
