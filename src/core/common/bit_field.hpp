#pragma once

// Clean-room reimplementation of a compile-time bit-field accessor (the general
// template technique used throughout the Switch/Wii lineage's common/bit_field)
// to read/write a sub-range of an integer with compile-time masks. Written fresh
// in Nemu's style; no source text copied.

#include "core/types.hpp"
#include <type_traits>

namespace nemu::core::common {

/// Describes a bit-field: `position` (LSB index) and `size` (bit width) within
/// an unsigned `Storage`. Read/Write operate directly on an unsigned word, which
/// avoids reference-binding subtleties and is the common usage.
template <std::size_t Position, std::size_t Size, class Storage>
class BitField {
    static_assert(Position + Size <= sizeof(Storage) * 8u, "field exceeds storage width");
    static_assert(std::is_unsigned_v<Storage>, "BitField storage must be unsigned");

public:
    using StorageType = Storage;

    constexpr static std::size_t position = Position;
    constexpr static std::size_t size = Size;

    /// Extract the field as an unsigned value.
    [[nodiscard]] static constexpr Storage Get(Storage storage) noexcept {
        const Storage mask = static_cast<Storage>((Storage{1} << Size) - Storage{1});
        return static_cast<Storage>((storage >> Position) & mask);
    }

    /// Store `value` into the field of `storage`, preserving other fields.
    [[nodiscard]] static constexpr Storage Set(Storage storage, Storage value) noexcept {
        const Storage mask = static_cast<Storage>((Storage{1} << Size) - Storage{1});
        const Storage field = static_cast<Storage>((value & mask) << Position);
        return static_cast<Storage>((storage & ~static_cast<Storage>(mask << Position)) | field);
    }

    // A thin lvalue wrapper so a register can expose named fields.
    class Ref {
    public:
        constexpr explicit Ref(Storage& host) noexcept : host_(host) {}
        constexpr operator Storage() const noexcept { return BitField::Get(host_); }
        constexpr Ref& operator=(Storage value) noexcept {
            host_ = BitField::Set(host_, value);
            return *this;
        }
        constexpr Storage GetRaw() const noexcept { return BitField::Get(host_); }

    private:
        Storage& host_;
    };
};

} // namespace nemu::core::common