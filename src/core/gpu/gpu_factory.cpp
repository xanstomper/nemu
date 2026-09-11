#include "gpu_factory.hpp"
#include "null_backend.hpp"
#include "platform/logger.hpp"

#ifdef _WIN32
#include "d3d12/d3d12_backend.hpp"
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

    auto null_backend = std::make_shared<NullGpuBackend>();
    null_backend->Initialize(render_width, render_height);
    return null_backend;
}

} // namespace nemu::core::gpu
