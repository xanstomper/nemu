#include "breadcrumbs.hpp"

namespace nemu::core::debug {

size_t BreadcrumbTrail::Snapshot(Breadcrumb* out, size_t max) noexcept {
    const u32 total = next_.load(std::memory_order_relaxed);
    const u32 count = total < kSlots ? total : kSlots;
    size_t n = 0;
    for (size_t i = 0; i < count && n < max; ++i) {
        const u32 idx = (total - count + i) % kSlots; // oldest first
        if (slots_[idx].kind != Breadcrumb::Kind::None) {
            out[n++] = slots_[idx];
        }
    }
    return n;
}

} // namespace nemu::core::debug
