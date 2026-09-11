#pragma once

#include "core/types.hpp"

namespace nemu::core::kernel::ipc::hid {

/// Shared-memory HLE layout produced by the hid: service. Guest input code
/// (e.g. libnx hidGetControllerStates) reads pad entries from this buffer.
/// Mirroring the Switch's dual ring (local + global), we expose two entries.

/// Button bitmask (subset of libnx HidNpadButton).
enum class HidButton : u32 {
    A     = 1u << 0,
    B     = 1u << 1,
    X     = 1u << 2,
    Y     = 1u << 3,
    L     = 1u << 4,
    R     = 1u << 5,
    Plus  = 1u << 6,
    Minus = 1u << 7,
    Up    = 1u << 8,
    Down  = 1u << 9,
    Left  = 1u << 10,
    Right = 1u << 11,
};

/// A single pad sample entry. 0x18 bytes.
struct SharedPadEntry {
    u32 buttons{0};
    s16 stick_l_x{0};
    s16 stick_l_y{0};
    s16 stick_r_x{0};
    s16 stick_r_y{0};
    u32 sensor_bits{0};
    u64 timestamp{0};
};

static_assert(sizeof(SharedPadEntry) == 0x18, "SharedPadEntry must be 0x18 bytes");

/// Header of the hid shared-memory page.
struct SharedPadHeader {
    static constexpr u32 kEntryCount = 2; // local + global
    static constexpr u32 kHeaderSize = 0x10;

    u64 entry_count{kEntryCount};
    u64 entry_size{sizeof(SharedPadEntry)};
    SharedPadEntry local{};
    SharedPadEntry global{};
};

static_assert(sizeof(SharedPadHeader) >= 0x10 + 2 * sizeof(SharedPadEntry),
              "SharedPadHeader layout overflow");

} // namespace nemu::core::kernel::ipc::hid