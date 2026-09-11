#pragma once

// Clean-room reimplementation of a reusable "scratch buffer" (the staging-buffer
// technique used across the Switch/Wii emulator lineage's `common/scratch_buffer`)
// to avoid repeated heap allocations for per-frame staging of vertex, texture,
// and sound data. Written fresh in Nemu's style; no source text copied.

#include <cstddef>
#include <cstring>
#include <vector>

namespace nemu::core::common {

/// A grow-only, reusable contiguous buffer of `T`. Prefer this over allocating a
/// fresh std::vector every frame: storage is retained and only enlarged when a
/// larger request arrives, so steady-state frames never hit the allocator.
template <typename T>
class ScratchBuffer {
public:
    using value_type = T;

    /// Ensure capacity for at least `count` elements; returns the element
    /// pointer. Does not shrink and does not reallocate when already large
    /// enough.
    T* Resize(std::size_t count) {
        if (count > capacity_) {
            storage_.resize(count);
            capacity_ = storage_.size();
        }
        return storage_.data();
    }

    /// Alias of Resize for callers that just want a buffer of `count`.
    T* Get(std::size_t count) { return Resize(count); }

    /// Return a pointer to the storage at `index`, growing so that
    /// [index, index+size) is addressable.
    T* MakeRoom(std::size_t index, std::size_t size) {
        const std::size_t end = index + size;
        if (end > capacity_) {
            Resize(end);
        }
        return storage_.data() + index;
    }

    /// Copy `n` bytes from `src` into the buffer at byte offset `dest_bytes`.
    /// Convenience for staging raw scoped data (e.g. texture bytes).
    void CopyBytes(const void* src, std::size_t dest_bytes, std::size_t n) {
        std::size_t start_elem = dest_bytes / sizeof(T);
        std::size_t byte_in_elem = dest_bytes % sizeof(T);
        std::size_t end_bytes = dest_bytes + n;
        std::size_t end_elem = (end_bytes + sizeof(T) - 1) / sizeof(T);
        T* dst = MakeRoom(0, end_elem);
        std::memcpy(reinterpret_cast<std::byte*>(dst) + dest_bytes, src, n);
        (void)start_elem;
        (void)byte_in_elem;
    }

    [[nodiscard]] std::size_t Size() const noexcept { return capacity_; }
    [[nodiscard]] bool Empty() const noexcept { return capacity_ == 0; }
    [[nodiscard]] T* Data() noexcept { return storage_.data(); }
    [[nodiscard]] const T* Data() const noexcept { return storage_.data(); }

    /// Release all backing storage (call only when no longer hot).
    void Release() {
        storage_.clear();
        storage_.shrink_to_fit();
        capacity_ = 0;
    }

private:
    std::vector<T> storage_;
    std::size_t capacity_{0};
};

} // namespace nemu::core::common