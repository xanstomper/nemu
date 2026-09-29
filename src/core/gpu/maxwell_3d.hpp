#pragma once

#include "core/types.hpp"
#include "core/common/scratch_buffer.hpp"
#include "gpu_interface.hpp"
#include "gmmu.hpp"
#include "buffer_cache.hpp"
#include "engine_upload.hpp"
#include "maxwell_macro.hpp"
#include <span>
#include <array>
#include <memory>
#include <cstring>


namespace nemu::core::memory {
class VirtualMemory;
}


namespace nemu::core::gpu {

namespace MaxwellMethod {
    // Control, DMA & Synchronization
    constexpr u32 ObjectId = 0x0000;
    constexpr u32 BindMacro = 0x0038;
    constexpr u32 LoadMacro = 0x0039;
    constexpr u32 Nop = 0x0200;
    constexpr u32 NopHw = 0x0040;
    constexpr u32 Notify = 0x0041;
    constexpr u32 WaitForIdle = 0x0045;
    constexpr u32 WaitForIdleHw = 0x0044;
    constexpr u32 LoadMme = 0x0045;
    constexpr u32 ShadowRamControl = 0x0049;
    constexpr u32 Upload = 0x0060;
    constexpr u32 LaunchDma = 0x006C;
    constexpr u32 InlineData = 0x006D;
    constexpr u32 SyncInfo = 0x00B2;
    constexpr u32 FragmentBarrier = 0x0378;
    constexpr u32 PipeNop = 0x068B;
    constexpr u32 ReportSemaphore = 0x06C0;
    constexpr u32 MacroCallBase = 0x0E00;
    constexpr u32 MacroCallEnd = 0x0E7F;

    // Transform Feedback (Stream Output)
    constexpr u32 StreamOutEnable = 0x05A7;
    constexpr u32 StreamOutBufferAddressHigh = 0x05A8;
    constexpr u32 StreamOutBufferAddressLow = 0x05A9;
    constexpr u32 StreamOutBufferSize = 0x05AA;
    constexpr u32 StreamOutStride = 0x05AB;

    // Viewport Depth Clipping / Reverse-Z
    constexpr u32 ViewportClipControl = 0x0599;

    // Viewports & Scissors
    constexpr u32 ViewportTransform = 0x0280;
    constexpr u32 ViewportScaleX = 0x035D;
    constexpr u32 ViewportScaleY = 0x035E;
    constexpr u32 ViewportOffsetX = 0x0360;
    constexpr u32 ViewportOffsetY = 0x0361;
    // Viewport Z (real Maxwell: 0x035F scale-z, 0x035C/0x035D pair per vp 0).
    constexpr u32 ViewportScaleZ = 0x035F;
    constexpr u32 ViewportOffsetZ = 0x0264;
    // Depth range defaults (yuzu: near=0.0 far=1.0 required by ARMS et al).
    constexpr u32 ViewportDepthRangeNear = 0x0362;
    constexpr u32 ViewportDepthRangeFar = 0x0363;
    constexpr u32 Viewports = 0x0300;
    constexpr u32 Windows = 0x0340;
    constexpr u32 WindowOffsetX = 0x037E;
    constexpr u32 WindowOffsetY = 0x037F;
    constexpr u32 ScissorEnable = 0x0380;
    constexpr u32 ScissorX = 0x0381;
    constexpr u32 ScissorY = 0x0382;
    constexpr u32 ScissorWidth = 0x0383;
    constexpr u32 ScissorHeight = 0x0384;

    // Clear registers
    constexpr u32 ClearRect = 0x035B;
    constexpr u32 ClearColorR = 0x0368;
    constexpr u32 ClearColorG = 0x0369;
    constexpr u32 ClearColorB = 0x036A;
    constexpr u32 ClearColorA = 0x036B;
    constexpr u32 ClearSurface = 0x036C;
    constexpr u32 ClearDepth = 0x036D;
    constexpr u32 ClearDepthHw = 0x0364;
    constexpr u32 ClearStencilHw = 0x0368;
    constexpr u32 ClearControl = 0x043E;
    constexpr u32 ClearReportValue = 0x054C;
    constexpr u32 ClearSurfaceHw = 0x0674;

    // --- Rasterizer state (Tier-A2 expanded surface) ---
    constexpr u32 RasterizeEnable = 0x00DF;
    constexpr u32 PolygonModeFront = 0x036B;
    constexpr u32 PolygonModeBack = 0x036C;
    constexpr u32 PolygonSmooth = 0x036D;
    constexpr u32 LineWidthSmooth = 0x04EC;
    constexpr u32 LineWidthAliased = 0x04ED;
    constexpr u32 ProvokingVertex = 0x05A1;
    constexpr u32 TwoSidedLightEnabled = 0x05A2;
    constexpr u32 PolygonStippleEnabled = 0x05A3;

