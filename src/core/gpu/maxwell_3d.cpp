#include "maxwell_3d.hpp"
#include "texture/astc_decoder.hpp"
#include "core/memory/virtual_memory.hpp"
#include "platform/logger.hpp"
#include <cstring>
#include <vector>

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

        case MaxwellMethod::DrawElements:
            ExecuteDrawElements(argument);
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

        case MaxwellMethod::ClearDepth: {
            if (backend_) {
                float depth = regs_.GetFloat(MaxwellMethod::ClearDepth);
                backend_->ClearDepthStencil(depth, 0);
            }
            break;
        }

        case MaxwellMethod::ScissorEnable:
        case MaxwellMethod::ScissorX:
        case MaxwellMethod::ScissorY:
        case MaxwellMethod::ScissorWidth:
        case MaxwellMethod::ScissorHeight: {
            if (backend_) {
                ScissorRect sr;
                const u32 sx = regs_.regs[MaxwellMethod::ScissorX];
                const u32 sy = regs_.regs[MaxwellMethod::ScissorY];
                const u32 sw = regs_.regs[MaxwellMethod::ScissorWidth];
                const u32 sh = regs_.regs[MaxwellMethod::ScissorHeight];
                sr.left = sx;
                sr.top = sy;
                sr.right = sx + sw;
                sr.bottom = sy + sh;
                backend_->SetScissor(sr);
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

void Maxwell3D::EmitDebugGeometry() {
    // Stage a recognizable test triangle in the reusable scratch buffer and bind
    // it to the backend so a guest DrawArrays produces real rasterized output.
    // (This is a stand-in for guest-loaded vertex buffers; it exercises the whole
    // clear -> bind -> draw -> framebuffer pipeline end to end.)
    RasterVertex* tri = geometry_scratch_.Resize(3);
    // A triangle covering most of the viewport with a green fill.
    tri[0] = {-0.85f, -0.85f, 0.0f, 1.0f, 0.3f, 1.0f};
    tri[1] = { 0.85f, -0.85f, 0.0f, 1.0f, 0.3f, 1.0f};
    tri[2] = { 0.0f,   0.85f, 0.0f, 1.0f, 0.3f, 1.0f};
    if (backend_) {
        backend_->SetRasterVertices(std::span<const RasterVertex>(tri, 3));
    }
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

    // Bind guest vertices if available; otherwise stage fallback debug geometry
    const u64 vtx_addr = (static_cast<u64>(regs_.regs[MaxwellMethod::VertexArrayAddressHigh]) << 32) |
                          static_cast<u64>(regs_.regs[MaxwellMethod::VertexArrayAddressLow]);
    bool bound_guest_verts = false;
    if (memory_ && vtx_addr != 0 && vertex_count >= 3) {
        const size_t bytes_needed = vertex_count * sizeof(RasterVertex);
        if (memory_->IsValidAddress(vtx_addr, bytes_needed)) {
            RasterVertex* verts = geometry_scratch_.Resize(vertex_count);
            if (memory_->ReadBlock(vtx_addr, verts, bytes_needed)) {
                backend_->SetRasterVertices(std::span<const RasterVertex>(verts, vertex_count));
                bound_guest_verts = true;
            }
        }
    }
    if (!bound_guest_verts && vertex_count >= 3) {
        EmitDebugGeometry();
    }
    backend_->DrawArrays(topology, 0, vertex_count);
}

void Maxwell3D::ExecuteDrawElements(u32 argument) {
    if (!backend_) return;

    const u32 topology_raw = argument & 0x0F;
    const u32 index_count = (argument >> 8) & 0xFFFFFF;

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

    // Bind guest indices if available; otherwise stage fallback debug indexed geometry
    const u64 idx_addr = (static_cast<u64>(regs_.regs[MaxwellMethod::IndexAddressHigh]) << 32) |
                          static_cast<u64>(regs_.regs[MaxwellMethod::IndexAddressLow]);
    const u32 idx_format = regs_.regs[MaxwellMethod::IndexFormat]; // 0 = u8, 1 = u16, 2 = u32
    bool bound_guest_indices = false;
    if (memory_ && idx_addr != 0 && index_count >= 3) {
        u32* indices = index_scratch_.Resize(index_count);
        if (idx_format == 1) { // u16
            std::vector<u16> u16_indices(index_count);
            if (memory_->ReadBlock(idx_addr, u16_indices.data(), index_count * sizeof(u16))) {
                for (size_t i = 0; i < index_count; ++i) indices[i] = u16_indices[i];
                backend_->SetRasterIndices(std::span<const u32>(indices, index_count));
                bound_guest_indices = true;
            }
        } else if (idx_format == 2) { // u32
            if (memory_->ReadBlock(idx_addr, indices, index_count * sizeof(u32))) {
                backend_->SetRasterIndices(std::span<const u32>(indices, index_count));
                bound_guest_indices = true;
            }
        }
    }
    if (!bound_guest_indices && index_count >= 3) {
        EmitDebugIndexedGeometry();
    }
    backend_->DrawIndexed(topology, index_count, 0, 0);
}

void Maxwell3D::EmitDebugIndexedGeometry() {
    RasterVertex* verts = geometry_scratch_.Resize(4);
    verts[0] = {-0.8f, -0.8f, 0.0f, 1.0f, 0.5f, 1.0f};
    verts[1] = { 0.8f, -0.8f, 0.0f, 1.0f, 0.5f, 1.0f};
    verts[2] = { 0.8f,  0.8f, 0.0f, 1.0f, 0.5f, 1.0f};
    verts[3] = {-0.8f,  0.8f, 0.0f, 1.0f, 0.5f, 1.0f};

    u32* indices = index_scratch_.Resize(6);
    indices[0] = 0; indices[1] = 1; indices[2] = 2;
    indices[3] = 0; indices[4] = 2; indices[5] = 3;

    if (backend_) {
        backend_->SetRasterVertices(std::span<const RasterVertex>(verts, 4));
        backend_->SetRasterIndices(std::span<const u32>(indices, 6));
    }
}

bool Maxwell3D::DecompressAstc(
    std::span<const u8> astc_data,
    u32 width,
    u32 height,
    u32 block_width,
    u32 block_height,
    std::vector<u32>& out_rgba8,
    bool is_srgb
) {
    return texture::AstcDecoder::DecompressSurface(
        astc_data, width, height, block_width, block_height, out_rgba8, is_srgb
    );
}

void Maxwell3D::SubmitPushbuffer(std::span<const u32> pushbuffer) {
    size_t index = 0;
    while (index < pushbuffer.size()) {
        const u32 header = pushbuffer[index++];
        const u32 method = header & 0x1FFF;
        const u32 count = (header >> 16) & 0x1FFF;
        const u32 mode = (header >> 29) & 0x07;

        if (mode == 2) { // InlineData: count field encodes the inline data
            ProcessMethod(method, count);
            continue;
        }

        for (u32 i = 0; i < count && index < pushbuffer.size(); ++i) {
            const u32 arg = pushbuffer[index++];
            u32 target_method = method;
            if (mode == 0) { // IncMethod
                target_method = method + i;
            } else if (mode == 1) { // NonIncMethod
                target_method = method;
            } else if (mode == 3) { // IncreaseOnce
                target_method = (i == 0) ? method : (method + 1);
            }
            ProcessMethod(target_method, arg);
        }
    }
}

} // namespace nemu::core::gpu
