#include "null_backend.hpp"
#include "platform/logger.hpp"

namespace nemu::core::gpu {

NullGpuBackend::NullGpuBackend() = default;
NullGpuBackend::~NullGpuBackend() {
    Shutdown();
}

bool NullGpuBackend::Initialize(u32 render_width, u32 render_height) {
    current_viewport_ = Viewport{
        .x = 0.0f,
        .y = 0.0f,
        .width = static_cast<float>(render_width),
        .height = static_cast<float>(render_height),
        .min_depth = 0.0f,
        .max_depth = 1.0f
    };
    current_scissor_ = ScissorRect{
        .left = 0,
        .top = 0,
        .right = render_width,
        .bottom = render_height
    };
    initialized_ = true;
    NEMU_LOG_INFO("GPU", "Initialized Null GPU backend ({}x{})", render_width, render_height);
    return true;
}

void NullGpuBackend::Shutdown() {
    if (initialized_) {
        NEMU_LOG_INFO("GPU", "Shutdown Null GPU backend");
        initialized_ = false;
    }
}

void NullGpuBackend::BeginFrame() {
    in_frame_ = true;
}

void NullGpuBackend::EndFrame() {
    in_frame_ = false;
}

void NullGpuBackend::Present() {
    stats_.frames_presented++;
    NEMU_LOG_DEBUG("GPU", "Present frame #{}", stats_.frames_presented);
}

void NullGpuBackend::SetViewport(const Viewport& viewport) {
    current_viewport_ = viewport;
}

void NullGpuBackend::SetScissor(const ScissorRect& scissor) {
    current_scissor_ = scissor;
}

void NullGpuBackend::ClearRenderTarget(const ClearColor& color) {
    last_clear_color_ = color;
    NEMU_LOG_DEBUG("GPU", "Clear render target: ({:.2f}, {:.2f}, {:.2f}, {:.2f})", color.r, color.g, color.b, color.a);
}

void NullGpuBackend::ClearDepthStencil([[maybe_unused]] float depth, [[maybe_unused]] u8 stencil) {
    NEMU_LOG_DEBUG("GPU", "Clear depth ({:.2f}) stencil ({})", depth, stencil);
}

void NullGpuBackend::DrawArrays([[maybe_unused]] PrimitiveTopology topology, [[maybe_unused]] u32 first_vertex, u32 vertex_count) {
    stats_.draw_calls++;
    stats_.vertices_submitted += vertex_count;
    NEMU_LOG_DEBUG("GPU", "DrawArrays: {} vertices", vertex_count);
}

void NullGpuBackend::DrawIndexed([[maybe_unused]] PrimitiveTopology topology, u32 index_count, [[maybe_unused]] u32 first_index, [[maybe_unused]] u32 base_vertex) {
    stats_.draw_calls++;
    stats_.vertices_submitted += index_count;
    NEMU_LOG_DEBUG("GPU", "DrawIndexed: {} indices", index_count);
}

} // namespace nemu::core::gpu