    // Depth bounds & Polygon offset / Depth bias
    constexpr u32 DepthMode = 0x035F;
    constexpr u32 DepthBoundsNear = 0x03E7;
    constexpr u32 DepthBoundsFar = 0x03E8;
    constexpr u32 DepthBounds = 0x03E7;
    constexpr u32 DepthBoundsEnable = 0x066F;
    constexpr u32 DepthBiasPoint = 0x0370;    // polygon_offset_point_enable
    constexpr u32 DepthBiasLine = 0x0371;     // polygon_offset_line_enable
    constexpr u32 DepthBiasTriangle = 0x0372; // polygon_offset_fill_enable
    constexpr u32 DepthBiasControl = 0x0444;
    constexpr u32 SlopeScaleDepthBias = 0x055B;
    constexpr u32 DepthBias = 0x056F;
    constexpr u32 DepthBiasClamp = 0x061F;

    // Depth/stencil test enable + funcs
    constexpr u32 DepthTestEnable = 0x0465;
    constexpr u32 DepthTestEnableHw = 0x04B3;
    constexpr u32 DepthWriteEnable = 0x0466;
    constexpr u32 DepthWriteEnableHw = 0x04BA;
    constexpr u32 DepthFunc = 0x0467;          // 0 Never..7 Always (GL enum - 0x200)
    constexpr u32 DepthFuncHw = 0x04C3;
    constexpr u32 StencilEnable = 0x04E4;      // front+back packed
    constexpr u32 StencilEnableHw = 0x04E0;
    constexpr u32 StencilFrontOpFail = 0x04E5; // Keep/Zero/Replace/Incr...
    constexpr u32 StencilFrontOpFailHw = 0x04E1;
    constexpr u32 StencilFrontOpZfail = 0x04E6;
    constexpr u32 StencilFrontOpZpass = 0x04E7;
    constexpr u32 StencilFrontFuncRef = 0x04E8;
    constexpr u32 StencilFrontFuncMask = 0x04E9;
    constexpr u32 StencilFrontMask = 0x04EA;
    constexpr u32 StencilFrontRefHw = 0x04E5;
    constexpr u32 StencilFrontFuncMaskHw = 0x04E6;
    constexpr u32 StencilFrontMaskHw = 0x04E7;
    constexpr u32 StencilTwoSideEnable = 0x0565;
    constexpr u32 StencilBackOpFail = 0x0566;
    constexpr u32 StencilBackRef = 0x03D5;
    constexpr u32 StencilBackMask = 0x03D6;
    constexpr u32 StencilBackFuncMask = 0x03D7;

    // Blend & Color
    constexpr u32 BlendEnablePerRT0 = 0x04D4;  // per-RT blend enables (0x04D4..0x04D7)
    constexpr u32 BlendPerTargetEnabled = 0x04B9;
    constexpr u32 BlendSeparateAlpha = 0x04D8;
    constexpr u32 BlendEquationRgb = 0x04E0;   // func, src, dst triples per RT
    constexpr u32 BlendHw = 0x04CF;
    constexpr u32 BlendColorR = 0x04C7;
    constexpr u32 BlendColorG = 0x04C8;
    constexpr u32 BlendColorB = 0x04C9;
    constexpr u32 BlendColorA = 0x04CA;
    constexpr u32 BlendPerTarget = 0x0780;
    constexpr u32 FramebufferSrgb = 0x056E;
    constexpr u32 ColorMaskCommon = 0x03E4;
    constexpr u32 ColorMaskRT0 = 0x0680;
    constexpr u32 ColorMaskRT1 = 0x0681;
    constexpr u32 ColorMaskRT2 = 0x0682;
    constexpr u32 ColorMaskRT3 = 0x0683;
    constexpr u32 ColorTargetMrtEnable = 0x03EB;
    constexpr u32 LogicOpEnable = 0x0671;

    // Alpha test, MSAA, and Culling
    constexpr u32 AlphaTestEnable = 0x042C;
    constexpr u32 AlphaTestEnableHw = 0x04BB;
    constexpr u32 AlphaFunc = 0x042D;
    constexpr u32 AlphaFuncHw = 0x04C5;
    constexpr u32 AlphaRef = 0x042E;
    constexpr u32 AlphaRefHw = 0x04C4;
    constexpr u32 MSAAEnable = 0x04D0;         // raster enable / MSAA samples
    constexpr u32 MSAA_samples = 0x04D1;
    constexpr u32 CullFaceEnable = 0x042B;
    constexpr u32 CullFaceEnableHw = 0x0646;
    constexpr u32 GlCullTestEnabled = CullFaceEnableHw;
    constexpr u32 FrontFace = 0x0430;
    constexpr u32 FrontFaceHw = 0x0647;
    constexpr u32 GlFrontFace = FrontFaceHw;
    constexpr u32 CullFace = 0x0431;
    constexpr u32 CullFaceHw = 0x0648;
    constexpr u32 GlCullFace = CullFaceHw;
    constexpr u32 D3DCullMode = 0x04C2;

