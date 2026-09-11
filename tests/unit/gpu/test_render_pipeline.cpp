#include "core/gpu/null_backend.hpp"
#include "core/gpu/maxwell_3d.hpp"
#include "core/gpu/gpu_factory.hpp"
#include "core/types.hpp"
#include <iostream>
#include <cstdlib>
#include <cstdio>

using namespace nemu;
using namespace nemu::core::gpu;

#define RP_FIRST_(a, ...) a
#define RP_ASSERT(...) \
    do { \
        if (!(RP_FIRST_(__VA_ARGS__))) { \
            std::cerr << "Assertion failed: " #__VA_ARGS__ << " at " \
                      << __FILE__ << ":" << __LINE__ << std::endl; \
            std::exit(1); \
        } \
    } while (0)

// Returns float bits helper.
static u32 F(float v) noexcept {
    u32 u = 0;
    __builtin_memcpy(&u, &v, sizeof(u));
    return u;
}

int main() {
    std::cout << "[Test: Guest-Driven Render Pipeline (pushbuffer -> rasterize -> frame)]" << std::endl;

    auto backendPtr = std::make_shared<NullGpuBackend>();
    RP_ASSERT(backendPtr->Initialize(256, 256));
    Maxwell3D m3d(backendPtr);

    // Guest-style pushbuffer: clear to a dark background, set a green draw, then
    // issue DrawArrays (3 vertices, Triangles).
    const u32 pb[] = {
        (4u << 16) | MaxwellMethod::ClearColorR, F(0.05f), F(0.05f), F(0.10f), F(1.0f),
        (1u << 16) | MaxwellMethod::ClearSurface, 1u,
        (1u << 16) | MaxwellMethod::DrawArrays, (3u << 8) | 3u
    };
    m3d.SubmitPushbuffer(std::span<const u32>(pb, sizeof(pb) / sizeof(pb[0])));

    // The software rasterizer should now hold a green-filled triangle on a dark
    // background. Center pixel (128,128) is inside the triangle -> strong green.
    const u8* fb = backendPtr->Framebuffer();
    const size_t mid = ((128u * 256u) + 128u) * 4u;
    RP_ASSERT(fb[mid + 0] < 60, "center is not red");
    RP_ASSERT(fb[mid + 1] > 200, "center is strong green (rasterized triangle)");
    RP_ASSERT(fb[mid + 2] < 120, "center is not blue-dominant");

    // A corner (2,2) is outside the triangle -> dark background.
    const size_t corner = ((2u * 256u) + 2u) * 4u;
    RP_ASSERT(fb[corner + 0] < 40 && fb[corner + 1] < 40, "corner stays on background");

    // Stats reflect a real draw.
    const auto stats = backendPtr->GetStats();
    RP_ASSERT(stats.draw_calls >= 1 && stats.vertices_submitted >= 3, "draw registered");

    // Dump the guest-driven frame for visual verification.
    RP_ASSERT(backendPtr->DumpFramePPM("/tmp/nemu_guest_frame.ppm"), "frame dump");
    std::cout << "  - Guest-driven render pipeline (pushbuffer -> rasterize -> frame): PASSED" << std::endl;
    std::cout << "[Test: Guest-Driven Render Pipeline PASSED]" << std::endl;
    return 0;
}