#include "sdl2_backend.hpp"
#include <cstring>

#ifdef NEMU_SDL2

namespace nemu::core::gpu::sdl2 {

Sdl2GpuBackend::Sdl2GpuBackend() = default;

Sdl2GpuBackend::~Sdl2GpuBackend() {
    Shutdown();
}

bool Sdl2GpuBackend::Initialize(u32 render_width, u32 render_height) {
    width_ = render_width;
    height_ = render_height;

    if (SDL_InitSubSystem(SDL_INIT_VIDEO) != 0) {
        return false;
    }

    window_ = SDL_CreateWindow(
        "Nemu - Nintendo Switch Emulator for Xbox Series S/X",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        static_cast<int>(width_), static_cast<int>(height_),
        SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
    if (!window_) {
        return false;
    }

    renderer_ = SDL_CreateRenderer(window_, -1,
        SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!renderer_) {
        // Fall back to software renderer if hardware acceleration is unavailable.
        renderer_ = SDL_CreateRenderer(window_, -1, SDL_RENDERER_SOFTWARE);
        if (!renderer_) {
            SDL_DestroyWindow(window_);
            window_ = nullptr;
            return false;
        }
    }

    // Internal software rasterizer does all the actual Draw/clear work.
    raster_ = std::make_unique<NullGpuBackend>();
    if (!raster_->Initialize(render_width, render_height)) {
        SDL_DestroyRenderer(renderer_);
        SDL_DestroyWindow(window_);
        renderer_ = nullptr;
        window_ = nullptr;
        return false;
    }

    RecreateTexture();
    initialized_ = true;
    return true;
}

void Sdl2GpuBackend::RecreateTexture() {
    if (texture_) SDL_DestroyTexture(texture_);
    texture_ = nullptr;
    if (renderer_ && width_ > 0 && height_ > 0) {
        texture_ = SDL_CreateTexture(renderer_, SDL_PIXELFORMAT_RGBA32,
                                     SDL_TEXTUREACCESS_STREAMING,
                                     static_cast<int>(width_), static_cast<int>(height_));
    }
}

void Sdl2GpuBackend::Shutdown() {
    if (raster_) raster_->Shutdown();
    if (texture_) SDL_DestroyTexture(texture_);
    if (renderer_) SDL_DestroyRenderer(renderer_);
    if (window_) SDL_DestroyWindow(window_);
    texture_ = nullptr;
    renderer_ = nullptr;
    window_ = nullptr;
    raster_.reset();
    if (initialized_) SDL_QuitSubSystem(SDL_INIT_VIDEO);
    initialized_ = false;
}

void Sdl2GpuBackend::BeginFrame() { if (raster_) raster_->BeginFrame(); }
void Sdl2GpuBackend::EndFrame()   { if (raster_) raster_->EndFrame(); }

void Sdl2GpuBackend::Present() {
    if (!raster_ || !renderer_ || !texture_ || !initialized_) return;

    // Upload the software-rasterized RGBA8 framebuffer into the SDL texture.
    if (texture_) {
        void* pixels = nullptr;
        int pitch = 0;
        if (SDL_LockTexture(texture_, nullptr, &pixels, &pitch) == 0) {
            const u8* fb = raster_->Framebuffer();
            const size_t row = width_ * 4;
            for (u32 y = 0; y < height_; ++y) {
                std::memcpy(static_cast<u8*>(pixels) + (size_t)y * pitch,
                            fb + (size_t)y * row, row);
            }
            SDL_UnlockTexture(texture_);
        }
    }

    SDL_RenderClear(renderer_);
    SDL_RenderCopy(renderer_, texture_, nullptr, nullptr);
    SDL_RenderPresent(renderer_);

    stats_.frames_presented++;
}

void Sdl2GpuBackend::SetViewport(const Viewport& vp) { if (raster_) raster_->SetViewport(vp); }
void Sdl2GpuBackend::SetScissor(const ScissorRect& sc) { if (raster_) raster_->SetScissor(sc); }
void Sdl2GpuBackend::ClearRenderTarget(const ClearColor& c) { if (raster_) raster_->ClearRenderTarget(c); }
void Sdl2GpuBackend::ClearDepthStencil(float d, u8 s) { if (raster_) raster_->ClearDepthStencil(d, s); }
void Sdl2GpuBackend::DrawArrays(PrimitiveTopology t, u32 fv, u32 vc) { if (raster_) raster_->DrawArrays(t, fv, vc); }
void Sdl2GpuBackend::DrawIndexed(PrimitiveTopology t, u32 ic, u32 fi, u32 bv) { if (raster_) raster_->DrawIndexed(t, ic, fi, bv); }
void Sdl2GpuBackend::SetRasterVertices(std::span<const RasterVertex> v) { if (raster_) raster_->SetRasterVertices(v); }
void Sdl2GpuBackend::SetRasterIndices(std::span<const u32> i) { if (raster_) raster_->SetRasterIndices(i); }
bool Sdl2GpuBackend::DumpFramePPM(const char* p) { return raster_ ? raster_->DumpFramePPM(p) : false; }

GpuStats Sdl2GpuBackend::GetStats() const noexcept {
    if (raster_) return raster_->GetStats();
    return stats_;
}

std::string_view Sdl2GpuBackend::GetBackendName() const noexcept {
    return "SDL2 / Software Rasterizer (desktop window)";
}

const u8* Sdl2GpuBackend::Framebuffer() const noexcept {
    return raster_ ? raster_->Framebuffer() : nullptr;
}

size_t Sdl2GpuBackend::FramebufferSize() const noexcept {
    return raster_ ? raster_->FramebufferSize() : 0;
}

bool Sdl2GpuBackend::PumpEvents() {
    if (!initialized_) return true;
    SDL_Event ev;
    bool keep_open = true;
    while (SDL_PollEvent(&ev)) {
        if (ev.type == SDL_QUIT) {
            keep_open = false;
        }
    }
    return keep_open;
}

} // namespace nemu::core::gpu::sdl2

#endif // NEMU_SDL2