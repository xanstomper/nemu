#include "core/common/aligned_allocator.hpp"
#include "core/types.hpp"
#include <vector>
#include <cstdint>
#include <iostream>
#include <cstdlib>

using namespace nemu;
using namespace nemu::core::common;

#define AA_FIRST_(a, ...) a
#define AA_ASSERT(...) \
    do { \
        if (!(AA_FIRST_(__VA_ARGS__))) { \
            std::cerr << "Assertion failed: " #__VA_ARGS__ << " at " \
                      << __FILE__ << ":" << __LINE__ << std::endl; \
            std::exit(1); \
        } \
    } while (0)

int main() {
    std::cout << "[Test: Aligned Allocator]" << std::endl;

    // 1. Allocation is aligned.
    {
        void* p = AllocateAligned(256, 64);
        AA_ASSERT(p != nullptr);
        AA_ASSERT((reinterpret_cast<std::uintptr_t>(p) & 63) == 0, "64-byte aligned");
        FreeAligned(p);

        void* q = AllocateAligned(1, 16);
        AA_ASSERT(q != nullptr);
        AA_ASSERT((reinterpret_cast<std::uintptr_t>(q) & 15) == 0, "16-byte aligned");
        FreeAligned(q);
    }

    // 2. Reject non-power-of-two alignment.
    {
        AA_ASSERT(AllocateAligned(16, 3) == nullptr, "invalid alignment rejected");
    }

    // 3. Usable as a std::vector allocator; data is aligned and round-trips.
    {
        std::vector<float, AlignedAllocator<float, 64>> vec;
        vec.reserve(8);
        for (int i = 0; i < 8; ++i) vec.push_back(static_cast<float>(i));
        AA_ASSERT(!vec.empty() && vec.size() == 8);
        const auto* data = vec.data();
        AA_ASSERT((reinterpret_cast<std::uintptr_t>(data) & 63) == 0, "vector data 64-byte aligned");
        for (int i = 0; i < 8; ++i) AA_ASSERT(vec[static_cast<size_t>(i)] == static_cast<float>(i));
    }

    // 4. Default alignment path (64).
    {
        void* p = AllocateAligned(8);
        AA_ASSERT(p != nullptr);
        AA_ASSERT((reinterpret_cast<std::uintptr_t>(p) & (kDefaultAlignment - 1)) == 0);
        FreeAligned(p);
    }

    std::cout << "  - Aligned allocator tests: PASSED" << std::endl;
    std::cout << "[Test: Aligned Allocator PASSED]" << std::endl;
    return 0;
}