#include "buffer_queue.hpp"
#include "platform/logger.hpp"

namespace nemu::core::gpu::presentation {

BufferQueue::BufferQueue() {
    for (size_t i = 0; i < NUM_BUFFER_SLOTS; ++i) {
        slots_[i].slot_index = static_cast<s32>(i);
        slots_[i].state = BufferState::Free;
    }
}

s32 BufferQueue::DequeueBuffer(u32 width, u32 height, PixelFormat format, u32 usage) {
    (void)usage;
    std::unique_lock lock(mutex_);
    for (size_t i = 0; i < NUM_BUFFER_SLOTS; ++i) {
        if (slots_[i].state == BufferState::Free) {
            slots_[i].state = BufferState::Dequeued;
            slots_[i].width = width;
            slots_[i].height = height;
            slots_[i].stride = width;
            slots_[i].format = format;
            NEMU_LOG_DEBUG("BufferQueue", "Dequeued slot {} ({}x{})", i, width, height);
            return static_cast<s32>(i);
        }
    }
    NEMU_LOG_WARN("BufferQueue", "DequeueBuffer: no free slots available");
    return -1;
}

bool BufferQueue::QueueBuffer(s32 slot, u64 timestamp_ns, vaddr_t guest_va, std::span<const u8> data) {
    if (slot < 0 || static_cast<size_t>(slot) >= NUM_BUFFER_SLOTS) return false;
    std::unique_lock lock(mutex_);
    auto& s = slots_[static_cast<size_t>(slot)];
    if (s.state != BufferState::Dequeued) {
        NEMU_LOG_WARN("BufferQueue", "QueueBuffer: slot {} not in Dequeued state", slot);
        return false;
    }
    s.state = BufferState::Queued;
    s.timestamp_ns = timestamp_ns;
    s.guest_va = guest_va;
    if (!data.empty()) {
        s.pixel_data.assign(data.begin(), data.end());
    }
    queue_.push_back(slot);
    NEMU_LOG_DEBUG("BufferQueue", "Queued slot {} (timestamp={}ns, queued_depth={})",
                   slot, timestamp_ns, queue_.size());
    return true;
}

bool BufferQueue::CancelBuffer(s32 slot) {
    if (slot < 0 || static_cast<size_t>(slot) >= NUM_BUFFER_SLOTS) return false;
    std::unique_lock lock(mutex_);
    auto& s = slots_[static_cast<size_t>(slot)];
    if (s.state == BufferState::Dequeued) {
        s.state = BufferState::Free;
        return true;
    }
    return false;
}

std::optional<s32> BufferQueue::AcquireBuffer() {
    std::unique_lock lock(mutex_);
    if (queue_.empty()) return std::nullopt;
    const s32 slot = queue_.front();
    queue_.pop_front();
    slots_[static_cast<size_t>(slot)].state = BufferState::Acquired;
    NEMU_LOG_DEBUG("BufferQueue", "Acquired slot {}", slot);
    return slot;
}

bool BufferQueue::ReleaseBuffer(s32 slot) {
    if (slot < 0 || static_cast<size_t>(slot) >= NUM_BUFFER_SLOTS) return false;
    std::unique_lock lock(mutex_);
    auto& s = slots_[static_cast<size_t>(slot)];
    if (s.state == BufferState::Acquired) {
        s.state = BufferState::Free;
        NEMU_LOG_DEBUG("BufferQueue", "Released slot {}", slot);
        return true;
    }
    return false;
}

std::optional<BufferSlot> BufferQueue::GetSlot(s32 slot) const {
    if (slot < 0 || static_cast<size_t>(slot) >= NUM_BUFFER_SLOTS) return std::nullopt;
    std::unique_lock lock(mutex_);
    return slots_[static_cast<size_t>(slot)];
}

size_t BufferQueue::GetQueuedCount() const {
    std::unique_lock lock(mutex_);
    return queue_.size();
}

} // namespace nemu::core::gpu::presentation
