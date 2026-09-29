#pragma once

#include "core/types.hpp"
#include <span>
#include <string_view>
#include <memory>

namespace nemu::core::memory { class VirtualMemory; }
namespace nemu::core::gpu::texture { struct TextureDescriptor; struct SamplerDescriptor; }
#include "pipeline/graphics_optimizer.hpp"

namespace nemu::core::gpu {

enum class PrimitiveTopology : u32 {
    Points = 0,
    Lines = 1,
    LineStrip = 2,
    Triangles = 3,
    TriangleStrip = 4,
    TriangleFan = 5
};

enum class PixelFormat : u32 {
    R8G8B8A8_UNORM,
    B8G8R8A8_UNORM,
    R16G16B16A16_FLOAT,
    D24_UNORM_S8_UINT,
    D32_FLOAT
};

struct Viewport {
    float x{0.0f};
    float y{0.0f};
    float width{1280.0f};
    float height{720.0f};
    float min_depth{0.0f};
    float max_depth{1.0f};
};

struct ScissorRect {
    u32 left{0};
    u32 top{0};
    u32 right{1280};
    u32 bottom{720};
};

struct ClearColor {
    float r{0.0f};
    float g{0.0f};
    float b{0.0f};
    float a{1.0f};
};

struct GpuStats {
    u64 frames_presented{0};
    u64 draw_calls{0};
    u64 vertices_submitted{0};
    u64 frames_upscaled{0};   // Tier-B1 optimizer pipeline
    u64 frames_generated{0};  // framegen interpolated frames
};

/// Guest rasterizer state block (Tier-A2). Games push these every frame;
/// the D3D12 backend maps them onto PSO/graphics state, the software
/// rasterizer honors depth test/func + alpha test, other backends ignore.
struct RasterizerState {
    bool depth_test_enable{false};
    bool depth_write_enable{true};
    u32 depth_func{7};        // GL-style compare (0 Never .. 7 Always)
    bool stencil_enable{false};
    bool alpha_test_enable{false};
    float alpha_ref{0.0f};
    bool cull_face_enable{false};
    u32 front_face{0};        // 0 = CCW, 1 = CW
    u32 cull_face{1};         // 0 = front, 1 = back, 2 = front_and_back
    u32 msaa_samples{1};
    bool blend_enable_0{false};
    u32 blend_equation_rgb{1}; // GL func enum (1 = Add)

    // Tier-A2: expanded Maxwell 3D rasterizer state surface
    bool depth_bounds_enable{false};
    float depth_bounds_near{0.0f};
    float depth_bounds_far{1.0f};
    bool polygon_offset_enable{false};
    float polygon_offset_factor{0.0f};
    float polygon_offset_units{0.0f};
    float polygon_offset_clamp{0.0f};
    float line_width{1.0f};
    u32 polygon_mode_front{2}; // 0 = Point, 1 = Line, 2 = Fill
    u32 polygon_mode_back{2};
    float blend_color[4]{0.0f, 0.0f, 0.0f, 0.0f};
    u32 color_mask[4]{0x1111, 0x1111, 0x1111, 0x1111};
    bool logic_op_enable{false};
    u32 logic_op{0};

    // Multi-Render-Target (MRT) and Depth-Stencil format configuration
    u8 num_render_targets{1};
    u8 rtv_formats[8]{}; // 0 = default (DXGI_FORMAT_R8G8B8A8_UNORM)
    u8 dsv_format{0};     // 0 = default (DXGI_FORMAT_D32_FLOAT)
};

/// Live graphics-optimizer settings (Tier-B UI wiring). The frontend Settings
/// UI mutates config → Emulator::ApplyRuntimeConfig pushes this into the
/// backend → Present() applies upscaling/AA/framegen on the presented frame.
struct FrameOptimizerSettings {
    pipeline::UpscalerMode upscaler{pipeline::UpscalerMode::Bilinear};
    float fsr_sharpness{0.8f};
    pipeline::AntiAliasingMode anti_aliasing{pipeline::AntiAliasingMode::None};
    pipeline::FrameGenMode frame_generation{pipeline::FrameGenMode::Disabled};
    bool enabled{false}; // master switch: false = present frames untouched
};

// A rasterizer vertex: 2D normalized-device position (x,y in [-1,1]) plus an
// RGBA color. This is the minimal geometric primitive the software rasterizer
// (and future guest shader output) feed into the backend.
struct RasterVertex {
    float x{0.0f};
    float y{0.0f};
    float r{1.0f};
    float g{1.0f};
    float b{1.0f};
    float a{1.0f};
};

/// One guest vertex attribute for the D3D12 PSO input layout. Position is
/// implicit (POSITION, R32G32_FLOAT at offset 0 of slot 0); each entry here maps
/// to a TEXCOORD{n} input element with the given format/offset/slot/stride.
struct GuestVertexAttrib {
    u8 attr_index{0};   // TEXCOORD semantic index
    u8 format_id{0};    // DXGI_FORMAT id (low byte)
    u8 offset{0};       // byte offset within the vertex slot
    u8 slot{0};         // vertex buffer slot
    u16 stride{16};     // slot stride in bytes
    bool valid{false};
};

class IGpuBackend {
public:
    virtual ~IGpuBackend() = default;

