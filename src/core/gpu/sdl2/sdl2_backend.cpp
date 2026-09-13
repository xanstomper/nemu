#include "sdl2_backend.hpp"
#include "platform/logger.hpp"
#include <cstring>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <algorithm>

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

    // Render everything in UI coordinates (1280x720); SDL scales to the actual
    // window size proportionally, so the layout survives window resizing.
    SDL_RenderSetLogicalSize(renderer_, static_cast<int>(width_), static_cast<int>(height_));

#ifdef NEMU_SDL2_UI
    // Crisp UI overlay: TrueType text + cover images.
    if (TTF_Init() == 0 && IMG_Init(IMG_INIT_JPG | IMG_INIT_PNG) != 0) {
        const std::string fp = FontPath();
        if (!fp.empty()) {
            ui_available_ = true;
        } else {
            NEMU_LOG_WARN("SDL2", "No UI font found; overlay text disabled");
        }
    }
#endif

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
#ifdef NEMU_SDL2_UI
    for (auto& [_, tex] : text_cache_) SDL_DestroyTexture(tex);
    for (auto& [_, tex] : cover_cache_) SDL_DestroyTexture(tex);
    text_cache_.clear();
    cover_cache_.clear();
    for (auto& [_, font] : fonts_) TTF_CloseFont(font);
    fonts_.clear();
    if (ui_available_) {
        IMG_Quit();
        TTF_Quit();
    }
    ui_available_ = false;
#endif
    ui_text_ops_.clear();
    ui_image_ops_.clear();
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
    FlushUiOverlay();
    SDL_RenderPresent(renderer_);

    ui_text_ops_.clear();
    ui_image_ops_.clear();

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


void Sdl2GpuBackend::UiTextOverlay(std::string_view text, float x, float y, float size_px,
                                   float r, float g, float b, float a, int align) {
    if (!ui_available_ || text.empty()) return;
    ui_text_ops_.push_back(UiTextOp{std::string(text), x, y, size_px, r, g, b, a, align});
}

void Sdl2GpuBackend::UiImageOverlay(std::string_view key, std::string_view host_path,
                                    float x, float y, float w, float h) {
    if (!ui_available_ || host_path.empty()) return;
    ui_image_ops_.push_back(UiImageOp{std::string(key), std::string(host_path), x, y, w, h});
}

void Sdl2GpuBackend::FlushUiOverlay() {
#ifdef NEMU_SDL2_UI
    if (!renderer_) return;

    // Covers first (under text), in queue order. Aspect is preserved with a
    // center crop ("cover" fit) so artwork fills the tile without distortion.
    for (const auto& op : ui_image_ops_) {
        SDL_Texture* tex = CoverTexture(op.host_path);
        if (!tex) continue;
        int iw = 0, ih = 0;
        SDL_QueryTexture(tex, nullptr, nullptr, &iw, &ih);
        SDL_Rect dst{static_cast<int>(op.x), static_cast<int>(op.y),
                     static_cast<int>(op.w), static_cast<int>(op.h)};
        if (iw > 0 && ih > 0) {
            const float src_ar = static_cast<float>(iw) / static_cast<float>(ih);
            const float dst_ar = op.w / op.h;
            SDL_Rect src{0, 0, iw, ih};
            if (src_ar > dst_ar) {          // source wider: crop left/right
                const int cw = static_cast<int>(ih * dst_ar);
                src.x = (iw - cw) / 2;
                src.w = cw;
            } else if (src_ar < dst_ar) {   // source taller: crop top/bottom
                const int ch = static_cast<int>(iw / dst_ar);
                src.y = (ih - ch) / 2;
                src.h = ch;
            }
            SDL_RenderCopy(renderer_, tex, &src, &dst);
        } else {
            SDL_RenderCopy(renderer_, tex, nullptr, &dst);
        }
    }

    // Text on top.
    for (const auto& op : ui_text_ops_) {
        SDL_Texture* tex = TextTexture(op);
        if (!tex) continue;
        int tw = 0, th = 0;
        SDL_QueryTexture(tex, nullptr, nullptr, &tw, &th);
        float dx = op.x;
        if (op.align == 0) dx = op.x - tw * 0.5f;
        else if (op.align == 1) dx = op.x - static_cast<float>(tw);
        SDL_Rect dst{static_cast<int>(dx), static_cast<int>(op.y), tw, th};
        SDL_SetTextureAlphaMod(tex, static_cast<Uint8>(std::clamp(op.a, 0.0f, 1.0f) * 255.0f));
        SDL_RenderCopy(renderer_, tex, nullptr, &dst);
    }
#endif
}

