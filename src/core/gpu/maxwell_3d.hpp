#pragma once

#include "core/types.hpp"
#include "gpu_interface.hpp"
#include <span>
#include <array>
#include <memory>
#include <cstring>


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
    constexpr u32 VertexArrayAddressHigh = 0x0587;
    constexpr u32 VertexArrayAddressLow = 0x0588;
    constexpr u32 DrawArrays = 0x0674;
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

    /// Submit a command stream pushbuffer
    /// Command format: header (method + count) followed by arguments
    void SubmitPushbuffer(std::span<const u32> pushbuffer);

    [[nodiscard]] const Maxwell3DRegisters& GetRegisters() const noexcept { return regs_; }
    [[nodiscard]] std::shared_ptr<IGpuBackend> GetBackend() const noexcept { return backend_; }

private:
    void ExecuteDrawArrays(u32 argument);
    void ExecuteClearSurface(u32 argument);

    std::shared_ptr<IGpuBackend> backend_;
    Maxwell3DRegisters regs_{};
};

} // namespace nemu::core::gpu
