#pragma once

#include "core/types.hpp"
#include <span>
#include <optional>

namespace nemu::core::memory {

enum class MemoryPermission : u8 {
    None    = 0,
    Read    = 1 << 0,
    Write   = 1 << 1,
    Execute = 1 << 2,
    ReadWrite = Read | Write,
    ReadExecute = Read | Execute,
    All = Read | Write | Execute
};

constexpr MemoryPermission operator|(MemoryPermission a, MemoryPermission b) noexcept {
    return static_cast<MemoryPermission>(static_cast<u8>(a) | static_cast<u8>(b));
}

constexpr MemoryPermission operator&(MemoryPermission a, MemoryPermission b) noexcept {
    return static_cast<MemoryPermission>(static_cast<u8>(a) & static_cast<u8>(b));
}

constexpr bool HasPermission(MemoryPermission perms, MemoryPermission required) noexcept {
    return (static_cast<u8>(perms) & static_cast<u8>(required)) == static_cast<u8>(required);
}

class IMemory {
public:
    virtual ~IMemory() = default;

    virtual u8 Read8(vaddr_t address) = 0;
    virtual u16 Read16(vaddr_t address) = 0;
    virtual u32 Read32(vaddr_t address) = 0;
    virtual u64 Read64(vaddr_t address) = 0;

    virtual void Write8(vaddr_t address, u8 value) = 0;
    virtual void Write16(vaddr_t address, u16 value) = 0;
    virtual void Write32(vaddr_t address, u32 value) = 0;
    virtual void Write64(vaddr_t address, u64 value) = 0;

    virtual bool ReadBlock(vaddr_t address, void* dest, size_t size) = 0;
    virtual bool WriteBlock(vaddr_t address, const void* src, size_t size) = 0;

    virtual u8* GetPointer(vaddr_t address) = 0;
    virtual const u8* GetPointer(vaddr_t address) const = 0;

    virtual bool IsValidAddress(vaddr_t address, size_t size = 1) const = 0;
};

} // namespace nemu::core::memory
