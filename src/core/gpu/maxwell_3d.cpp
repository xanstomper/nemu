#include "maxwell_3d.hpp"
#include "platform/logger.hpp"
#include <cstring>

namespace nemu::core::gpu {

Maxwell3D::Maxwell3D(std::shared_ptr<IGpuBackend> backend)
    : backend_(std::move(backend)) {
}

void Maxwell3D::ProcessMethod(u32 method, u32 argument) {
    const u32 reg_idx = method & 0xFFF;
    regs_.regs[reg_idx] = argument;

    switch (method) {
        case MaxwellMethod::Nop:
        case MaxwellMethod::WaitForIdle:
            break;

        case MaxwellMethod::ClearSurface:
            ExecuteClearSurface(argument);
            break;

        case MaxwellMethod::DrawArrays:
            ExecuteDrawArrays(argument);
            break;

        case MaxwellMethod::ViewportScaleX:
        case MaxwellMethod::ViewportScaleY:
        case MaxwellMethod::ViewportOffsetX:
        case MaxwellMethod::ViewportOffsetY: {
            if (backend_) {
                Viewport vp;
                vp.width = regs_.GetFloat(MaxwellMethod::ViewportScaleX) * 2.0f;
                vp.height = regs_.GetFloat(MaxwellMethod::ViewportScaleY) * 2.0f;
                vp.x = regs_.GetFloat(MaxwellMethod::ViewportOffsetX) - (vp.width * 0.5f);
                vp.y = regs_.GetFloat(MaxwellMethod::ViewportOffsetY) - (vp.height * 0.5f);
                backend_->SetViewport(vp);
            }
            break;
        }

        default:
            NEMU_LOG_DEBUG("GPU", "Maxwell3D method 0x{:04X} = 0x{:08X}", method, argument);
            break;
    }
}

void Maxwell3D::ExecuteClearSurface([[maybe_unused]] u32 argument) {
    if (!backend_) return;

    ClearColor color{
        .r = regs_.GetFloat(MaxwellMethod::ClearColorR),
        .g = regs_.GetFloat(MaxwellMethod::ClearColorG),
        .b = regs_.GetFloat(MaxwellMethod::ClearColorB),
        .a = regs_.GetFloat(MaxwellMethod::ClearColorA)
    };
    backend_->ClearRenderTarget(color);
}

void Maxwell3D::ExecuteDrawArrays(u32 argument) {
    if (!backend_) return;

    const u32 topology_raw = argument & 0x0F;
    const u32 vertex_count = (argument >> 8) & 0xFFFFFF;

    PrimitiveTopology topology = PrimitiveTopology::Triangles;
    switch (topology_raw) {
        case 0: topology = PrimitiveTopology::Points; break;
        case 1: topology = PrimitiveTopology::Lines; break;
        case 2: topology = PrimitiveTopology::LineStrip; break;
        case 3: topology = PrimitiveTopology::Triangles; break;
        case 4: topology = PrimitiveTopology::TriangleStrip; break;
        case 5: topology = PrimitiveTopology::TriangleFan; break;
        default: break;
    }

    backend_->DrawArrays(topology, 0, vertex_count);
}

void Maxwell3D::SubmitPushbuffer(std::span<const u32> pushbuffer) {
    size_t index = 0;
    while (index < pushbuffer.size()) {
        const u32 header = pushbuffer[index++];
        const u32 method = header & 0x1FFF;
        const u32 count = (header >> 16) & 0x1FFF;
        const bool is_inc = ((header >> 29) & 1) == 0;

        for (u32 i = 0; i < count && index < pushbuffer.size(); ++i) {
            const u32 arg = pushbuffer[index++];
            ProcessMethod(is_inc ? (method + i) : method, arg);
        }
    }
}

} // namespace nemu::core::gpu