#ifdef NEMU_SDL2_UI
std::string Sdl2GpuBackend::FontPath() {
    if (const char* env = std::getenv("NEMU_FONT_PATH")) {
        if (std::filesystem::exists(env)) return env;
    }
    static const char* kCandidates[] = {
        "/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf",
        "/usr/share/fonts/truetype/noto/NotoSans-Regular.ttf",
        "/usr/share/fonts/opentype/noto/NotoSans-Regular.ttf",
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
        "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf",
        "/usr/share/fonts/truetype/freefont/FreeSans.ttf",
        "/usr/share/fonts/TTF/DejaVuSans.ttf",
    };
    for (const char* p : kCandidates) {
        if (std::filesystem::exists(p)) return p;
    }
    return {};
}

TTF_Font* Sdl2GpuBackend::FontForSize(float size_px) {
    const int pts = std::clamp(static_cast<int>(size_px), 8, 96);
    auto it = fonts_.find(pts);
    if (it != fonts_.end()) return it->second;
    const std::string fp = FontPath();
    if (fp.empty()) return nullptr;
    TTF_Font* font = TTF_OpenFont(fp.c_str(), pts);
    if (font) fonts_[pts] = font;
    return font;
}

SDL_Texture* Sdl2GpuBackend::TextTexture(const UiTextOp& op) {
    // Cache key: text | size | color (alpha excluded; applied per frame).
    const int cr = static_cast<int>(op.r * 255.0f);
    const int cg = static_cast<int>(op.g * 255.0f);
    const int cb = static_cast<int>(op.b * 255.0f);
    const std::string cache_key = op.text + "|" + std::to_string(static_cast<int>(op.size)) +
                                  "|" + std::to_string(cr) + "," + std::to_string(cg) + "," + std::to_string(cb);
    auto it = text_cache_.find(cache_key);
    if (it != text_cache_.end()) return it->second;

    TTF_Font* font = FontForSize(op.size);
    if (!font) return nullptr;
    SDL_Color c{static_cast<Uint8>(cr), static_cast<Uint8>(cg), static_cast<Uint8>(cb), 255};
    SDL_Surface* surf = TTF_RenderUTF8_Blended(font, op.text.c_str(), c);
    if (!surf) return nullptr;
    SDL_Texture* tex = SDL_CreateTextureFromSurface(renderer_, surf);
    SDL_FreeSurface(surf);
    if (tex) {
        SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);
        text_cache_[cache_key] = tex;
    }
    return tex;
}

SDL_Texture* Sdl2GpuBackend::CoverTexture(const std::string& host_path) {
    auto it = cover_cache_.find(host_path);
    if (it != cover_cache_.end()) return it->second;  // nullptr values are cached misses
    SDL_Texture* tex = nullptr;
    SDL_Surface* surf = IMG_Load(host_path.c_str());
    if (surf) {
        tex = SDL_CreateTextureFromSurface(renderer_, surf);
        SDL_FreeSurface(surf);
        if (tex) SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);
    }
    cover_cache_[host_path] = tex;  // cache negative result too
    return tex;
}
#endif // NEMU_SDL2_UI
} // namespace nemu::core::gpu::sdl2

#endif // NEMU_SDL2