    virtual bool Initialize(u32 render_width, u32 render_height) = 0;
    virtual void Shutdown() = 0;

    virtual void BeginFrame() = 0;
    virtual void EndFrame() = 0;
    virtual void Present() = 0;

    virtual void SetViewport(const Viewport& viewport) = 0;
    virtual void SetScissor(const ScissorRect& scissor) = 0;
    virtual void ClearRenderTarget(const ClearColor& color) = 0;
    virtual void ClearDepthStencil(float depth, u8 stencil) = 0;

    virtual void DrawArrays(PrimitiveTopology topology, u32 first_vertex, u32 vertex_count) = 0;
    virtual void DrawIndexed(PrimitiveTopology topology, u32 index_count, u32 first_index, u32 base_vertex) = 0;

    // --- Tier-A2/A3: rasterizer state + compute dispatch --------------------
    // Push the guest rasterizer state block (depth/stencil/blend/MSAA/cull).
    // Default no-op: backends that cannot honor it (Null passthrough) ignore.
    virtual void SetRasterizerState(const RasterizerState& /*state*/) {}
    // Dispatch a compute shader launch (Tier-A3). block_x/y are the guest
    // workgroup dims. Default no-op; D3D12 builds a compute PSO, Null counts.
    virtual void DispatchCompute(u32 /*block_x*/, u32 /*block_y*/, u32 /*block_z*/) {}

    // --- Live frame-optimizer settings (Tier-B UI wiring) -------------------
    // Push upscaler/AA/framegen config so Present() applies the optimization
    // pipeline. Live: the Switch Settings UI can change it every frame.
    virtual void SetFrameOptimizerSettings(const FrameOptimizerSettings& /*settings*/) {}

    // --- Guest draw state (translation-layer input) -----------------------
    // These carry the real guest graphics state that the D3D12 backend feeds
    // through the Maxwell->HLSL->PSO translation chain (ShaderTranslator,
    // PipelineBridge, PipelineCache). Defaults are no-ops so backends that only
    // render the software/passthrough path (Null, SDL2, or a D3D12 backend in
    // fallback mode) are unaffected.
    virtual void SetGuestShaders(std::span<const u8> /*vs_bytecode*/, std::span<const u8> /*ps_bytecode*/) {}
    // Cap resident texture memory (bytes) for the 5 GiB budget. Default no-op;
    // the D3D12 backend forwards to its TextureCache's byte-budget LRU.
    virtual void SetTextureByteBudget(size_t /*bytes*/) {}
    // Carry a guest compute shader program so the translated backend can build
    // and dispatch a compute PSO. Default no-op (software backends count the
    // dispatch only).
    virtual void SetComputeShader(std::span<const u8> /*compute_bytecode*/) {}
    virtual void SetGuestConstantBuffer(u32 /*slot*/, const void* /*data*/, u32 /*bytes*/) {}

    // --- Guest texture state (translation-layer input) ---------------------
    // Carry the real guest texture/sampler bindings so the D3D12 backend can
    // build and bind SRV descriptor tables into the translated root signature.
    // `binding` is the shader-visible register/slot index. Defaults are no-ops
    // so backends without texture support (Null, SDL2) are unaffected.
    virtual void SetGuestTextureBinding(u32 /*binding*/, const texture::TextureDescriptor& /*desc*/, memory::VirtualMemory* /*memory*/) {}
    virtual void SetGuestSamplerBinding(u32 /*binding*/, const texture::SamplerDescriptor& /*desc*/) {}
    virtual void SetGuestTextureCount(u32 /*count*/) {}

