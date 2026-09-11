#pragma once

#include "core/kernel/k_auto_object.hpp"
#include "core/types.hpp"
#include <string_view>

namespace nemu::core::kernel::ipc {

/// A guest-visible shared memory object (HandleType::SharedMemory). Owns the
/// guest virtual address of the mapped region that the hid: service populates.
class KSharedMemory final : public KAutoObject {
public:
    KSharedMemory() : KAutoObject(kernel::HandleType::SharedMemory) {}

    [[nodiscard]] std::string_view GetTypeName() const noexcept override {
        return "KSharedMemory";
    }

    void SetAddress(vaddr_t address) noexcept { address_ = address; }
    [[nodiscard]] vaddr_t GetAddress() const noexcept { return address_; }

private:
    vaddr_t address_{0};
};

} // namespace nemu::core::kernel::ipc