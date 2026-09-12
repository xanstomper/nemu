#pragma once

#include "core/types.hpp"
#include "core/common/scratch_buffer.hpp"
#include "gpu_interface.hpp"
#include <span>
#include <array>
#include <memory>
#include <cstring>


namespace nemu::core::memory {
class VirtualMemory;
}

namespace nemu::core::gpu {

namespace MaxwellMethod {
    constexpr u32 Nop = 0x0200;
    constexpr u32 WaitForIdle = 0x0045;
    constexpr u32 ViewportScaleX = 0x035D;
    constexpr u32 ViewportScaleY = 0x035E;
    constexpr u32 ViewportOffsetX = 0x0360;
    constexpr u32 ViewportOffsetY = 0x0361;
    constexpr u32 ClearColorR = 0x0368;
    constexpr u32 ClearColorG = 0x0369;
    constexpr u32 ClearColorB = 0x036A;
    constexpr u32 ClearColorA = 0x036B;
    constexpr u32 ClearSurface = 0x036C;
    constexpr u32 ClearDepth = 0x036D;
    constexpr u32 ScissorEnable = 0x0380;
    constexpr u32 ScissorX = 0x0381;
    constexpr u32 ScissorY = 0x0382;
    constexpr u32 ScissorWidth = 0x0383;
    constexpr u32 ScissorHeight = 0x0384;
    constexpr u32 VertexArrayAddressHigh = 0x0587;
    constexpr u32 VertexArrayAddressLow = 0x0588;
    constexpr u32 IndexAddressHigh = 0x05F2;
    constexpr u32 IndexAddressLow = 0x05F3;
    constexpr u32 IndexFormat = 0x05F4;
    constexpr u32 IndexCount = 0x05F5;
    constexpr u32 DrawArrays = 0x0674;
    constexpr u32 DrawElements = 0x0675;
    constexpr u32 TextureAddressHigh = 0x0585;
    constexpr u32 TextureAddressLow = 0x0586;
    constexpr u32 TextureFormat = 0x0589;
    constexpr u32 TextureWidth = 0x058A;
    constexpr u32 TextureHeight = 0x058B;
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

private:
    void ExecuteDrawArrays(u32 argument);
    void ExecuteDrawElements(u32 argument);
    void ExecuteClearSurface(u32 argument);
    void EmitDebugGeometry(); // stage a recognizable test triangle for draws
    void EmitDebugIndexedGeometry(); // stage indexed test geometry

    std::shared_ptr<IGpuBackend> backend_;
    memory::VirtualMemory* memory_{nullptr};
    Maxwell3DRegisters regs_{};
    // Reusable scratch buffer staging guest-draw vertex data
    mutable common::ScratchBuffer<RasterVertex> geometry_scratch_;
    mutable common::ScratchBuffer<u32> index_scratch_;
};

} // namespace nemu::core::gpu
