#pragma once

#include "core/types.hpp"
#include "pipeline_cache.hpp"
#include "core/gpu/shader/shader_translator.hpp"
#include "core/gpu/shader/hlsl_validator.hpp"
#include <optional>
#include <string>
#include <vector>
#include <span>
#include <memory>

namespace nemu::core::gpu::pipeline {

/// Result of driving a guest Maxwell draw through the full translation chain:
/// decode -> HLSL emit -> validate -> pipeline cache.
///
/// This is the headless-testable, platform-agnostic front-end that the D3D12
/// backend calls for a guest draw. On Linux it validates the emitted HLSL and
/// simulates the cache (no real GPU); on Windows/Xbox it additionally runs
/// D3DCompile and CreateGraphicsPipelineState through PipelineCache.
struct PipelineBuildResult {
    bool decoder_ok{false};
    bool hlsl_valid{false};
    bool budget_ok{false};
    bool cached{false};
    std::string error{};
    std::string vs_hlsl{};
    std::string ps_hlsl{};

    [[nodiscard]] bool Passed() const noexcept {
        return decoder_ok && hlsl_valid && budget_ok;
    }
};

/// Full Maxwell -> HLSL -> PipelineCache build path for one draw.
class PipelineBridge {
public:
    /// @param vs_bytecode Maxwell vertex program bytes (may be empty).
    /// @param ps_bytecode Maxwell fragment program bytes (may be empty).
    /// @param key        Raster/blend/depth/topology + resource usage state.
    /// @param cache      Target pipeline cache (nullable for pure validation).
    static PipelineBuildResult Build(
        std::span<const u8> vs_bytecode,
        std::span<const u8> ps_bytecode,
        const PipelineStateKey& key,
        PipelineCache* cache = nullptr
    );
};

} // namespace nemu::core::gpu::pipeline