    // Point size
    constexpr u32 PointSize = 0x0442;
    constexpr u32 PointSizeHw = 0x0546;
    constexpr u32 PointSpriteEnable = 0x0548;
    constexpr u32 PointCoordReplace = 0x0581;
    constexpr u32 AntiAliasPointEnable = 0x0596;

    // Vertex attribute format registers (real Maxwell: 0x0620..0x063F = attrib
    // format array, 8 attribs, each packs [offset:16][fmt:5][size:5][elems:3]...).
    constexpr u32 VertexAttribFormat0 = 0x0620;  // ..0x0627
    constexpr u32 VertexAttribFormatHw = 0x0458;
    constexpr u32 VertexAttribCount = 0x0628;    // number of active attribs
    // Draw topology register (real games set VertexGlTopo before draws).
    constexpr u32 VertexGlTopology = 0x0593;
    // Vertex buffer strides (0x0580..0x0587 per-vertex-stream stride words).
    constexpr u32 VertexStreamStride0 = 0x0580;
    // Instance/step mode for instanced draws.
    constexpr u32 VertexIDBase = 0x05E8;
    constexpr u32 VertexIDBaseHw = 0x0446;
    constexpr u32 InstanceCount = 0x05F6;      // used by DrawArraysInstanced
    constexpr u32 VertexArrayAddressHigh = 0x0587;
    constexpr u32 VertexArrayAddressLow = 0x0588;
    constexpr u32 VertexArrayStride = 0x0589;
    constexpr u32 IndexAddressHigh = 0x05F2;
    constexpr u32 IndexAddressLow = 0x05F3;
    constexpr u32 IndexFormat = 0x05F4;
    constexpr u32 IndexCount = 0x05F5;
    constexpr u32 DrawArrays = 0x0674;
    constexpr u32 DrawElements = 0x0675;
    // DrawTexture (blit-style draw; used by games for UI compositing).
    constexpr u32 DrawTexture = 0x0676;
    constexpr u32 TextureAddressHigh = 0x0585;
    constexpr u32 TextureAddressLow = 0x0586;
    constexpr u32 TextureFormat = 0x0589;
    constexpr u32 TextureWidth = 0x058A;
    constexpr u32 TextureHeight = 0x058B;
    constexpr u32 TexSampler = 0x0557;
    constexpr u32 TexHeader = 0x055D;
    constexpr u32 SamplerBinding = 0x048D;

    // --- Compute (Tier-A3): shader launch through the compute engine ---
    // Real Maxwell compute writes a LaunchDescription / queue-meta-descriptor
    // (QMD) region; we model the essential registers: block dims, program
    // (shader bytecode address + size), grid dims, and constants.
    constexpr u32 ComputeLaunchDesc = 0x0180;   // block dims + shared mem size
    constexpr u32 ComputeEntryAddressHigh = 0x0182;
    constexpr u32 ComputeEntryAddressLow = 0x0183;
    constexpr u32 ComputeProgramSize = 0x0188;  // compute shader bytecode size (-1)
    constexpr u32 ComputeGridDimX = 0x0189;     // grid/CTA raster X
    constexpr u32 ComputeGridDimY = 0x018A;     // grid Y
    constexpr u32 ComputeGridDimZ = 0x018B;     // grid Z
    constexpr u32 ComputeConstBufferHigh = 0x0184;
    constexpr u32 ComputeConstBufferLow = 0x0185;
    constexpr u32 ComputeConstBufferSize = 0x0186;
    constexpr u32 DispatchCompute = 0x0190;     // trigger method
    // Hardware Kepler/Maxwell compute launch (Tier-A3: method 0xAD launch_desc_loc, 0xAF launch)
    constexpr u32 ComputeLaunchDescLoc = 0x00AD;
    constexpr u32 ComputeLaunch = 0x00AF;

