#pragma once

#include "core/types.hpp"
#include "nvhost_as_gpu.hpp"
#include "nvhost_ctrl.hpp"
#include "core/gpu/maxwell_3d.hpp"
#include "core/memory/virtual_memory.hpp"
#include <memory>
#include <vector>
#include <span>

namespace nemu::core::gpu::nvhost {

struct GpfifoEntry {
    u64 gpu_va{0};
    u32 num_words{0};
    bool is_non_main{false};
    bool no_fence{false};
};

class Channel {
public:
    Channel(u32 channel_id,
            std::shared_ptr<SyncpointManager> syncpoints,
            std::shared_ptr<AddressSpace> address_space,
            std::shared_ptr<Maxwell3D> maxwell_3d,
            memory::VirtualMemory* memory);
    ~Channel() = default;

    Channel(const Channel&) = delete;
    Channel& operator=(const Channel&) = delete;

    /// Set client host PID.
    void SetNvhostPid(u32 pid) noexcept { host_pid_ = pid; }
    [[nodiscard]] u32 GetNvhostPid() const noexcept { return host_pid_; }

    /// Allocate GPFIFO entries. Returns assigned fence address or 0.
    u64 AllocGpfifo(u32 num_entries, u32 flags);

    /// Submit a list of GPFIFO entries.
    /// Returns pair of (fence_id, fence_value).
    std::pair<u32, u32> SubmitGpfifo(std::span<const u64> gpfifo_raw, u32 flags);

    /// Allocate engine object context.
    u64 AllocObjCtx(u32 class_num, u32 flags);

    /// Set execution priority.
    void SetPriority(u32 priority) noexcept { priority_ = priority; }
    [[nodiscard]] u32 GetPriority() const noexcept { return priority_; }

    [[nodiscard]] u32 GetChannelId() const noexcept { return channel_id_; }
    [[nodiscard]] u32 GetSyncpointId() const noexcept { return syncpoint_id_; }

private:
    u32 channel_id_{0};
    u32 syncpoint_id_{0};
    u32 host_pid_{0};
    u32 priority_{0};
    std::shared_ptr<SyncpointManager> syncpoints_;
    std::shared_ptr<AddressSpace> address_space_;
    std::shared_ptr<Maxwell3D> maxwell_3d_;
    memory::VirtualMemory* memory_{nullptr};
};

} // namespace nemu::core::gpu::nvhost
