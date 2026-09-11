#pragma once

#include "core/types.hpp"
#include "nvmap.hpp"
#include "nvhost_ctrl.hpp"
#include "nvhost_as_gpu.hpp"
#include "nvhost_channel.hpp"
#include "core/gpu/maxwell_3d.hpp"
#include "core/memory/virtual_memory.hpp"
#include <span>
#include <string_view>
#include <memory>
#include <mutex>
#include <unordered_map>

namespace nemu::core::gpu::nvhost {

class NvDeviceFile {
public:
    virtual ~NvDeviceFile() = default;
    virtual u32 Ioctl(u32 cmd, std::span<const u8> in_buf, std::span<u8> out_buf) = 0;
    virtual void Close() {}
};

class NvDeviceManager {
public:
    NvDeviceManager(std::shared_ptr<Maxwell3D> maxwell_3d, memory::VirtualMemory* memory);
    ~NvDeviceManager() = default;

    NvDeviceManager(const NvDeviceManager&) = delete;
    NvDeviceManager& operator=(const NvDeviceManager&) = delete;

    /// Open a device path (/dev/nvhost-ctrl, /dev/nvhost-as-gpu, /dev/nvmap, /dev/nvhost-gpu).
    /// Returns fd >= 0 on success, or -1 on error.
    s32 Open(std::string_view path);

    /// Dispatch ioctl to an open fd.
    u32 Ioctl(s32 fd, u32 cmd, std::span<const u8> in_buf, std::span<u8> out_buf);

    /// Close an open fd.
    bool Close(s32 fd);

    [[nodiscard]] std::shared_ptr<NvMap> GetNvMap() const noexcept { return nvmap_; }
    [[nodiscard]] std::shared_ptr<SyncpointManager> GetSyncpoints() const noexcept { return syncpoints_; }
    [[nodiscard]] std::shared_ptr<AddressSpace> GetAddressSpace() const noexcept { return address_space_; }
    [[nodiscard]] std::shared_ptr<Channel> GetChannel() const noexcept { return channel_; }

private:
    std::shared_ptr<Maxwell3D> maxwell_3d_;
    memory::VirtualMemory* memory_{nullptr};

    std::shared_ptr<NvMap> nvmap_;
    std::shared_ptr<SyncpointManager> syncpoints_;
    std::shared_ptr<AddressSpace> address_space_;
    std::shared_ptr<Channel> channel_;

    mutable std::mutex mutex_;
    s32 next_fd_{1};
    std::unordered_map<s32, std::shared_ptr<NvDeviceFile>> files_;
};

} // namespace nemu::core::gpu::nvhost
