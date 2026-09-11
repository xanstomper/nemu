#pragma once

// Clean-room reimplementation of an alignment-aware allocator (the general
// technique used across the Switch/Wii lineage for SIMD-safe buffers in the
// render/audio paths). Written fresh in Nemu's style; no source text copied.
// Provides a C++17 std::allocator-compatible aligned allocator.

#include <cstddef>
#include <cstdlib>
#include <new>
#include <type_traits>
#include <memory>

namespace nemu::core::common {

/// Default alignment used when none is specified by the caller.
constexpr std::size_t kDefaultAlignment = 64;

/// Allocate `size` bytes aligned to `alignment` (must be a power of two).
inline void* AllocateAligned(std::size_t size, std::size_t alignment = kDefaultAlignment) noexcept {
    if (alignment == 0 || (alignment & (alignment - 1)) != 0) {
        return nullptr;
    }
#ifdef _WIN32
    return _aligned_malloc(size ? size : 1, alignment);
#else
    void* p = nullptr;
    if (posix_memalign(&p, alignment, size ? size : 1) != 0) {
        return nullptr;
    }
    return p;
#endif
}

/// Free memory returned by AllocateAligned.
inline void FreeAligned(void* ptr) noexcept {
#ifdef _WIN32
    _aligned_free(ptr);
#else
    std::free(ptr);
#endif
}

/// std::allocator-compatible aligned allocator for T.
template <typename T, std::size_t Alignment = kDefaultAlignment>
class AlignedAllocator {
public:
    using value_type = T;

    AlignedAllocator() noexcept = default;
    template <typename U, std::size_t A>
    constexpr explicit AlignedAllocator(const AlignedAllocator<U, A>&) noexcept {}

    [[nodiscard]] T* allocate(std::size_t n) {
        if (n == 0) {
            return nullptr;
        }
        if (n > max_size()) {
            throw std::bad_alloc();
        }
        void* p = AllocateAligned(n * sizeof(T), Alignment);
        if (!p) {
            throw std::bad_alloc();
        }
        return static_cast<T*>(p);
    }

    void deallocate(T* p, std::size_t /*n*/) noexcept {
        FreeAligned(p);
    }

    [[nodiscard]] static constexpr std::size_t max_size() noexcept {
        return static_cast<std::size_t>(-1) / sizeof(T);
    }

    template <typename U>
    struct rebind {
        using other = AlignedAllocator<U, Alignment>;
    };
};

template <typename T, std::size_t A, typename U, std::size_t B>
constexpr bool operator==(const AlignedAllocator<T, A>&, const AlignedAllocator<U, B>&) noexcept {
    return A == B;
}
template <typename T, std::size_t A, typename U, std::size_t B>
constexpr bool operator!=(const AlignedAllocator<T, A>&, const AlignedAllocator<U, B>&) noexcept {
    return A != B;
}

} // namespace nemu::core::common