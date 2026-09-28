// SPDX-FileCopyrightText: Copyright 2020 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later
//
// Ported from citron-neo/emulator
// src/core/hle/service/nvdrv/devices/nvhost_nvdec.cpp/.h and
// nvhost_nvdec_common.cpp/.h, adapted to NEMU's NvDeviceFile Ioctl model.
//
// Ioctl command word layout (Horizon nvdrv): bits [31:16] = size, [15:8] =
// cmd, [7:0] = group. Group 0x0 = nvdec engine commands, group 'H' (0x48) =
// host1x channel commands shared with nvhost-gpu.

#include "nvhost_nvdec.hpp"
#include "nvdec.hpp"
#include "nvmap.hpp"
#include "nvhost_ctrl.hpp"
#include "core/memory/virtual_memory.hpp"
#include "platform/logger.hpp"
#include <cstring>
#include <vector>

namespace nemu::core::gpu::nvhost {

namespace {

constexpr u32 kIoctlGroupMask = 0xFFU;
constexpr u32 kIoctlCmdShift = 8;
constexpr u32 kIoctlCmdMask = 0xFFU;

[[nodiscard]] constexpr u32 IoctlGroup(u32 cmd) noexcept { return cmd & kIoctlGroupMask; }
[[nodiscard]] constexpr u32 IoctlCmd(u32 cmd) noexcept {
    return (cmd >> kIoctlCmdShift) & kIoctlCmdMask;
}

// Group 0x0 ioctl command numbers (citron nvhost_nvdec.cpp Ioctl1 case 0x0).
constexpr u32 kCmdSubmit = 0x1;
constexpr u32 kCmdGetSyncpoint = 0x2;
constexpr u32 kCmdGetWaitbase = 0x3;
constexpr u32 kCmdSetSubmitTimeout = 0x7;
constexpr u32 kCmdMapBuffer = 0x9;
constexpr u32 kCmdUnmapBuffer = 0xA;

// Group 'H' (0x48).
constexpr u32 kGroupHost1x = 'H';
constexpr u32 kCmdSetNvmapFd = 0x1;

struct IoctlSubmitHeader { // citron IoctlSubmit, 0x10 bytes
    u32 cmd_buffer_count;
    u32 relocation_count;
    u32 syncpoint_count;
    u32 fence_count;
};
static_assert(sizeof(IoctlSubmitHeader) == 0x10);

struct CommandBuffer { // citron CommandBuffer, 0xC bytes
    s32 memory_id;
    u32 offset;
    s32 word_count;
};
static_assert(sizeof(CommandBuffer) == 0xC);

struct SyncptIncr { // citron SyncptIncr, 0x14 bytes... actual: {u32 id; u32 increments;}
    u32 id;
    u32 increments;
};

struct IoctlGetSyncpointOut {
    u32 value;
};

constexpr u32 kNvResultSuccess = 0;
constexpr u32 kNvResultInvalidState = 0x1;
constexpr u32 kNvResultBadParameter = 0x4;
constexpr u32 kNvResultInsufficientMemory = 0x5;

template <typename T>
[[nodiscard]] bool CopyIn(std::span<const u8> in, T& value) {
    if (in.size() < sizeof(T)) return false;
    std::memcpy(&value, in.data(), sizeof(T));
    return true;
}

template <typename T>
void CopyOut(std::span<u8> out, const T& value) {
    if (out.size() >= sizeof(T)) {
        std::memcpy(out.data(), &value, sizeof(T));
    }
}

} // namespace

NvHostNvdecDevice::NvHostNvdecDevice(std::shared_ptr<NvMap> nvmap,
                                     std::shared_ptr<SyncpointManager> syncpoints,
                                     memory::VirtualMemory* memory)
    : nvmap_(std::move(nvmap))
    , syncpoints_(std::move(syncpoints))
    , memory_(memory)
    , engine_(memory) {}

NvHostNvdecDevice::~NvHostNvdecDevice() = default;

void NvHostNvdecDevice::Close() {
    fd_to_session_.clear();
}

u32 NvHostNvdecDevice::Ioctl(u32 cmd, std::span<const u8> in_buf, std::span<u8> out_buf) {
    const u32 group = IoctlGroup(cmd);
    const u32 io = IoctlCmd(cmd);

    if (group == 0x0) {
        switch (io) {
        case kCmdSubmit: return IoctlSubmit(in_buf, out_buf);
        case kCmdGetSyncpoint: return IoctlGetSyncpoint(in_buf, out_buf);
        case kCmdGetWaitbase: return IoctlGetWaitbase(in_buf, out_buf);
        case kCmdSetSubmitTimeout: return IoctlSetSubmitTimeout(in_buf, out_buf);
        case kCmdMapBuffer: return IoctlMapBuffer(in_buf, out_buf);
        case kCmdUnmapBuffer: return IoctlUnmapBuffer(in_buf, out_buf);
        default: break;
        }
    } else if (group == kGroupHost1x) {
        if (io == kCmdSetNvmapFd) return IoctlSetNvmapFd(in_buf, out_buf);
    }

    NEMU_LOG_WARN("NVDEC", "Unimplemented ioctl 0x{:08X} (group 0x{:02X} cmd 0x{:02X})",
                  cmd, group, io);
    return kNvResultBadParameter;
}

u32 NvHostNvdecDevice::IoctlSetNvmapFd(std::span<const u8> in, std::span<u8> out) {
    u32 nvmap_fd = 0;
    if (!CopyIn(in, nvmap_fd)) return kNvResultBadParameter;
    nvmap_fd_ = nvmap_fd;
    NEMU_LOG_DEBUG("NVDEC", "SetNVMAPfd: fd={}", nvmap_fd_);
    CopyOut(out, nvmap_fd);
    return kNvResultSuccess;
}

u32 NvHostNvdecDevice::IoctlSubmit(std::span<const u8> in, std::span<u8> out) {
    IoctlSubmitHeader params{};
    if (!CopyIn(in, params)) return kNvResultBadParameter;
    NEMU_LOG_DEBUG("NVDEC", "Submit: cmd_buffers={} relocs={} syncpts={} fences={}",
                   params.cmd_buffer_count, params.relocation_count,
                   params.syncpoint_count, params.fence_count);

    // Slice the payload: command buffers then relocs then shifts then syncpt
    // increments then fence thresholds (citron ordering).
    size_t offset = sizeof(IoctlSubmitHeader);
    const u8* base = in.data();
    const size_t avail = in.size();

    for (u32 i = 0; i < params.cmd_buffer_count; ++i) {
        if (offset + sizeof(CommandBuffer) > avail) return kNvResultInvalidState;
        CommandBuffer cb{};
        std::memcpy(&cb, base + offset, sizeof(cb));
        offset += sizeof(cb);

        const auto obj = nvmap_ ? nvmap_->GetObject(static_cast<u32>(cb.memory_id)) : nullptr;
        if (!obj) {
            NEMU_LOG_WARN("NVDEC", "Submit: unknown nvmap handle {}", cb.memory_id);
            return kNvResultInvalidState;
        }
        if (!obj->is_allocated || obj->guest_va == 0) {
            NEMU_LOG_WARN("NVDEC", "Submit: handle {} not allocated", cb.memory_id);
            return kNvResultInvalidState;
        }
        // Feed the engine: the command buffer's words are host1x methods
        // targeting the NVDEC register file. Deliver them word by word.
        const size_t byte_count = static_cast<size_t>(cb.word_count) * sizeof(u32);
        std::vector<u8> cmdbytes(byte_count);
        if (byte_count > 0) {
            if (!memory_ ||
                !memory_->ReadBlock(obj->guest_va + cb.offset, cmdbytes.data(), byte_count)) {
                NEMU_LOG_WARN("NVDEC", "Submit: failed to read cmd buffer at +0x{:X}",
                              cb.offset);
                return kNvResultInvalidState;
            }
            size_t w = 0;
            while (w + 1 < cb.word_count) {
                u32 method = 0, arg = 0;
                std::memcpy(&method, cmdbytes.data() + w * sizeof(u32), sizeof(u32));
                std::memcpy(&arg, cmdbytes.data() + (w + 1) * sizeof(u32), sizeof(u32));
                // Host1x NVDEC method words: {method_id, argument} pairs where
                // method_id is the register slot.
                engine_.CallMethod(method, arg);
                w += 2;
            }
        }
    }

    // Syncpoint increments: bump the channel syncpoint by the requested counts
    // so waiters (the game's video thread) are released.
    offset += static_cast<size_t>(params.relocation_count) * 0x10; // Reloc
    offset += static_cast<size_t>(params.relocation_count) * 0x4;  // reloc shifts
    for (u32 i = 0; i < params.syncpoint_count; ++i) {
        if (offset + sizeof(SyncptIncr) > avail) break;
        SyncptIncr incr{};
        std::memcpy(&incr, base + offset, sizeof(incr));
        offset += sizeof(incr);
        if (syncpoints_) {
            syncpoints_->Increment(incr.id, incr.increments);
        }
    }

    // citron writes command_buffers back into the output payload (some games
    // expect it); mirror that by copying the input header+command buffers out.
    if (out.size() >= in.size()) {
        std::memcpy(out.data(), base, in.size());
    }

    return kNvResultSuccess;
}

u32 NvHostNvdecDevice::IoctlGetSyncpoint(std::span<const u8> in, std::span<u8> out) {
    // citron: params.value = channel_syncpoint (fixed base, real HW value).
    IoctlGetSyncpointOut out_val{channel_syncpoint_};
    CopyOut(out, out_val);
    return kNvResultSuccess;
}

u32 NvHostNvdecDevice::IoctlGetWaitbase(std::span<const u8> in, std::span<u8> out) {
    // Hard-coded 0 per citron (WAITBASE).
    IoctlGetSyncpointOut out_val{0};
    CopyOut(out, out_val);
    return kNvResultSuccess;
}

u32 NvHostNvdecDevice::IoctlSetSubmitTimeout(std::span<const u8> in, std::span<u8> out) {
    // STUBBED in citron too.
    (void)in;
    CopyOut(out, in.empty() ? u32{0} : u32{0});
    return kNvResultSuccess;
}

u32 NvHostNvdecDevice::IoctlMapBuffer(std::span<const u8> in, std::span<u8> out) {
    // citron MapBuffer: pin nvmap handles and fill map_address fields.
    // NEMU: nvmap objects already carry a stable guest_va; return it as the
    // 32-bit map_address (device-address-space identity map).
    struct MapBufferEntry {
        u32 map_handle;
        u32 map_address;
    };
    struct IoctlMapBufferHeader {
        u32 num_entries;
    };
    IoctlMapBufferHeader params{};
    if (!CopyIn(in, params)) return kNvResultBadParameter;
    size_t offset = sizeof(params);
    for (u32 i = 0; i < params.num_entries; ++i) {
        if (offset + sizeof(MapBufferEntry) > in.size()) break;
        MapBufferEntry entry{};
        std::memcpy(&entry, in.data() + offset, sizeof(entry));
        const auto obj = nvmap_ ? nvmap_->GetObject(entry.map_handle) : nullptr;
        if (!obj) {
            NEMU_LOG_WARN("NVDEC", "MapBuffer: unknown handle {}", entry.map_handle);
            return kNvResultInsufficientMemory;
        }
        entry.map_address = static_cast<u32>(obj->guest_va & 0xFFFFFFFFULL);
        if (out.size() >= offset + sizeof(entry)) {
            std::memcpy(out.data() + offset, &entry, sizeof(entry));
        }
        offset += sizeof(entry);
    }
    return kNvResultSuccess;
}

u32 NvHostNvdecDevice::IoctlUnmapBuffer(std::span<const u8> in, std::span<u8> out) {
    // citron UnmapBuffer: unpin + zero the entries.
    if (out.size() >= in.size() && !in.empty()) {
        std::memset(out.data(), 0, in.size());
    }
    return kNvResultSuccess;
}

} // namespace nemu::core::gpu::nvhost
