#pragma once

#include "core/types.hpp"
#include <span>
#include <vector>
#include <cstdint>
#include <string_view>

namespace nemu::core::gpu::pipeline {

enum class UpscalerMode : u32 {
    Nearest = 0,
    Bilinear = 1,
    Bicubic = 2,
    FSR_1_0 = 3,
    FSR_2_0 = 4,
};

enum class AntiAliasingMode : u32 {
    None = 0,
    FXAA = 1,
    SMAA = 2,
    MSAA_2x = 3,
    MSAA_4x = 4,
    MSAA_8x = 5,
};

enum class FrameGenMode : u32 {
    Disabled = 0,
    AFMF_Extrapolation_2x = 1, // AMD Fluid Motion Frames / 2x Frame Multiplier
};

struct OptimizerStats {
    u64 frames_upscaled{0};
    u64 frames_generated{0};
    u64 msaa_resolves{0};
    u64 fxaa_passes{0};
};

class GraphicsOptimizer {
public:
    GraphicsOptimizer() = default;
    ~GraphicsOptimizer() = default;

    /// Apply spatial upscaling (FSR / Bicubic / Bilinear) from source resolution to target
    static bool ApplyUpscale(
        std::span<const u32> src_rgba,
        u32 src_w,
        u32 src_h,
        std::span<u32> dst_rgba,
        u32 dst_w,
        u32 dst_h,
        UpscalerMode mode,
        float sharpness = 0.8f
    );

    /// Apply post-process anti-aliasing (FXAA / SMAA) to a color buffer
    static bool ApplyAntiAliasing(
        std::span<u32> inout_rgba,
        u32 width,
        u32 height,
        AntiAliasingMode mode
    );

    /// Multi-Sample Anti-Aliasing (MSAA) Resolve filter
    static bool ApplyMsaaResolve(
        std::span<const u32> multi_sample_buffer,
        std::span<u32> out_resolved_rgba,
        u32 width,
        u32 height,
        u32 sample_count
    );

    /// Generate an intermediate interpolated frame using temporal motion estimation (2x Frame Generation)
    static bool GenerateIntermediateFrame(
        std::span<const u32> prev_frame,
        std::span<const u32> curr_frame,
        std::span<u32> out_frame,
        u32 width,
        u32 height
    );

    [[nodiscard]] OptimizerStats GetStats() const noexcept { return stats_; }

    static constexpr std::string_view GetUpscalerName(UpscalerMode mode) noexcept {
        switch (mode) {
            case UpscalerMode::Nearest:  return "Nearest Neighbor";
            case UpscalerMode::Bilinear: return "Bilinear Filtering";
            case UpscalerMode::Bicubic:  return "Bicubic Catmull-Rom";
            case UpscalerMode::FSR_1_0:  return "AMD FidelityFX Super Resolution 1.0 (EASU+RCAS)";
            case UpscalerMode::FSR_2_0:  return "AMD FidelityFX Super Resolution 2.0 (Temporal)";
            default:                     return "Bilinear";
        }
    }

    static constexpr std::string_view GetAntiAliasingName(AntiAliasingMode mode) noexcept {
        switch (mode) {
            case AntiAliasingMode::None:     return "Off";
            case AntiAliasingMode::FXAA:     return "FXAA (Fast Approximate)";
            case AntiAliasingMode::SMAA:     return "SMAA (Subpixel Morphological)";
            case AntiAliasingMode::MSAA_2x:  return "2x MSAA (Multi-Sample)";
            case AntiAliasingMode::MSAA_4x:  return "4x MSAA (Multi-Sample)";
            case AntiAliasingMode::MSAA_8x:  return "8x MSAA (Ultra Multi-Sample)";
            default:                         return "Off";
        }
    }

    static constexpr std::string_view GetFrameGenName(FrameGenMode mode) noexcept {
        switch (mode) {
            case FrameGenMode::Disabled:                return "Off (Native Refresh)";
            case FrameGenMode::AFMF_Extrapolation_2x:   return "AFMF 2x Frame Multiplier (30->60 / 60->120 FPS)";
            default:                                    return "Off";
        }
    }

private:
    OptimizerStats stats_{};
};

} // namespace nemu::core::gpu::pipeline
