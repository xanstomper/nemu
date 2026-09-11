#pragma once

#include "gpu_interface.hpp"
#include <memory>

namespace nemu::core::gpu {

class GpuFactory {
public:
    static std::shared_ptr<IGpuBackend> CreateBackend(u32 render_width = 1280, u32 render_height = 720);
};

} // namespace nemu::core::gpu
