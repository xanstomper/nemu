#pragma once

// Clean-room reimplementation of a Horizon-style packed result code (the result-
// value design used throughout the Switch OS and its emulators: a 32-bit word
// carrying module, description, and a level/flag bit). Written fresh in Nemu's
// style; no source text copied. Kept as a new, opt-in type so existing code that
// uses the flat Result enum is unaffected.

#include "core/types.hpp"
#include <cstdint>

namespace nemu::core::common {

/// A packed Nintendo Switch result code.
///
/// Bit layout (Horizon): [31:18] undefined/printf, [17:10] module (2048
/// ranges), [9] level flag, [8:0] description (512 values). Success is raw 0.
class ResultCode {
public:
    constexpr ResultCode() = default;
    constexpr explicit ResultCode(u32 raw) : raw_(raw) {}

    /// Build from module/description/level. Description and module are masked to
    /// their fields; a non-zero description together with level != Success marks
    /// the result as an error.
    constexpr ResultCode(u32 module, u32 description, u32 level) noexcept
        : raw_((module & 0x7FFu) << 10u | (level & 1u) << 9u | (description & 0x1FFu)) {}

    [[nodiscard]] constexpr u32 raw() const noexcept { return raw_; }
    [[nodiscard]] constexpr bool IsSuccess() const noexcept { return raw_ == 0; }
    [[nodiscard]] constexpr bool IsError() const noexcept { return raw_ != 0; }

    [[nodiscard]] constexpr u32 GetModule() const noexcept { return (raw_ >> 10u) & 0x7FFu; }
    [[nodiscard]] constexpr u32 GetDescription() const noexcept { return raw_ & 0x1FFu; }
    [[nodiscard]] constexpr u32 GetLevel() const noexcept { return (raw_ >> 9u) & 1u; }

    constexpr bool operator==(const ResultCode& o) const noexcept { return raw_ == o.raw_; }
    constexpr bool operator!=(const ResultCode& o) const noexcept { return raw_ != o.raw_; }
    constexpr bool operator==(u32 r) const noexcept { return raw_ == r; }

    static constexpr ResultCode Success() noexcept { return ResultCode(0u); }

private:
    u32 raw_{0};
};

} // namespace nemu::core::common