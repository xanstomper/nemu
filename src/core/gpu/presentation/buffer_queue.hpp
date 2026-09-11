#pragma once

#include "core/types.hpp"
#include "core/gpu/gpu_interface.hpp"
#include <array>
#include <deque>
#include <mutex>
#include <vector>
#include <span>
#include <optional>

namespace nemu::core::gpu::presentation {

constexpr size_t NUM_BUFFER_SLOTS = 64;

enum class BufferState {
    Free,
    Dequeued,
    Queued,
    Acquired
};

struct BufferSlot {
    s32 slot_index{0};
    BufferState state{BufferState::Free};
    u32 width{1280};
    u32 height{720};
    u32 stride{1280};
    PixelFormat format{PixelFormat::R8G8B8A8_UNORM};
    u32 nvmap_handle{0};
    vaddr_t guest_va{0};
    u64 timestamp_ns{0};
    std::vector<u8> pixel_data;
};

class BufferQueue {
public:
    BufferQueue();
    ~BufferQueue() = default;

    BufferQueue(const BufferQueue&) = delete;
    BufferQueue& operator=(const BufferQueue&) = delete;

    /// Dequeue a buffer slot for rendering. Returns slot index (0..63) or -1 on failure.
    s32 DequeueBuffer(u32 width, u32 height, PixelFormat format, u32 usage = 0);

    /// Queue a rendered buffer slot for presentation.
    bool QueueBuffer(s32 slot, u64 timestamp_ns, vaddr_t guest_va, std::span<const u8> data = {});

    /// Cancel a dequeued buffer without rendering.
    bool CancelBuffer(s32 slot);

    /// Acquire the next queued buffer for display presentation. Returns slot index or std::nullopt.
    std::optional<s32> AcquireBuffer();

    /// Release an acquired buffer back to Free state after presentation.
    bool ReleaseBuffer(s32 slot);

    /// Query a buffer slot's metadata.
    [[nodiscard]] std::optional<BufferSlot> GetSlot(s32 slot) const;

    /// Connect / Disconnect client producer.
    void Connect() noexcept { is_connected_ = true; }
    void Disconnect() noexcept { is_connected_ = false; }
    [[nodiscard]] bool IsConnected() const noexcept { return is_connected_; }

    [[nodiscard]] size_t GetQueuedCount() const;

private:
    mutable std::mutex mutex_;
    std::array<BufferSlot, NUM_BUFFER_SLOTS> slots_{};
    std::deque<s32> queue_;
    bool is_connected_{false};
};

} // namespace nemu::core::gpu::presentation
