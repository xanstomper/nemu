// SPDX-FileCopyrightText: Copyright 2020 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later
//
// Ported from citron-neo/emulator
// src/core/hle/service/nvdrv/devices/nvhost_nvdec.cpp/.h and
// nvhost_nvdec_common.cpp/.h, adapted to NEMU's NvDeviceFile Ioctl model
// (single cmd/word dispatch instead of citron's Ioctl1/2/3 group/cmd split).

#pragma once

#include "nvdec_common.hpp"
#include "nvdec.hpp"
#include "core/types.hpp"
#include "nvdevice.hpp"
#include <memory>
#include <span>
#include <unordered_map>

namespace nemu::core::memory {
class VirtualMemory;
}
namespace nemu::core::gpu::nvhost {

class NvMap;
class SyncpointManager;

/// /dev/nvhost-nvdec device file: video decode engine channel.
class NvHostNvdecDevice final : public NvDeviceFile {
public:
    NvHostNvdecDevice(std::shared_ptr<NvMap> nvmap,
                      std::shared_ptr<SyncpointManager> syncpoints,
                      memory::VirtualMemory* memory);
    ~NvHostNvdecDevice() override;

    u32 Ioctl(u32 cmd, std::span<const u8> in_buf, std::span<u8> out_buf) override;
    void Close() override;

    /// Engine access for tests / channel routing.
    [[nodiscard]] Nvdec& GetEngine() noexcept { return engine_; }

private:
    // --- ioctl handlers (citron nvhost_nvdec_common contract) ---
    u32 IoctlSetNvmapFd(std::span<const u8> in, std::span<u8> out);
    u32 IoctlSubmit(std::span<const u8> in, std::span<u8> out);
    u32 IoctlGetSyncpoint(std::span<const u8> in, std::span<u8> out);
    u32 IoctlGetWaitbase(std::span<const u8> in, std::span<u8> out);
    u32 IoctlSetSubmitTimeout(std::span<const u8> in, std::span<u8> out);
    u32 IoctlMapBuffer(std::span<const u8> in, std::span<u8> out);
    u32 IoctlUnmapBuffer(std::span<const u8> in, std::span<u8> out);

    std::shared_ptr<NvMap> nvmap_;
    std::shared_ptr<SyncpointManager> syncpoints_;
    memory::VirtualMemory* memory_;
    Nvdec engine_;

    u32 nvmap_fd_{0};
    u32 channel_syncpoint_{0x41}; // NVDEC channel syncpoint base (real HW: 0x41)
    std::unordered_map<u32, u32> fd_to_session_; // fd -> decode session id
};

} // namespace nemu::core::gpu::nvhost
