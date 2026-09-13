#include "gpu_factory.hpp"
#include "null_backend.hpp"
#include "platform/logger.hpp"
#include <cstdlib>

#ifdef _WIN32
#include "d3d12/d3d12_backend.hpp"
#endif

#ifdef NEMU_SDL2
#include "sdl2/sdl2_backend.hpp"
#endif

namespace nemu::core::gpu {

std::shared_ptr<IGpuBackend> GpuFactory::CreateBackend(u32 render_width, u32 render_height) {
#ifdef _WIN32
    auto d3d12 = std::make_shared<D3D12GpuBackend>();
    if (d3d12->Initialize(render_width, render_height)) {
        NEMU_LOG_INFO("GPU", "Using Direct3D 12 Hardware Backend");
        return d3d12;
    }
    NEMU_LOG_WARN("GPU", "Direct3D 12 initialization failed, falling back to Null GPU backend");
#endif

#ifdef NEMU_SDL2
    // On desktop Linux, prefer the SDL2 windowed backend so the real UI is
    // visible. Override with NEMU_HEADLESS=1 to force the headless Null path.
    if (std::getenv("NEMU_HEADLESS") == nullptr) {
        auto sdl2 = std::make_shared<sdl2::Sdl2GpuBackend>();
        if (sdl2->Initialize(render_width, render_height)) {
            NEMU_LOG_INFO("GPU", "Using SDL2 Windowed Backend (desktop UI)");
            return sdl2;
        }
        NEMU_LOG_WARN("GPU", "SDL2 window unavailable, falling back to Null GPU backend");
    }
#endif

    auto null_backend = std::make_shared<NullGpuBackend>();
    null_backend->Initialize(render_width, render_height);
    return null_backend;
}

} // namespace nemu::core::gpu
