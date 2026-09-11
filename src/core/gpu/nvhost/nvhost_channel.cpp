#include "nvhost_channel.hpp"
#include "platform/logger.hpp"
#include <vector>

namespace nemu::core::gpu::nvhost {

Channel::Channel(u32 channel_id,
                 std::shared_ptr<SyncpointManager> syncpoints,
                 std::shared_ptr<AddressSpace> address_space,
                 std::shared_ptr<Maxwell3D> maxwell_3d,
                 memory::VirtualMemory* memory)
    : channel_id_(channel_id)
    , syncpoint_id_(channel_id)
    , syncpoints_(std::move(syncpoints))
    , address_space_(std::move(address_space))
    , maxwell_3d_(std::move(maxwell_3d))
    , memory_(memory) {}

u64 Channel::AllocGpfifo(u32 num_entries, u32 flags) {
    (void)flags;
    NEMU_LOG_DEBUG("NvChannel", "Channel {} AllocGpfifo: {} entries", channel_id_, num_entries);
    return 0x2000'0000ULL + channel_id_ * 0x10000ULL;
}

std::pair<u32, u32> Channel::SubmitGpfifo(std::span<const u64> gpfifo_raw, u32 flags) {
    (void)flags;
    std::vector<u32> pushbuffer;

    for (u64 entry : gpfifo_raw) {
        const u64 gpu_va = entry & 0xFF'FFFF'FFFFULL;
        const u32 num_words = static_cast<u32>((entry >> 42) & 0x1F'FFFFULL);

        if (num_words == 0 || gpu_va == 0) continue;

        if (address_space_ && memory_) {
            auto guest_va_opt = address_space_->GpuVaToGuestVa(gpu_va);
            if (guest_va_opt.has_value()) {
                const vaddr_t guest_va = *guest_va_opt;
                const size_t byte_count = static_cast<size_t>(num_words) * sizeof(u32);
                pushbuffer.resize(num_words);
                if (memory_->ReadBlock(guest_va, pushbuffer.data(), byte_count)) {
                    if (maxwell_3d_) {
                        maxwell_3d_->SubmitPushbuffer(pushbuffer);
                    }
                } else {
                    NEMU_LOG_WARN("NvChannel", "Failed to read pushbuffer from guest_va 0x{:X}", guest_va);
                }
            } else {
                NEMU_LOG_WARN("NvChannel", "Unmapped GPU VA 0x{:X} in SubmitGpfifo", gpu_va);
            }
        }
    }

    u32 fence_val = 0;
    if (syncpoints_) {
        fence_val = syncpoints_->Increment(syncpoint_id_);
    }

    NEMU_LOG_DEBUG("NvChannel", "Channel {} submitted {} GPFIFO entries -> fence (id={}, val={})",
                   channel_id_, gpfifo_raw.size(), syncpoint_id_, fence_val);

    return {syncpoint_id_, fence_val};
}

u64 Channel::AllocObjCtx(u32 class_num, u32 flags) {
    (void)flags;
    NEMU_LOG_DEBUG("NvChannel", "Channel {} AllocObjCtx: class 0x{:X}", channel_id_, class_num);
    return static_cast<u64>(class_num);
}

} // namespace nemu::core::gpu::nvhost