    /// Guest vertex-attribute layout (position implicit at offset 0). The D3D12
    /// backend renders this into the PSO input layout; Null/SDL2 ignore it.
    /// `attrs` are {semantic_index, DXGI_FORMAT id, byte offset, slot, stride}.
    virtual void SetGuestVertexAttributes(std::span<const GuestVertexAttrib> /*attrs*/) {}

    /// Raw guest vertex-buffer bytes + per-vertex stride (matching the layout
    /// passed to SetGuestVertexAttributes). The D3D12 backend uploads and binds
    /// this instead of the fixed RasterVertex buffer. Null/SDL2 ignore it.
    virtual void SetGuestVertexBuffer(std::span<const u8> /*data*/, u32 /*stride*/) {}

    // --- NVDEC video presentation -------------------------------------------
    // Carry a decoded video frame (NV12: Y plane + interleaved UV) to the
    // backend for presentation via the VIC (video output compositor) path.
    // The D3D12 backend uploads into a dynamic texture and draws it as a
    // fullscreen quad; Null/SDL2 may rasterize to their framebuffer or ignore.
    // Returns true when the backend consumed the frame.
    struct NVDECFrame {
        u32 width{0};
        u32 height{0};
        std::span<const u8> nv12_data; // width*height Y + width/2*height interleaved UV
        u64 frame_number{0};
    };
    virtual bool PresentNVDECFrame(const NVDECFrame& /*frame*/) { return false; }

    // UE4 shader-storm mitigation (per-title ue4_shader_storm tweak): warm the
    // pipeline cache ahead of draws (pre-compiles the passthrough + common
    // translated PSOs) so thousands of lazy compiles don't stall the first
    // frames on console. Returns the post-warmup cached-pipeline count.
    virtual u32 WarmupShaderStorm() { return 0; }

    // --- Vertex / material binding and host-observable frame capture ---
    // These are the software-rasterizable entry points. Backends that only
    // forward to a hardware API (e.g. D3D12) may leave them as no-ops; the
    // software/Null backend rasterizes them into a host framebuffer so a first
    // real frame is observable even in a headless/CI environment.
    virtual void SetRasterVertices(std::span<const RasterVertex> vertices) { vertices_readonly_ = vertices; }
    virtual void SetRasterIndices(std::span<const u32> indices) { indices_readonly_ = indices; }
    /// Dump the current host framebuffer to a P6-binary PPM file (for headless
    /// visual verification). Returns false if no framebuffer is available.
    virtual bool DumpFramePPM(const char* path) { (void)path; return false; }

    // --- Optional crisp UI overlay -----------------------------------------
    // Backends with real font/image capability (e.g. SDL2 desktop) composite
    // anti-aliased text and cover images over the rasterized frame at present
    // time. The frontend queues overlay ops while building UI geometry and the
    // backend flushes them inside Present(). Coordinates are in the same UI
    // space as the raster vertices (default 1280x720). Default implementations
    // are no-ops so other backends (Null, D3D12) are unaffected.
    [[nodiscard]] virtual bool SupportsUiOverlay() const noexcept { return false; }
    /// Queue a text draw. align: -1 left, 0 center, 1 right (relative to x).
    virtual void UiTextOverlay(std::string_view /*text*/, float /*x*/, float /*y*/,
                               float /*size_px*/, float /*r*/, float /*g*/, float /*b*/,
                               float /*a*/, int /*align*/) {}
    /// Queue a cover image drawn into the given rect (cached by key/path).
    virtual void UiImageOverlay(std::string_view /*key*/, std::string_view /*host_path*/,
                                float /*x*/, float /*y*/, float /*w*/, float /*h*/) {}
    /// Queue a filled rectangle drawn into the overlay.
    virtual void UiFillRectOverlay(float /*x*/, float /*y*/, float /*w*/, float /*h*/,
                                   float /*r*/, float /*g*/, float /*b*/, float /*a*/) {}
    /// Queue a rectangle outline drawn into the overlay.
    virtual void UiRectOutlineOverlay(float /*x*/, float /*y*/, float /*w*/, float /*h*/,
                                      float /*thickness*/, float /*r*/, float /*g*/, float /*b*/, float /*a*/) {}

    [[nodiscard]] virtual GpuStats GetStats() const noexcept = 0;
    [[nodiscard]] virtual std::string_view GetBackendName() const noexcept = 0;

protected:
    // Retained vertex/index data for backends that rasterize on the host.
    std::span<const RasterVertex> vertices_readonly_;
    std::span<const u32> indices_readonly_;
};

} // namespace nemu::core::gpu
