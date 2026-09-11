#pragma once

#include "k_auto_object.hpp"
#include "core/memory/virtual_memory.hpp"
#include <vector>
#include <mutex>

namespace nemu::core::kernel {

class KSharedMemory final : public KAutoObject {
public:
    KSharedMemory(size_t size, memory::MemoryPermission owner_perm, memory::MemoryPermission user_perm);
    ~KSharedMemory() override = default;

    [[nodiscard]] std::string_view GetTypeName() const noexcept override { return "KSharedMemory"; }

    [[nodiscard]] size_t GetSize() const noexcept { return size_; }
    [[nodiscard]] memory::MemoryPermission GetOwnerPerm() const noexcept { return owner_perm_; }
    [[nodiscard]] memory::MemoryPermission GetUserPerm() const noexcept { return user_perm_; }

    [[nodiscard]] u8* GetBacking() noexcept { return backing_.data(); }
    [[nodiscard]] const u8* GetBacking() const noexcept { return backing_.data(); }

    bool MapInto(memory::VirtualMemory& vm, vaddr_t address, memory::MemoryPermission perm);
    bool UnmapFrom(memory::VirtualMemory& vm, vaddr_t address);

private:
    size_t size_{0};
    memory::MemoryPermission owner_perm_{memory::MemoryPermission::ReadWrite};
    memory::MemoryPermission user_perm_{memory::MemoryPermission::Read};
    std::vector<u8> backing_;
    std::mutex mutex_;
};

} // namespace nemu::core::kernel
