#pragma once

#include <cstdint>
#include <cstddef>
#include <bit>
#include <string_view>
#include <type_traits>

namespace nemu {

using u8 = uint8_t;
using u16 = uint16_t;
using u32 = uint32_t;
using u64 = uint64_t;

using s8 = int8_t;
using s16 = int16_t;
using s32 = int32_t;
using s64 = int64_t;

using vaddr_t = u64;
using paddr_t = u64;

struct alignas(16) u128 {
    u64 low{0};
    u64 high{0};

    constexpr bool operator==(const u128& other) const noexcept {
        return low == other.low && high == other.high;
    }
    constexpr bool operator!=(const u128& other) const noexcept {
        return !(*this == other);
    }
};

// Bit extraction helpers
template <typename T>
constexpr T ExtractBit(T value, size_t bit) noexcept {
    return (value >> bit) & T{1};
}

template <typename T>
constexpr T ExtractBits(T value, size_t start, size_t count) noexcept {
    if (count == 0) return 0;
    if (count >= sizeof(T) * 8) return value >> start;
    const T mask = (T{1} << count) - T{1};
    return (value >> start) & mask;
}

template <typename T>
constexpr T SignExtend(T value, size_t original_bits) noexcept {
    const size_t shift = (sizeof(T) * 8) - original_bits;
    using SignedT = std::make_signed_t<T>;
    return static_cast<T>((static_cast<SignedT>(value) << shift) >> shift);
}

} // namespace nemu
