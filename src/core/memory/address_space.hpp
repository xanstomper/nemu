#pragma once

// Clean-room reimplementation of a guest physical-memory "address space" backed
// by one contiguous host virtual-address reservation. This is the technique used
// by the Switch/Wii emulator lineage's device-memory manager (Eden/yuzu): reserve
// a large host VA region up front, then COMMIT pages as the guest maps memory,
// so a guest address produces a direct host pointer in O(1) without a per-page
// lookup. Written fresh in Nemu's style; no source text copied from any project.

#include "core/types.hpp"
#include <cstddef>
#include <vector>

namespace nemu::core::memory {

/// Manages a large host virtual-address reservation used to directly back guest
/// physical (or virtual) memory. Operations are 4 KiB page aligned.
class AddressSpace {
public:
    AddressSpace() = default;
    ~AddressSpace();
    AddressSpace(const AddressSpace&) = delete;
    AddressSpace& operator=(const AddressSpace&) = delete;

    /// Reserve `reservation_size` bytes of contiguous host VA space.
    bool Reserve(size_t reservation_size);

    /// Commit (back) the page(s) covering [offset, offset+size). Initializes
    /// them to zero. Returns false if out of the reservation or on failure.
    bool Commit(vaddr_t offset, size_t size);

    /// Decommit (unback) the page(s) covering [offset, offset+size).
    bool Decommit(vaddr_t offset, size_t size);

    /// Returns the direct host pointer for `offset`, or nullptr if that page has
    /// not been committed (or the address space is not reserved).
    [[nodiscard]] u8* GetPointer(vaddr_t offset) const noexcept;

    /// Guarded accessors that return a pointer only when the whole [offset,
    /// offset+size) range is inside the reservation.
    [[nodiscard]] u8* GetRangePointer(vaddr_t offset, size_t size) const noexcept;

    [[nodiscard]] size_t GetReservationSize() const noexcept { return reservation_size_; }
    [[nodiscard]] u8* GetBase() const noexcept { return base_; }
    [[nodiscard]] bool IsReserved() const noexcept { return base_ != nullptr; }

    /// Whether the range is entirely within the reservation bounds.
    [[nodiscard]] bool IsInRange(vaddr_t offset, size_t size = 1) const noexcept {
        return base_ != nullptr && offset + size <= reservation_size_ && offset + size >= offset;
    }

private:
    struct Range {
        vaddr_t offset;
        size_t size;
        vaddr_t begin_end() const noexcept { return offset + size; }
    };

    u8* base_{nullptr};
    size_t reservation_size_{0};
    std::vector<Range> committed_ranges_;
    static constexpr size_t kPageSize = 0x1000;
    static constexpr size_t kPageMask = kPageSize - 1;
};

} // namespace nemu::core::memory