#include "core/common/scratch_buffer.hpp"
#include "core/types.hpp"
#include <iostream>
#include <cstdlib>
#include <cstring>

using namespace nemu;
using namespace nemu::core::common;

#define SB_ASSERT_FIRST_(a, ...) a
#define SB_ASSERT(...) \
    do { \
        if (!(SB_ASSERT_FIRST_(__VA_ARGS__))) { \
            std::cerr << "Assertion failed: " #__VA_ARGS__ << " at " \
                      << __FILE__ << ":" << __LINE__ << std::endl; \
            std::exit(1); \
        } \
    } while (0)

int main() {
    std::cout << "[Test: Reusable Scratch Buffer]" << std::endl;

    // 1. Grow-only and reusable.
    {
        ScratchBuffer<float> buf;
        SB_ASSERT(buf.Empty() && buf.Size() == 0);

        float* p = buf.Resize(16);
        SB_ASSERT(p != nullptr && buf.Size() == 16 && !buf.Empty());
        for (int i = 0; i < 16; ++i) p[i] = static_cast<float>(i);

        // Requesting the same or smaller size reuses storage (no realloc: same Data()).
        buf.Resize(8);
        SB_ASSERT(buf.Data() == p, "no reallocation on shrink");

        // Larger request grows and preserves contents.
        float* r = buf.Resize(64);
        SB_ASSERT(r != nullptr && buf.Size() == 64);
        SB_ASSERT(r[3] == 3.0f && r[0] == 0.0f, "contents preserved after grow");
    }

    // 2. MakeRoom addresses [index, index+size).
    {
        ScratchBuffer<u32> buf;
        u32* base = buf.MakeRoom(2, 4); // ensure [2, 6) addressable; base == &data[2]
        SB_ASSERT(buf.Size() >= 6);
        base[0] = 77; // == data[2]
        base[3] = 88; // == data[5]
        SB_ASSERT(buf.Data()[2] == 77);
        SB_ASSERT(buf.Data()[5] == 88);
    }

    // 3. CopyBytes stages raw bytes into the buffer.
    {
        ScratchBuffer<std::byte> buf;
        const std::byte src[] = { std::byte(0xDE), std::byte(0xAD), std::byte(0xBE), std::byte(0xEF) };
        buf.CopyBytes(src, 0, 4);
        SB_ASSERT(buf.Size() >= 4);
        const auto* d = reinterpret_cast<const std::byte*>(buf.Data());
        SB_ASSERT(d[0] == std::byte(0xDE) && d[1] == std::byte(0xAD) && d[3] == std::byte(0xEF));
    }

    // 4. Release frees storage.
    {
        ScratchBuffer<char> buf;
        buf.Resize(1024);
        SB_ASSERT(buf.Size() == 1024);
        buf.Release();
        SB_ASSERT(buf.Empty() && buf.Size() == 0);
    }

    std::cout << "  - Reusable scratch buffer tests: PASSED" << std::endl;
    std::cout << "[Test: Reusable Scratch Buffer PASSED]" << std::endl;
    return 0;
}