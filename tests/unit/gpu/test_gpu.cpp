#include "core/gpu/deswizzle.hpp"
#include "core/gpu/null_backend.hpp"
#include "core/gpu/maxwell_3d.hpp"
#include "core/gpu/gpu_factory.hpp"
#include <iostream>
#include <vector>
#include <cstring>
#include <cstdlib>

#define NEMU_TEST_ASSERT(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "Assertion failed: " #cond " (" << (msg) << ") at " \
                      << __FILE__ << ":" << __LINE__ << std::endl; \
            std::exit(1); \
        } \
    } while (0)

using namespace nemu;
using namespace nemu::core::gpu;

void TestTextureSwizzleRoundTrip(u32 width, u32 height, u32 bpp, u32 block_height) {
    const size_t linear_size = static_cast<size_t>(width) * height * bpp;
    std::vector<u8> original(linear_size);

    // Populate with deterministic pattern
    for (size_t i = 0; i < linear_size; ++i) {
        original[i] = static_cast<u8>((i * 17 + 31) & 0xFF);
    }

    // Allocate swizzled buffer (aligned to block dimensions)
    const u32 width_bytes = width * bpp;
    const u32 blocks_x = (width_bytes + TextureSwizzler::GOB_WIDTH_BYTES - 1) / TextureSwizzler::GOB_WIDTH_BYTES;
    const u32 lines_per_block = TextureSwizzler::GOB_HEIGHT_LINES * block_height;
    const u32 blocks_y = (height + lines_per_block - 1) / lines_per_block;
    const size_t swizzled_size = static_cast<size_t>(blocks_x) * blocks_y * (TextureSwizzler::GOB_SIZE_BYTES * block_height);

    std::vector<u8> swizzled(swizzled_size, 0);
    std::vector<u8> recovered(linear_size, 0);

    // 1. Swizzle
    bool swizzle_res = TextureSwizzler::SwizzleBlockLinear(original, swizzled, width, height, bpp, block_height);
    NEMU_TEST_ASSERT(swizzle_res, "SwizzleBlockLinear failed");

    // Verify swizzled buffer is not identical to linear buffer (confirming permutation occurred)
    bool is_different = false;
    for (size_t i = 0; i < linear_size; ++i) {
        if (original[i] != swizzled[i]) {
            is_different = true;
            break;
        }
    }
    NEMU_TEST_ASSERT(is_different, "Swizzled data must differ from linear layout");

    // 2. Deswizzle
    bool deswizzle_res = TextureSwizzler::DeswizzleBlockLinear(swizzled, recovered, width, height, bpp, block_height);
    NEMU_TEST_ASSERT(deswizzle_res, "DeswizzleBlockLinear failed");

    // 3. Compare recovered data with original
    NEMU_TEST_ASSERT(original == recovered, "Round-trip deswizzled data must match original exactly");
}

int main() {
    std::cout << "[Test: Tegra X1 Maxwell GPU & Texture Pipeline]" << std::endl;

    // 1. Test Block-Linear Swizzling & Deswizzling across various dimensions and block heights
    {
        TestTextureSwizzleRoundTrip(64, 64, 4, 1);
        TestTextureSwizzleRoundTrip(128, 128, 4, 2);
        TestTextureSwizzleRoundTrip(256, 128, 4, 4);
        TestTextureSwizzleRoundTrip(128, 64, 2, 1); // 16-bit textures (e.g. RGB565)
        TestTextureSwizzleRoundTrip(64, 32, 1, 1);  // 8-bit textures
        std::cout << "  - Texture swizzling round-trip tests: PASSED" << std::endl;
    }

    // 2. Test Maxwell 3D Command Processor & Backend
    {
        auto backend = std::make_shared<NullGpuBackend>();
        NEMU_TEST_ASSERT(backend->Initialize(1280, 720), "Null backend init");

        Maxwell3D maxwell(backend);

        backend->BeginFrame();

        // Pushbuffer commands:
        // Set clear color to RGBA(1.0, 0.5, 0.25, 1.0)
        // Method ClearColorR = 0x0368 (inc count=4 for R, G, B, A)
        // Packet: (0 << 29) | (4 << 16) | 0x0368
        const u32 clear_r = 0x3F800000; // 1.0f
        const u32 clear_g = 0x3F000000; // 0.5f
        const u32 clear_b = 0x3E800000; // 0.25f
        const u32 clear_a = 0x3F800000; // 1.0f

        const u32 pushbuffer[] = {
            (4 << 16) | 0x0368, // 4 inc methods: ClearColorR, G, B, A
            clear_r,
            clear_g,
            clear_b,
            clear_a,
            (1 << 16) | 0x036C, // ClearSurface
            0x00000001,
            (1 << 16) | 0x0674, // DrawArrays: 3 vertices, Triangles (3)
            (3 << 8) | 3,
            (1 << 16) | 0x0674, // DrawArrays: 6 vertices, Triangles (3)
            (6 << 8) | 3
        };

        maxwell.SubmitPushbuffer(pushbuffer);

        backend->EndFrame();
        backend->Present();

        const auto stats = backend->GetStats();
        NEMU_TEST_ASSERT(stats.frames_presented == 1, "Frames presented must be 1");
        NEMU_TEST_ASSERT(stats.draw_calls == 2, "Draw calls must be 2");
        NEMU_TEST_ASSERT(stats.vertices_submitted == 9, "Vertices submitted must be 9 (3 + 6)");

        const auto clear_col = backend->GetLastClearColor();
        NEMU_TEST_ASSERT(clear_col.r == 1.0f, "Clear color R mismatch");
        NEMU_TEST_ASSERT(clear_col.g == 0.5f, "Clear color G mismatch");
        NEMU_TEST_ASSERT(clear_col.b == 0.25f, "Clear color B mismatch");
        NEMU_TEST_ASSERT(clear_col.a == 1.0f, "Clear color A mismatch");

        backend->Shutdown();
        std::cout << "  - Maxwell 3D pushbuffer processing tests: PASSED" << std::endl;
    }

    // 3. Test GpuFactory
    {
        auto factory_backend = GpuFactory::CreateBackend(1280, 720);
        NEMU_TEST_ASSERT(factory_backend != nullptr, "Factory must create backend");
        std::cout << "  - GpuFactory instantiated backend: " << factory_backend->GetBackendName() << std::endl;
        factory_backend->Shutdown();
    }

    std::cout << "[Test: Tegra X1 Maxwell GPU & Texture Pipeline PASSED]" << std::endl;
    return 0;
}
