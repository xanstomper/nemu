#pragma once

#include "core/types.hpp"
#include <cstring>

namespace nemu::core::kernel::ipc::hid {

/// Button bitmask (subset of libnx HidNpadButton) — used by the legacy HLE
/// bridge and tests. Values match libnx's HidNpadButton enumeration.
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

// ---------------------------------------------------------------------------
// Real Switch hid shared-memory ring structures (RingLifo<NpadCommonState>).
//
// Layout derived from Ryujinx's type-size asserts (MIT, Hid.cs:
//   RingLifo<KeyboardState> == 0x3D8 — same shape as NpadCommonState lifo)
// and libnx's canonical hid shared memory map. Real homebrew
// (hidGetNpadStates / hidGetControllerStates) reads exactly this structure,
// so the old NEMU custom header (entry_count/entry_size + 2 entries) could
// never satisfy a real NRO's input path.
//
// RingLifo<T>:
//   +0x00 u64 unused
//   +0x08 u64 buffer_count (must be 17)
//   +0x10 u64 index   (tail for writes)
//   +0x18 u64 count   (valid entries)
//   +0x20 AtomicStorage<T> entries[17]
//
// AtomicStorage<T>:
//   +0x00 u64 sampling_number
//   +0x08 T state
//
// NpadCommonState (0x30):
//   +0x00 u64 sampling_number
//   +0x08 u64 buttons        (HidNpadButton bitmask)
//   +0x10 AnalogStickL { s32 x; s32 y; }
//   +0x18 AnalogStickR { s32 x; s32 y; }
//   +0x20 u32 attributes
//   +0x24 u32 reserved[3]
// ---------------------------------------------------------------------------
static constexpr u64 kRingLifoEntries = 17;
static constexpr u64 kRingLifoHeaderSize = 0x20;

// Analog stick state in shared memory is two s32 values (floats * 
// ATOMIC scale in libnx: s32 range -0x8000..0x8000 for full deflection).
struct AnalogStickState {
    s32 x{0};
    s32 y{0};
};
static_assert(sizeof(AnalogStickState) == 8);

struct NpadCommonState {
    u64 sampling_number{0};
    u64 buttons{0};
    AnalogStickState stick_l{};
    AnalogStickState stick_r{};
    u32 attributes{0};
    u32 reserved[3]{};
};
static_assert(sizeof(NpadCommonState) == 0x30, "NpadCommonState must be 0x30");

template <typename T>
struct AtomicStorage {
    u64 sampling_number{0};
    T state{};
};
static_assert(sizeof(AtomicStorage<NpadCommonState>) == 0x38);

template <typename T>
struct RingLifo {
    u64 unused{0};
    u64 buffer_count{kRingLifoEntries};
    u64 index{0};
    u64 count{0};
    AtomicStorage<T> entries[kRingLifoEntries]{};
};
using NpadCommonLifo = RingLifo<NpadCommonState>;
static_assert(sizeof(NpadCommonLifo) == 0x3D8, "RingLifo<NpadCommonState> must be 0x3D8");

// ---------------------------------------------------------------------------
// Legacy HLE layout (kept for the existing bridge/tests/frontend injection).
// The real-protocol RingLifo lives at the head of the shared page; the legacy
// header follows at kLegacyOffset so both readers stay working.
// ---------------------------------------------------------------------------
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

// The real-protocol NpadCommonLifo is placed at shared page offset 0; the
// legacy HLE header at this offset (page is 4 KiB, both fit comfortably).
static constexpr u64 kLegacyHeaderOffset = 0x100;

// Advance a ring lifo by one sample with the given state (writer side).
template <typename T>
void RingLifoPush(RingLifo<T>& lifo, u64 sampling_number, const T& state) {
    const u64 idx = lifo.index % kRingLifoEntries;
    lifo.entries[idx].sampling_number = sampling_number;
    lifo.entries[idx].state = state;
    lifo.index = (lifo.index + 1) % kRingLifoEntries;
    if (lifo.count < kRingLifoEntries) {
        lifo.count++;
    }
}

} // namespace nemu::core::kernel::ipc::hid
