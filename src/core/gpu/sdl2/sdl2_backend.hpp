#pragma once

#include "core/gpu/gpu_interface.hpp"
#include "core/gpu/null_backend.hpp"

// SDL2 is available on Linux (and cross-platform). Guarded by a macro so the
// file only compiles where the library is present.
#ifdef NEMU_SDL2

#include <SDL.h>
#ifdef NEMU_SDL2_UI
#include <SDL_ttf.h>
#include <SDL_image.h>
#endif
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace nemu::core::gpu::sdl2 {

/// One queued text overlay op (UI pixel space, y = top edge of the line).
struct UiTextOp {
    std::string text;
    float x, y, size, r, g, b, a;
    int align;
};

/// One queued cover-image op.
struct UiImageOp {
    std::string key;
    std::string host_path;
    float x, y, w, h;
};

/// Windowed GPU backend for desktop Linux (and any platform with SDL2). It
/// reuses the software rasterizer (NullGpuBackend) for all Draw/clear work and
/// additionally presents the resulting RGBA8 framebuffer into a real SDL2
/// window, so the full emulator UI and guest frames are visible on a monitor —
/// the same layout the app presents on Xbox (via the swap chain).
///
/// This lets Nemu run with its actual frontend UI on a Linux dev PC, not just
/// headless, so behavior can be eyeballed before sideloading to Xbox.
class Sdl2GpuBackend final : public IGpuBackend {
public:
    Sdl2GpuBackend();
    ~Sdl2GpuBackend() override;

    Sdl2GpuBackend(const Sdl2GpuBackend&) = delete;
    Sdl2GpuBackend& operator=(const Sdl2GpuBackend&) = delete;

    bool Initialize(u32 render_width, u32 render_height) override;
    void Shutdown() override;

    void BeginFrame() override;
    void EndFrame() override;
    void Present() override;

    void SetViewport(const Viewport& viewport) override;
    void SetScissor(const ScissorRect& scissor) override;
    void ClearRenderTarget(const ClearColor& color) override;
    void ClearDepthStencil(float depth, u8 stencil) override;

    void DrawArrays(PrimitiveTopology topology, u32 first_vertex, u32 vertex_count) override;
    void DrawIndexed(PrimitiveTopology topology, u32 index_count, u32 first_index, u32 base_vertex) override;

    void SetRasterVertices(std::span<const RasterVertex> vertices) override;
    void SetRasterIndices(std::span<const u32> indices) override;
    bool DumpFramePPM(const char* path) override;

    [[nodiscard]] GpuStats GetStats() const noexcept override;
    [[nodiscard]] std::string_view GetBackendName() const noexcept override;

    // Crisp UI overlay (TTF text + cover images), composited at present time.
    [[nodiscard]] bool SupportsUiOverlay() const noexcept override { return ui_available_; }
    void UiTextOverlay(std::string_view text, float x, float y, float size_px,
                       float r, float g, float b, float a, int align) override;
    void UiImageOverlay(std::string_view key, std::string_view host_path,
                        float x, float y, float w, float h) override;

    /// Raw host RGBA8 framebuffer (delegates to the software rasterizer).
    [[nodiscard]] const u8* Framebuffer() const noexcept;
    [[nodiscard]] size_t FramebufferSize() const noexcept;

    /// Drain accumulated window events (e.g. close button polled by the main
    /// loop). Returns false when the user requested to close the window.
    bool PumpEvents();

private:
    void RecreateTexture();
    void FlushUiOverlay();

#ifdef NEMU_SDL2_UI
    TTF_Font* FontForSize(float size_px);
    SDL_Texture* TextTexture(const UiTextOp& op);
    SDL_Texture* CoverTexture(const std::string& host_path);
    static std::string FontPath();
#endif

    std::unique_ptr<NullGpuBackend> raster_;
    SDL_Window* window_{nullptr};
    SDL_Renderer* renderer_{nullptr};
    SDL_Texture* texture_{nullptr};
    u32 width_{0};
    u32 height_{0};
    bool initialized_{false};
    GpuStats stats_{};

    // Queued overlay ops for the current frame (flushed by Present).
    std::vector<UiTextOp> ui_text_ops_;
    std::vector<UiImageOp> ui_image_ops_;
    bool ui_available_{false};
#ifdef NEMU_SDL2_UI
    std::unordered_map<int, TTF_Font*> fonts_;
    std::unordered_map<std::string, SDL_Texture*> text_cache_;
    std::unordered_map<std::string, SDL_Texture*> cover_cache_;
#endif
};

} // namespace nemu::core::gpu::sdl2

#endif // NEMU_SDL2