    // Guest pipeline shader program upload (Maxwell PIPE/LOAD_PROGRAM). Addresses
    // point at guest memory holding the Maxwell SASS bytecode for each stage.
    constexpr u32 VertexProgramAddressHigh = 0x0E01;
    constexpr u32 VertexProgramAddressLow = 0x0E02;
    constexpr u32 FragmentProgramAddressHigh = 0x0E03;
    constexpr u32 FragmentProgramAddressLow = 0x0E04;
    constexpr u32 ProgramEndOffset = 0x0E05;    // +1 = size in bytes of the program
    constexpr u32 ProgramClear = 0x0E06;        // force a fresh program upload
    constexpr u32 VertexProgramEndOffset = 0x0E07;   // +1 = VS size in bytes
    constexpr u32 FragmentProgramEndOffset = 0x0E08; // +1 = PS size in bytes
    constexpr u32 Pipelines = 0x0800;
    constexpr u32 ConstBuffer = 0x08E0;
    constexpr u32 BindGroups = 0x0900;
} // namespace MaxwellMethod

struct Maxwell3DRegisters {
    std::array<u32, 0x1000> regs{};

    float GetFloat(u32 method) const noexcept {
        const u32 val = regs[method & 0xFFF];
        float f = 0.0f;
        std::memcpy(&f, &val, sizeof(f));
        return f;
    }

    void SetFloat(u32 method, float f) noexcept {
        u32 val = 0;
        std::memcpy(&val, &f, sizeof(val));
        regs[method & 0xFFF] = val;
    }
};

class Maxwell3D {
public:
    explicit Maxwell3D(std::shared_ptr<IGpuBackend> backend);
    ~Maxwell3D() = default;

    /// Process a single Maxwell 3D command method + argument
    void ProcessMethod(u32 method, u32 argument);

    /// Submit a command stream pushbuffer with multi-mode decoding
    void SubmitPushbuffer(std::span<const u32> pushbuffer);

    /// Decompress an ASTC compressed texture into a contiguous RGBA8 surface
    static bool DecompressAstc(
        std::span<const u8> astc_data,
        u32 width,
        u32 height,
        u32 block_width,
        u32 block_height,
        std::vector<u32>& out_rgba8,
        bool is_srgb = false
    );

    [[nodiscard]] const Maxwell3DRegisters& GetRegisters() const noexcept { return regs_; }
    [[nodiscard]] std::shared_ptr<IGpuBackend> GetBackend() const noexcept { return backend_; }

    void SetMemory(memory::VirtualMemory* memory) noexcept { memory_ = memory; }
    [[nodiscard]] memory::VirtualMemory* GetMemory() const noexcept { return memory_; }

    /// GMMU + buffer cache accessors (Tier-A4/A5). The GMMU resolves guest GPU
    /// virtual addresses; the buffer cache streams vertex/index/uniform/storage
    /// uploads through it. Both are optional (null when unused).
    void SetGpuMemory(std::shared_ptr<GpuMemoryManager> gmmu);
    [[nodiscard]] GpuMemoryManager* GetGpuMemory() const noexcept { return gmmu_.get(); }
    [[nodiscard]] BufferCache* GetBufferCache() const noexcept { return buffer_cache_.get(); }
    [[nodiscard]] MaxwellMacroEngine& GetMacroEngine() noexcept { return macro_engine_; }
    [[nodiscard]] const MaxwellMacroEngine& GetMacroEngine() const noexcept { return macro_engine_; }

private:
    void ExecuteDrawArrays(u32 argument);
    void ExecuteDrawElements(u32 argument);
    void ExecuteDrawTexture(u32 argument);
    void ExecuteDispatchCompute(u32 argument);
    void ExecuteClearSurface(u32 argument);
    void ExecuteMacro(u32 slot, u32 argument);
    void EmitDebugGeometry(); // stage a recognizable test triangle for draws
    void EmitDebugIndexedGeometry(); // stage indexed test geometry
    void BindGuestShaders(); // upload guest VS/PS bytecode to the backend
    void BindGuestTextures(); // upload guest texture binding to the backend
    void BindGuestVertexAttributes(); // upload guest vertex layout + buffer
    void ApplyRasterizerState(); // push depth/stencil/blend/MSAA/cull state
    void BindGuestConstantBuffers(); // UBO upload via buffer cache
    void InitializeRegisterDefaults(); // yuzu-style boot defaults (GPL-3.0 port)

    std::shared_ptr<IGpuBackend> backend_;
    memory::VirtualMemory* memory_{nullptr};
    std::shared_ptr<GpuMemoryManager> gmmu_;
    std::shared_ptr<BufferCache> buffer_cache_;
    std::unique_ptr<EngineUpload> upload_;  // inline-to-memory uploader
    MaxwellMacroEngine macro_engine_{};
    Maxwell3DRegisters regs_{};
    bool programs_dirty_{false};
    bool textures_dirty_{false};
    bool raster_state_dirty_{false};
    bool cbuffs_dirty_{false};
    // Reusable scratch buffer staging guest-draw vertex data
    mutable common::ScratchBuffer<RasterVertex> geometry_scratch_;
    mutable common::ScratchBuffer<u32> index_scratch_;
};

} // namespace nemu::core::gpu
