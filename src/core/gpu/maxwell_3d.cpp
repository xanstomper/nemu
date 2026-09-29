#include "maxwell_3d.hpp"
#include "core/debug/breadcrumbs.hpp"
#include "compute_qmd.hpp"
#include "engine_upload.hpp"
#include "texture/astc_decoder.hpp"
#include "texture/texture_types.hpp"
#include "core/memory/virtual_memory.hpp"
#include "platform/logger.hpp"
#include <algorithm>
#include <cstring>
#include <vector>

namespace nemu::core::gpu {

Maxwell3D::Maxwell3D(std::shared_ptr<IGpuBackend> backend)
    : backend_(std::move(backend)) {
    InitializeRegisterDefaults();
}

void Maxwell3D::InitializeRegisterDefaults() {
    // Ported from yuzu Maxwell3D::InitializeRegisterDefaults (GPL-3.0-or-later,
    // src/video_core/engines/maxwell_3d.cpp). Real games expect these defaults
    // at boot and never explicitly set some of them (ARMS needs the depth
    // range; Sonic Mania needs the color masks; Doom/Bomberman rely on sane
    // blend defaults).
    // regs_ is already zeroed by its member initializer.

    // Depth range near/far defaults 0.0f/1.0f (single viewport model).
    regs_.SetFloat(MaxwellMethod::ViewportDepthRangeNear, 0.0f);
    regs_.SetFloat(MaxwellMethod::ViewportDepthRangeFar, 1.0f);

    // Depth bounds: default 0.0f .. 1.0f, disabled
    regs_.SetFloat(MaxwellMethod::DepthBoundsNear, 0.0f);
    regs_.SetFloat(MaxwellMethod::DepthBoundsFar, 1.0f);
    regs_.regs[MaxwellMethod::DepthBoundsEnable] = 0;

    // Blend defaults: Add, One, Zero (D3D-style equation encoding).
    regs_.regs[MaxwellMethod::BlendEquationRgb] = 1; // Add
    regs_.regs[MaxwellMethod::BlendEnablePerRT0] = 0;
    regs_.regs[MaxwellMethod::BlendPerTargetEnabled] = 0;
    regs_.SetFloat(MaxwellMethod::BlendColorR, 0.0f);
    regs_.SetFloat(MaxwellMethod::BlendColorG, 0.0f);
    regs_.SetFloat(MaxwellMethod::BlendColorB, 0.0f);
    regs_.SetFloat(MaxwellMethod::BlendColorA, 0.0f);

    // Stencil: Keep/Keep/Keep ops, Always func, full masks (GL encoding).
    regs_.regs[MaxwellMethod::StencilEnable] = 1;
    regs_.regs[MaxwellMethod::StencilEnableHw] = 1;
    regs_.regs[MaxwellMethod::StencilFrontOpFail] = 1;     // Keep
    regs_.regs[MaxwellMethod::StencilFrontOpFailHw] = 1;   // Keep
    regs_.regs[MaxwellMethod::StencilFrontOpZfail] = 1;    // Keep
    regs_.regs[MaxwellMethod::StencilFrontOpZpass] = 1;    // Keep
    regs_.regs[MaxwellMethod::StencilFrontFuncRef] = 0;
    regs_.regs[MaxwellMethod::StencilFrontFuncMask] = 0xFFFFFFFFu;
    regs_.regs[MaxwellMethod::StencilFrontMask] = 0xFFFFFFFFu;
    regs_.regs[MaxwellMethod::StencilFrontRefHw] = 0;
    regs_.regs[MaxwellMethod::StencilFrontFuncMaskHw] = 0xFFFFFFFFu;
    regs_.regs[MaxwellMethod::StencilFrontMaskHw] = 0xFFFFFFFFu;
    regs_.regs[MaxwellMethod::StencilTwoSideEnable] = 1;
    regs_.regs[MaxwellMethod::StencilBackRef] = 0;
    regs_.regs[MaxwellMethod::StencilBackMask] = 0xFFFFFFFFu;
    regs_.regs[MaxwellMethod::StencilBackFuncMask] = 0xFFFFFFFFu;

    // Depth test func = Always (GL encoding: Always = 0x207 -> our 7 encoding).
    regs_.regs[MaxwellMethod::DepthFunc] = 7;
    regs_.regs[MaxwellMethod::DepthFuncHw] = 7;
    regs_.regs[MaxwellMethod::DepthTestEnable] = 0; // disabled until guest sets it
    regs_.regs[MaxwellMethod::DepthTestEnableHw] = 0;
    regs_.regs[MaxwellMethod::DepthWriteEnable] = 1;
    regs_.regs[MaxwellMethod::DepthWriteEnableHw] = 1;

    // Front face CCW (0), cull face Back (1), culling disabled by default.
    regs_.regs[MaxwellMethod::FrontFace] = 0;
    regs_.regs[MaxwellMethod::GlFrontFace] = 0;
    regs_.regs[MaxwellMethod::CullFace] = 1;
    regs_.regs[MaxwellMethod::GlCullFace] = 1;
    regs_.regs[MaxwellMethod::CullFaceEnable] = 0;
    regs_.regs[MaxwellMethod::GlCullTestEnabled] = 0;

    // Point size default 1.0 (OpenGL default).
    regs_.SetFloat(MaxwellMethod::PointSize, 1.0f);
    regs_.SetFloat(MaxwellMethod::PointSizeHw, 1.0f);

    // Color masks (Sonic Mania / yuzu boot requirement: all components enabled).
    regs_.regs[MaxwellMethod::ColorMaskCommon] = 0x1111;
    for (u32 i = 0; i < 8; ++i) {
        regs_.regs[MaxwellMethod::ColorMaskRT0 + i] = 0x1111;
    }
    regs_.regs[MaxwellMethod::ColorTargetMrtEnable] = 1;

    // Rasterize enable, Line width, Polygon modes
    regs_.regs[MaxwellMethod::RasterizeEnable] = 1;
    regs_.regs[MaxwellMethod::FramebufferSrgb] = 1;
    regs_.SetFloat(MaxwellMethod::LineWidthSmooth, 1.0f);
    regs_.SetFloat(MaxwellMethod::LineWidthAliased, 1.0f);
    regs_.regs[MaxwellMethod::PolygonModeFront] = 2; // Fill
    regs_.regs[MaxwellMethod::PolygonModeBack] = 2;  // Fill
    regs_.regs[MaxwellMethod::ClearControl] = 0;
}

void Maxwell3D::SetGpuMemory(std::shared_ptr<GpuMemoryManager> gmmu) {
    gmmu_ = std::move(gmmu);
    buffer_cache_ = std::make_shared<BufferCache>(gmmu_);
    upload_ = std::make_unique<EngineUpload>(gmmu_.get());
}

void Maxwell3D::ProcessMethod(u32 method, u32 argument) {
    const u32 reg_idx = method & 0xFFF;
    regs_.regs[reg_idx] = argument;

    if (method >= MaxwellMethod::MacroCallBase && method <= MaxwellMethod::MacroCallEnd) {
        const u32 slot = method - MaxwellMethod::MacroCallBase;
        if (macro_engine_.HasMacro(slot)) {
            ExecuteMacro(slot, argument);
            return;
        }
    }

    switch (method) {
        case MaxwellMethod::Nop:
        case MaxwellMethod::NopHw:
        case MaxwellMethod::WaitForIdle:
        case MaxwellMethod::WaitForIdleHw:
        case MaxwellMethod::SyncInfo:
        case MaxwellMethod::FragmentBarrier:
        case MaxwellMethod::PipeNop:
            break;

        // Inline-to-memory upload engine (yuzu engine_upload port).
        case 0x0060: case 0x0061: case 0x0062: case 0x0063:
        case 0x0064: case 0x0065: case 0x0066: case 0x0067:
        case 0x0068: case 0x0069: case 0x006A: case 0x006B:
            if (upload_) upload_->SetReg(method, argument);
            break;
        case MaxwellMethod::LaunchDma:
            if (upload_) upload_->ProcessExec((argument & 1) != 0);
            break;
        case MaxwellMethod::InlineData:
            if (upload_) upload_->ProcessData(argument, true);
            break;

        case MaxwellMethod::BindMacro:
            macro_engine_.BindMacro(argument);
            break;

        case MaxwellMethod::LoadMacro:
            macro_engine_.LoadMacroCode(argument);
            break;

        case MaxwellMethod::StreamOutEnable:
        case MaxwellMethod::StreamOutBufferAddressHigh:
        case MaxwellMethod::StreamOutBufferAddressLow:
        case MaxwellMethod::StreamOutBufferSize:
        case MaxwellMethod::StreamOutStride:
        case MaxwellMethod::ViewportClipControl:
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

        case MaxwellMethod::DrawTexture:
            ExecuteDrawTexture(argument);
            break;

        case MaxwellMethod::DispatchCompute:
        case MaxwellMethod::ComputeLaunch:
            ExecuteDispatchCompute(argument);
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

        case MaxwellMethod::ClearDepth:
        case MaxwellMethod::ClearDepthHw: {
            if (backend_) {
                float depth = regs_.GetFloat(method == MaxwellMethod::ClearDepthHw ? MaxwellMethod::ClearDepthHw : MaxwellMethod::ClearDepth);
                backend_->ClearDepthStencil(depth, static_cast<u8>(regs_.regs[MaxwellMethod::ClearStencilHw]));
            }
            break;
        }

        case MaxwellMethod::VertexProgramAddressHigh:
        case MaxwellMethod::VertexProgramAddressLow:
        case MaxwellMethod::FragmentProgramAddressHigh:
        case MaxwellMethod::FragmentProgramAddressLow:
        case MaxwellMethod::ProgramEndOffset:
        case MaxwellMethod::VertexProgramEndOffset:
        case MaxwellMethod::FragmentProgramEndOffset:
        case MaxwellMethod::ProgramClear: {
            // A program address/clear event: re-upload guest shaders to the
            // backend on the next draw. Marking dirty here is sufficient; the
            // actual bytecode read happens in BindGuestShaders() at draw time,
            // which reads the fully-updated register set.
            programs_dirty_ = true;
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

        case MaxwellMethod::TextureAddressHigh:
        case MaxwellMethod::TextureAddressLow:
        case MaxwellMethod::TextureFormat:
        case MaxwellMethod::TextureWidth:
        case MaxwellMethod::TextureHeight: {
            // Guest texture state changed: re-bind on the next draw.
            textures_dirty_ = true;
            break;
        }

        // --- Tier-A2: rasterizer state surface -----------------------------
        case MaxwellMethod::DepthTestEnable:
        case MaxwellMethod::DepthTestEnableHw:
        case MaxwellMethod::DepthWriteEnable:
        case MaxwellMethod::DepthWriteEnableHw:
        case MaxwellMethod::DepthFunc:
        case MaxwellMethod::DepthFuncHw:
        case MaxwellMethod::DepthBoundsNear:
        case MaxwellMethod::DepthBoundsFar:
        case MaxwellMethod::DepthBoundsEnable:
        case MaxwellMethod::DepthBiasPoint:
        case MaxwellMethod::DepthBiasLine:
        case MaxwellMethod::DepthBiasTriangle:
        case MaxwellMethod::DepthBiasControl:
        case MaxwellMethod::SlopeScaleDepthBias:
        case MaxwellMethod::DepthBias:
        case MaxwellMethod::DepthBiasClamp:
        case MaxwellMethod::BlendEnablePerRT0:
        case MaxwellMethod::BlendPerTargetEnabled:
        case MaxwellMethod::BlendSeparateAlpha:
        case MaxwellMethod::BlendEquationRgb:
        case MaxwellMethod::BlendHw:
        case MaxwellMethod::BlendColorR:
        case MaxwellMethod::BlendColorG:
        case MaxwellMethod::BlendColorB:
        case MaxwellMethod::BlendColorA:
        case MaxwellMethod::BlendPerTarget:
        case MaxwellMethod::ColorMaskCommon:
        case MaxwellMethod::ColorMaskRT0:
        case MaxwellMethod::ColorMaskRT1:
        case MaxwellMethod::ColorMaskRT2:
        case MaxwellMethod::ColorMaskRT3:
        case MaxwellMethod::ColorTargetMrtEnable:
        case MaxwellMethod::FramebufferSrgb:
        case MaxwellMethod::LogicOpEnable:
        case MaxwellMethod::StencilEnable:
        case MaxwellMethod::StencilFrontOpFail:
        case MaxwellMethod::StencilFrontOpFailHw:
        case MaxwellMethod::StencilFrontOpZfail:
        case MaxwellMethod::StencilFrontOpZpass:
        case MaxwellMethod::StencilFrontFuncRef:
        case MaxwellMethod::StencilFrontFuncMask:
        case MaxwellMethod::StencilFrontMask:
        case MaxwellMethod::StencilTwoSideEnable:
        case MaxwellMethod::StencilBackOpFail:
        case MaxwellMethod::StencilBackRef:
        case MaxwellMethod::StencilBackMask:
        case MaxwellMethod::StencilBackFuncMask:
        case MaxwellMethod::AlphaTestEnable:
        case MaxwellMethod::AlphaTestEnableHw:
        case MaxwellMethod::AlphaFunc:
        case MaxwellMethod::AlphaFuncHw:
        case MaxwellMethod::AlphaRef:
        case MaxwellMethod::AlphaRefHw:
        case MaxwellMethod::MSAAEnable:
        case MaxwellMethod::MSAA_samples:
        case MaxwellMethod::CullFaceEnable:
        case MaxwellMethod::CullFaceEnableHw:
        case MaxwellMethod::FrontFace:
        case MaxwellMethod::FrontFaceHw:
        case MaxwellMethod::CullFace:
        case MaxwellMethod::CullFaceHw:
        case MaxwellMethod::D3DCullMode:
        case MaxwellMethod::RasterizeEnable:
        case MaxwellMethod::PolygonModeFront:
        case MaxwellMethod::LineWidthSmooth:
        case MaxwellMethod::LineWidthAliased:
        case MaxwellMethod::ProvokingVertex:
        case MaxwellMethod::TwoSidedLightEnabled:
        case MaxwellMethod::PolygonStippleEnabled:
        case MaxwellMethod::PointSize:
        case MaxwellMethod::PointSizeHw:
        case MaxwellMethod::PointSpriteEnable:
        case MaxwellMethod::PointCoordReplace:
        case MaxwellMethod::AntiAliasPointEnable: {
            raster_state_dirty_ = true;
            break;
        }

        // --- Tier-A2: vertex attribute format array ------------------------
        case MaxwellMethod::VertexAttribFormat0:
        case MaxwellMethod::VertexAttribFormat0 + 1:
        case MaxwellMethod::VertexAttribFormat0 + 2:
        case MaxwellMethod::VertexAttribFormat0 + 3:
        case MaxwellMethod::VertexAttribFormat0 + 4:
        case MaxwellMethod::VertexAttribFormat0 + 5:
        case MaxwellMethod::VertexAttribFormat0 + 6:
        case MaxwellMethod::VertexAttribFormat0 + 7:
        case MaxwellMethod::VertexAttribFormatHw:
            BindGuestVertexAttributes();
            break;

        // --- Tier-A4: uniform/storage buffer binds via the buffer cache ----
        case MaxwellMethod::ComputeConstBufferHigh:
        case MaxwellMethod::ComputeConstBufferLow:
        case MaxwellMethod::ComputeConstBufferSize:
            cbuffs_dirty_ = true;
            break;

        default:
            debug::BreadcrumbTrail::PushGpu(method, argument);
            NEMU_LOG_DEBUG("GPU", "Maxwell3D method 0x{:04X} = 0x{:08X}", method, argument);
            break;
    }
}

void Maxwell3D::ExecuteMacro(u32 slot, u32 argument) {
    macro_engine_.Execute(slot, std::span<const u32>(&argument, 1), [this](u32 m, u32 a) {
        ProcessMethod(m, a);
    });
}

void Maxwell3D::ExecuteClearSurface(u32 argument) {
    if (!backend_) return;

    ClearColor color{
        .r = regs_.GetFloat(MaxwellMethod::ClearColorR),
        .g = regs_.GetFloat(MaxwellMethod::ClearColorG),
        .b = regs_.GetFloat(MaxwellMethod::ClearColorB),
        .a = regs_.GetFloat(MaxwellMethod::ClearColorA)
    };
    backend_->ClearRenderTarget(color);

    // If depth/stencil clear is requested (argument bit 4/5 or ClearDepth set)
    if ((argument & 0x30) != 0) {
        float depth = regs_.GetFloat(MaxwellMethod::ClearDepth);
        if (depth == 0.0f) depth = regs_.GetFloat(MaxwellMethod::ClearDepthHw);
        u8 stencil = static_cast<u8>(regs_.regs[MaxwellMethod::ClearStencilHw]);
        backend_->ClearDepthStencil(depth, stencil);
    }
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

    if (programs_dirty_) {
        BindGuestShaders();
    }
    if (textures_dirty_) {
        BindGuestTextures();
    }
    if (raster_state_dirty_) {
        ApplyRasterizerState();
    }
    if (cbuffs_dirty_) {
        BindGuestConstantBuffers();
    }
    BindGuestVertexAttributes();

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

    // Tier-A4: prefer the buffer-cache path (GMMU-resolved guest vertex data,
    // streamed with dirty-range uploads) when a GMMU is attached.
    u64 vb_entry = 0;
    const u64 vtx_addr = (static_cast<u64>(regs_.regs[MaxwellMethod::VertexArrayAddressHigh]) << 32) |
                          static_cast<u64>(regs_.regs[MaxwellMethod::VertexArrayAddressLow]);
    const u32 stride = std::max<u32>(24u, regs_.regs[MaxwellMethod::VertexArrayStride] & 0xFF);
    if (buffer_cache_ && vtx_addr != 0 && vertex_count >= 3) {
        vb_entry = buffer_cache_->Acquire(BufferCache::Type::Vertex, vtx_addr,
                                          static_cast<u64>(stride) * vertex_count);
        if (vb_entry != 0) {
            // Upload the raw bytes to the backend as the guest vertex buffer
            // (the D3D12 path uses this directly; software path falls through
            // to the cached read below).
            std::vector<u8> vb(static_cast<size_t>(stride) * vertex_count);
            if (buffer_cache_->ReadEntry(vb_entry, 0, vb.data(), vb.size())) {
                backend_->SetGuestVertexBuffer(std::span<const u8>(vb), stride);
            }
        }
    }

    // Bind guest vertices if available; otherwise stage fallback debug geometry
    bool bound_guest_verts = false;
    if (memory_ && vtx_addr != 0 && vertex_count >= 3 && vb_entry == 0) {
        const size_t bytes_needed = vertex_count * sizeof(RasterVertex);
        if (memory_->IsValidAddress(vtx_addr, bytes_needed)) {
            RasterVertex* verts = geometry_scratch_.Resize(vertex_count);
            if (memory_->ReadBlock(vtx_addr, verts, bytes_needed)) {
                backend_->SetRasterVertices(std::span<const RasterVertex>(verts, vertex_count));
                bound_guest_verts = true;
            }
        }
    }
    if (!bound_guest_verts && vb_entry == 0 && vertex_count >= 3) {
        EmitDebugGeometry();
    }
    // Instanced draws (yuzu draw_manager port): one backend submission for the
    // whole instance count instead of N separate draws (vegetation, particles,
    // foliage — the multi-thousand-draw pattern). Vertex buffers bound above are
    // instance-invariant; per-instance data flows through the vertex shader's
    // sv_instance_id, matching how games author instanced geometry.
    const u32 instances = std::max<u32>(1u, regs_.regs[MaxwellMethod::InstanceCount]);
    backend_->DrawArraysInstanced(topology, 0, vertex_count, instances);
}

void Maxwell3D::ExecuteDrawElements(u32 argument) {
    if (!backend_) return;

    if (programs_dirty_) {
        BindGuestShaders();
    }
    if (textures_dirty_) {
        BindGuestTextures();
    }
    if (raster_state_dirty_) {
        ApplyRasterizerState();
    }
    if (cbuffs_dirty_) {
        BindGuestConstantBuffers();
    }
    BindGuestVertexAttributes();

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
    const u32 elem_size = (idx_format == 1) ? 2u : ((idx_format == 2) ? 4u : 1u);

    // Tier-A4: buffer-cache path for index data (GMMU-resolved, streamed).
    u64 ib_entry = 0;
    if (buffer_cache_ && idx_addr != 0 && index_count >= 3) {
        ib_entry = buffer_cache_->Acquire(BufferCache::Type::Index, idx_addr,
                                          static_cast<u64>(elem_size) * index_count);
    }

    bool bound_guest_indices = false;
    if (memory_ && idx_addr != 0 && index_count >= 3 && ib_entry == 0) {
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
    if (!bound_guest_indices && ib_entry == 0 && index_count >= 3) {
        EmitDebugIndexedGeometry();
    }
    backend_->DrawIndexed(topology, index_count, 0, 0);
}

void Maxwell3D::ExecuteDrawTexture([[maybe_unused]] u32 argument) {
    // Tier-A2: DrawTexture (games use it for UI/letterbox compositing). We map
    // it to a fullscreen textured draw using the current texture binding.
    if (!backend_) return;
    if (textures_dirty_) {
        BindGuestTextures();
    }
    // A fullscreen quad covering the viewport; the texture provides the color.
    RasterVertex* verts = geometry_scratch_.Resize(4);
    verts[0] = {-1.0f, -1.0f, 0.0f, 1.0f, 1.0f, 1.0f};
    verts[1] = { 1.0f, -1.0f, 0.0f, 1.0f, 1.0f, 1.0f};
    verts[2] = { 1.0f,  1.0f, 0.0f, 1.0f, 1.0f, 1.0f};
    verts[3] = {-1.0f,  1.0f, 0.0f, 1.0f, 1.0f, 1.0f};
    u32* indices = index_scratch_.Resize(6);
    indices[0] = 0; indices[1] = 1; indices[2] = 2;
    indices[3] = 0; indices[4] = 2; indices[5] = 3;
    backend_->SetRasterVertices(std::span<const RasterVertex>(verts, 4));
    backend_->SetRasterIndices(std::span<const u32>(indices, 6));
    backend_->DrawIndexed(PrimitiveTopology::Triangles, 6, 0, 0);
}

void Maxwell3D::ExecuteDispatchCompute([[maybe_unused]] u32 argument) {
    // Tier-A3: compute dispatch. Handles both hardware Kepler/Maxwell Queue
    // Meta Descriptor (QMD) launches (via method 0xAD launch_desc_loc + 0xAF
    // launch) and synthetic/direct register dispatches (method 0x0190).
    // Real games use compute for postFX, shadows, and GPU particles.
    if (!backend_) return;
    if (cbuffs_dirty_) {
        BindGuestConstantBuffers();
    }

    const u32 launch_desc_loc = regs_.regs[MaxwellMethod::ComputeLaunchDescLoc];
    if (launch_desc_loc != 0) {
        // Hardware Kepler/Maxwell Queue Meta Descriptor (QMD) launch.
        const u64 qmd_address = static_cast<u64>(launch_desc_loc) << 8;
        std::array<u8, ComputeQmd::kByteSize> qmd_bytes{};
        bool qmd_read = false;

        if (gmmu_ && gmmu_->IsMapped(qmd_address, ComputeQmd::kByteSize)) {
            qmd_read = (gmmu_->Read(qmd_address, qmd_bytes.data(), qmd_bytes.size()) == qmd_bytes.size());
        } else if (memory_) {
            qmd_read = memory_->ReadBlock(qmd_address, qmd_bytes.data(), qmd_bytes.size());
        }

        if (qmd_read) {
            const ComputeQmd qmd = ComputeQmd::FromBytes(qmd_bytes);
            const u32 grid_x = qmd.GridDimX();
            const u32 grid_y = qmd.GridDimY();
            const u32 grid_z = qmd.GridDimZ();

            // Bind constant buffers specified in the QMD descriptor table.
            for (size_t i = 0; i < ComputeQmd::kMaxConstantBuffers; ++i) {
                if (qmd.ConstantBufferValid(i)) {
                    const u64 cb_addr = qmd.ConstantBufferAddress(i);
                    const u32 cb_size = qmd.ConstantBufferSize(i);
                    if (cb_addr != 0 && cb_size > 0 && cb_size <= 65536) {
                        std::vector<u8> cb_data(cb_size);
                        bool cb_read = false;
                        if (gmmu_ && gmmu_->IsMapped(cb_addr, cb_size)) {
                            cb_read = (gmmu_->Read(cb_addr, cb_data.data(), cb_size) == cb_size);
                        } else if (memory_) {
                            cb_read = memory_->ReadBlock(cb_addr, cb_data.data(), cb_size);
                        }
                        if (cb_read) {
                            backend_->SetGuestConstantBuffer(static_cast<u32>(i), cb_data.data(), cb_size);
                        }
                    }
                }
            }

            // Determine compute program entry address.
            u64 code_addr = (static_cast<u64>(regs_.regs[MaxwellMethod::ComputeEntryAddressHigh]) << 32) |
                             static_cast<u64>(regs_.regs[MaxwellMethod::ComputeEntryAddressLow]);
            if (code_addr == 0) {
                code_addr = qmd.ProgramOffset();
            } else {
                code_addr += qmd.ProgramOffset();
            }

            if (code_addr != 0) {
                const u32 prog_bytes = (regs_.regs[MaxwellMethod::ComputeProgramSize] + 1) & 0xFFFFFF;
                const u32 read_size = (prog_bytes > 0 && prog_bytes < (1u << 16)) ? prog_bytes : 256;
                std::vector<u8> cs(read_size);
                bool cs_read = false;
                if (gmmu_ && gmmu_->IsMapped(code_addr, read_size)) {
                    cs_read = (gmmu_->Read(code_addr, cs.data(), read_size) == read_size);
                } else if (memory_) {
                    cs_read = memory_->ReadBlock(code_addr, cs.data(), read_size);
                }
                if (cs_read) {
                    backend_->SetComputeShader(cs);
                }
            }

            NEMU_LOG_DEBUG("GPU", "DispatchCompute (QMD): grid {}x{}x{}, entry 0x{:X}",
                           grid_x, grid_y, grid_z, code_addr);
            backend_->DispatchCompute(grid_x, grid_y, grid_z);
            return;
        }
    }

    // Direct / synthetic fallback dispatch path.
    const u32 block_x = (regs_.regs[MaxwellMethod::ComputeLaunchDesc] >> 0) & 0xFFFF;
    const u32 block_y = (regs_.regs[MaxwellMethod::ComputeLaunchDesc] >> 16) & 0xFFFF;
    // Grid dims from the dedicated registers (fall back to block-dims-derived).
    const u32 grid_x = regs_.regs[MaxwellMethod::ComputeGridDimX] ? regs_.regs[MaxwellMethod::ComputeGridDimX] : block_x;
    const u32 grid_y = regs_.regs[MaxwellMethod::ComputeGridDimY] ? regs_.regs[MaxwellMethod::ComputeGridDimY] : block_y;
    const u32 grid_z = regs_.regs[MaxwellMethod::ComputeGridDimZ] ? regs_.regs[MaxwellMethod::ComputeGridDimZ] : 1;
    const u64 entry = (static_cast<u64>(regs_.regs[MaxwellMethod::ComputeEntryAddressHigh]) << 32) |
                       static_cast<u64>(regs_.regs[MaxwellMethod::ComputeEntryAddressLow]);

    // Upload the compute shader bytecode (from GMMU or guest memory) to the backend.
    if (entry != 0) {
        const u32 prog_bytes = (regs_.regs[MaxwellMethod::ComputeProgramSize] + 1) & 0xFFFFFF;
        if (prog_bytes < (1u << 16)) {
            std::vector<u8> cs(prog_bytes);
            bool cs_read = false;
            if (gmmu_ && gmmu_->IsMapped(entry, prog_bytes)) {
                cs_read = (gmmu_->Read(entry, cs.data(), prog_bytes) == prog_bytes);
            } else if (memory_) {
                cs_read = memory_->ReadBlock(entry, cs.data(), cs.size());
            }
            if (cs_read) {
                backend_->SetComputeShader(cs);
            }
        }
    }

    NEMU_LOG_DEBUG("GPU", "DispatchCompute: grid {}x{}x{}, entry 0x{:X}",
                   grid_x, grid_y, grid_z, entry);
    // The backend interface gains DispatchCompute; no-op default keeps the
    // software path working while D3D12 implements it.
    backend_->DispatchCompute(grid_x, grid_y, grid_z);
}

void Maxwell3D::ApplyRasterizerState() {
    // Tier-A2: forward the guest rasterizer state block to the backend.
    if (!backend_) {
        raster_state_dirty_ = false;
        return;
    }
    RasterizerState rs{};
    rs.depth_test_enable = (regs_.regs[MaxwellMethod::DepthTestEnable] != 0) ||
                           (regs_.regs[MaxwellMethod::DepthTestEnableHw] != 0);
    rs.depth_write_enable = (regs_.regs[MaxwellMethod::DepthWriteEnable] != 0) ||
                            (regs_.regs[MaxwellMethod::DepthWriteEnableHw] != 0);
    rs.depth_func = regs_.regs[MaxwellMethod::DepthFuncHw] != 0
                        ? regs_.regs[MaxwellMethod::DepthFuncHw]
                        : regs_.regs[MaxwellMethod::DepthFunc];
    rs.stencil_enable = (regs_.regs[MaxwellMethod::StencilEnable] != 0) ||
                        (regs_.regs[MaxwellMethod::StencilEnableHw] != 0);
    rs.alpha_test_enable = (regs_.regs[MaxwellMethod::AlphaTestEnable] != 0) ||
                           (regs_.regs[MaxwellMethod::AlphaTestEnableHw] != 0);
    rs.alpha_ref = regs_.GetFloat(MaxwellMethod::AlphaRef);
    if (rs.alpha_ref == 0.0f) {
        rs.alpha_ref = regs_.GetFloat(MaxwellMethod::AlphaRefHw);
    }
    rs.cull_face_enable = (regs_.regs[MaxwellMethod::CullFaceEnable] != 0) ||
                          (regs_.regs[MaxwellMethod::CullFaceEnableHw] != 0);
    rs.front_face = regs_.regs[MaxwellMethod::FrontFaceHw] != 0
                        ? regs_.regs[MaxwellMethod::FrontFaceHw]
                        : regs_.regs[MaxwellMethod::FrontFace];
    rs.cull_face = regs_.regs[MaxwellMethod::CullFaceHw] != 0
                       ? regs_.regs[MaxwellMethod::CullFaceHw]
                       : regs_.regs[MaxwellMethod::CullFace];
    rs.msaa_samples = regs_.regs[MaxwellMethod::MSAAEnable]
                          ? regs_.regs[MaxwellMethod::MSAA_samples]
                          : 1u;
    rs.blend_enable_0 = (regs_.regs[MaxwellMethod::BlendEnablePerRT0] != 0) ||
                        (regs_.regs[MaxwellMethod::BlendPerTargetEnabled] != 0);
    rs.blend_equation_rgb = regs_.regs[MaxwellMethod::BlendEquationRgb];

    // Depth bounds
    rs.depth_bounds_enable = regs_.regs[MaxwellMethod::DepthBoundsEnable] != 0;
    rs.depth_bounds_near = regs_.GetFloat(MaxwellMethod::DepthBoundsNear);
    rs.depth_bounds_far = regs_.GetFloat(MaxwellMethod::DepthBoundsFar);

    // Depth bias / Polygon offset
    rs.polygon_offset_enable = (regs_.regs[MaxwellMethod::DepthBiasPoint] != 0) ||
                               (regs_.regs[MaxwellMethod::DepthBiasLine] != 0) ||
                               (regs_.regs[MaxwellMethod::DepthBiasTriangle] != 0);
    rs.polygon_offset_factor = regs_.GetFloat(MaxwellMethod::SlopeScaleDepthBias);
    rs.polygon_offset_units = regs_.GetFloat(MaxwellMethod::DepthBias);
    rs.polygon_offset_clamp = regs_.GetFloat(MaxwellMethod::DepthBiasClamp);

    // Line width & Polygon modes
    rs.line_width = regs_.GetFloat(MaxwellMethod::LineWidthSmooth);
    if (rs.line_width == 0.0f) rs.line_width = regs_.GetFloat(MaxwellMethod::LineWidthAliased);
    if (rs.line_width == 0.0f) rs.line_width = 1.0f;
    rs.polygon_mode_front = regs_.regs[MaxwellMethod::PolygonModeFront];
    rs.polygon_mode_back = regs_.regs[MaxwellMethod::PolygonModeBack];

    // Blend color
    rs.blend_color[0] = regs_.GetFloat(MaxwellMethod::BlendColorR);
    rs.blend_color[1] = regs_.GetFloat(MaxwellMethod::BlendColorG);
    rs.blend_color[2] = regs_.GetFloat(MaxwellMethod::BlendColorB);
    rs.blend_color[3] = regs_.GetFloat(MaxwellMethod::BlendColorA);

    // Color mask
    for (size_t i = 0; i < 4; ++i) {
        rs.color_mask[i] = regs_.regs[MaxwellMethod::ColorMaskRT0 + i];
    }
    rs.logic_op_enable = regs_.regs[MaxwellMethod::LogicOpEnable] != 0;
    rs.logic_op = regs_.regs[MaxwellMethod::LogicOpEnable];

    // Multi-Render-Target (MRT) count detection
    u32 mrt_mask = regs_.regs[MaxwellMethod::ColorTargetMrtEnable];
    if (mrt_mask == 0) {
        u8 count = 1;
        for (u8 i = 1; i < 4; ++i) {
            if (regs_.regs[MaxwellMethod::ColorMaskRT0 + i] != 0 ||
                regs_.regs[MaxwellMethod::BlendEnablePerRT0 + i] != 0) {
                count = i + 1;
            }
        }
        rs.num_render_targets = count;
    } else {
        u8 count = 0;
        for (u8 i = 0; i < 8; ++i) {
            if (mrt_mask & (1u << i)) {
                count = i + 1;
            }
        }
        rs.num_render_targets = std::max<u8>(count, 1);
    }

    backend_->SetRasterizerState(rs);
    raster_state_dirty_ = false;
}

void Maxwell3D::BindGuestConstantBuffers() {
    // Tier-A4: resolve the guest uniform buffer (constant buffer slot 0) via
    // the buffer cache and bind it to the backend. Games pack view/proj mats,
    // light params, etc. into this.
    if (!backend_) {
        cbuffs_dirty_ = false;
        return;
    }
    const u64 cb_addr = (static_cast<u64>(regs_.regs[MaxwellMethod::ComputeConstBufferHigh]) << 32) |
                         static_cast<u64>(regs_.regs[MaxwellMethod::ComputeConstBufferLow]);
    const u32 cb_size = regs_.regs[MaxwellMethod::ComputeConstBufferSize];

    if (cb_addr == 0 || cb_size == 0) {
        cbuffs_dirty_ = false;
        return;
    }
    if (buffer_cache_) {
        const u64 entry = buffer_cache_->Acquire(BufferCache::Type::Uniform, cb_addr, cb_size);
        if (entry != 0) {
            std::vector<u8> cb(cb_size);
            if (buffer_cache_->ReadEntry(entry, 0, cb.data(), cb.size())) {
                backend_->SetGuestConstantBuffer(0, cb.data(), static_cast<u32>(cb.size()));
            }
        }
    } else if (memory_) {
        // No GMMU attached (unit-test path): read straight from guest memory.
        constexpr u32 kMaxCbufBytes = 1u << 16;
        std::vector<u8> cb(std::min<u32>(cb_size, kMaxCbufBytes));
        if (memory_->ReadBlock(cb_addr, cb.data(), cb.size())) {
            backend_->SetGuestConstantBuffer(0, cb.data(), static_cast<u32>(cb.size()));
        }
    }
    cbuffs_dirty_ = false;
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

void Maxwell3D::BindGuestShaders() {
    if (!backend_ || !memory_) {
        programs_dirty_ = false;
        return;
    }

    // Upload guest Maxwell shader bytecode to the backend once (until the guest
    // changes a program address/clear method, which sets programs_dirty_).
    const u64 vs_addr = (static_cast<u64>(regs_.regs[MaxwellMethod::VertexProgramAddressHigh]) << 32) |
                         static_cast<u64>(regs_.regs[MaxwellMethod::VertexProgramAddressLow]);
    const u64 fs_addr = (static_cast<u64>(regs_.regs[MaxwellMethod::FragmentProgramAddressHigh]) << 32) |
                         static_cast<u64>(regs_.regs[MaxwellMethod::FragmentProgramAddressLow]);

    if (vs_addr == 0 && fs_addr == 0) {
        // No guest programs -> clear any previously-bound shaders and fall back
        // to the backend's passthrough pipeline.
        backend_->SetGuestShaders({}, {});
        programs_dirty_ = false;
        return;
    }

    // Program sizes = end offset + 1 (offset, not count), in bytes. Per-stage.
    const u32 vs_bytes_req = (regs_.regs[MaxwellMethod::VertexProgramEndOffset] & 0xFFFFFF) + 1u;
    const u32 fs_bytes_req = (regs_.regs[MaxwellMethod::FragmentProgramEndOffset] & 0xFFFFFF) + 1u;
    const u32 kMaxProgramBytes = 1u << 16; // safety bound; real programs are small

    std::vector<u8> vs_bytes;
    std::vector<u8> fs_bytes;
    if (vs_addr != 0) {
        const u32 n = std::min(vs_bytes_req, kMaxProgramBytes);
        vs_bytes.resize(n);
        if (!memory_->ReadBlock(vs_addr, vs_bytes.data(), n)) {
            vs_bytes.clear();
        }
    }
    if (fs_addr != 0) {
        const u32 n = std::min(fs_bytes_req, kMaxProgramBytes);
        fs_bytes.resize(n);
        if (!memory_->ReadBlock(fs_addr, fs_bytes.data(), n)) {
            fs_bytes.clear();
        }
    }

    backend_->SetGuestShaders(vs_bytes, fs_bytes);
    programs_dirty_ = false;
    NEMU_LOG_DEBUG("GPU", "Bound guest shaders to backend (VS {} B, PS {} B)", vs_bytes.size(), fs_bytes.size());
}

void Maxwell3D::BindGuestTextures() {
    if (!backend_) {
        textures_dirty_ = false;
        return;
    }

    // Build a single texture binding from the Maxwell texture state registers.
    const u64 tex_addr = (static_cast<u64>(regs_.regs[MaxwellMethod::TextureAddressHigh]) << 32) |
                          static_cast<u64>(regs_.regs[MaxwellMethod::TextureAddressLow]);

    if (tex_addr == 0) {
        backend_->SetGuestTextureCount(0);
        textures_dirty_ = false;
        return;
    }

    texture::TextureDescriptor desc{};
    desc.gpu_address = tex_addr;
    desc.width = std::max(1u, regs_.regs[MaxwellMethod::TextureWidth]);
    desc.height = std::max(1u, regs_.regs[MaxwellMethod::TextureHeight]);
    desc.depth = 1;
    desc.mip_levels = 1;
    desc.is_block_linear = true;
    desc.block_height_gobs = 1;
    desc.bytes_per_pixel = 4;
    desc.format = texture::TextureFormat::RGBA8_UNORM;

    backend_->SetGuestTextureBinding(0, desc, memory_);
    backend_->SetGuestTextureCount(1);
    textures_dirty_ = false;
    NEMU_LOG_DEBUG("GPU", "Bound guest texture to backend ({}x{} @ 0x{:X})", desc.width, desc.height, desc.gpu_address);
}

void Maxwell3D::BindGuestVertexAttributes() {
    if (!backend_) {
        return;
    }
    // Build the guest vertex-attribute layout (POSITION implicit at offset 0).
    // Conventions mirror the D3D12 default one-RasterVertex layout:
    //   POSITION  -> offset 0, R32G32_FLOAT
    //   TEXCOORD0 -> offset 8, R32G32B32A32_FLOAT (rgba)  [DXGI_FORMAT 28]
    const u32 stride = std::max<u32>(24u, regs_.regs[MaxwellMethod::VertexArrayStride] & 0xFF);
    GuestVertexAttrib attrs[2]{};
    attrs[1].attr_index = 0;
    attrs[1].format_id = 28; // DXGI_FORMAT_R32G32B32A32_FLOAT
    attrs[1].offset = 8;
    attrs[1].slot = 0;
    attrs[1].stride = static_cast<u16>(stride);
    attrs[1].valid = true;
    backend_->SetGuestVertexAttributes(std::span<const GuestVertexAttrib>(attrs, 2));

    // Read the guest vertex buffer from memory (bounded) and upload it.
    const u64 va = (static_cast<u64>(regs_.regs[MaxwellMethod::VertexArrayAddressHigh]) << 32) |
                   static_cast<u64>(regs_.regs[MaxwellMethod::VertexArrayAddressLow]);
    if (va == 0 || !memory_) {
        backend_->SetGuestVertexBuffer({}, stride);
        return;
    }
    constexpr u32 kMaxVbBytes = 1u << 16; // safety bound
    std::vector<u8> vb(static_cast<size_t>(stride) * 3);
    const size_t n = std::min<size_t>(vb.size(), kMaxVbBytes);
    vb.resize(n);
    if (!memory_->ReadBlock(va, vb.data(), n)) {
        backend_->SetGuestVertexBuffer({}, stride);
        return;
    }
    backend_->SetGuestVertexBuffer(std::span<const u8>(vb), stride);
    NEMU_LOG_DEBUG("GPU", "Bound guest vertex buffer to backend ({} B, stride {})", n, stride);
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

        if (mode == 2) {
            if (method == MaxwellMethod::InlineData && upload_ && index < pushbuffer.size()) {
                upload_->ProcessExec(true);
                const size_t avail = std::min<size_t>(count, pushbuffer.size() - index);
                std::span<const u32> payload{pushbuffer.data() + index, avail};
                upload_->ProcessData(payload);
                index += avail;
            } else {
                ProcessMethod(method, count);
            }
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
