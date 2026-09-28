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

    // Set hints before video subsystem init and window/renderer creation for maximum quality
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "linear");
    SDL_SetHint(SDL_HINT_RENDER_LINE_METHOD, "3");
    SDL_SetHint(SDL_HINT_VIDEO_X11_NET_WM_BYPASS_COMPOSITOR, "0");

    if (SDL_InitSubSystem(SDL_INIT_VIDEO) != 0) {
        return false;
    }

    window_ = SDL_CreateWindow(
        "NEMULATOR - Universal Nintendo Switch Emulator (Xbox UWP & Desktop)",
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

    // Explicitly synchronize swap interval to vertical blank (60 Hz VSync)
    SDL_GL_SetSwapInterval(1);

    // Render everything in UI coordinates (1280x720); SDL scales to the actual
    // window size proportionally and centers with letterboxing/pillarboxing.
    SDL_RenderSetLogicalSize(renderer_, 1280, 720);

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
    ui_ops_.clear();
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

void Sdl2GpuBackend::BeginFrame() {
    if (raster_) raster_->BeginFrame();
}
void Sdl2GpuBackend::EndFrame()   { if (raster_) raster_->EndFrame(); }

void Sdl2GpuBackend::Present() {
    if (!initialized_) return;

    // The software rasterizer always produced a (possibly headless) frame, so
    // count it regardless of whether a window renderer exists. On a headless
    // host SDL_CreateRenderer fails and renderer_ is null; we still must not
    // drop the frame accounting so the emulator render path is observable.
    if (renderer_) {
        FlushUiOverlay();

#ifdef NEMU_SDL2_UI
        if (const char* dump = std::getenv("NEMU_SCREENSHOT_PATH")) {
            static bool dumped = false;
            if (!dumped && stats_.frames_presented >= 30) {
                dumped = true;
                SDL_Surface* sshot = SDL_CreateRGBSurfaceWithFormat(0, static_cast<int>(width_), static_cast<int>(height_), 32, SDL_PIXELFORMAT_RGBA32);
                if (sshot) {
                    SDL_RenderReadPixels(renderer_, nullptr, SDL_PIXELFORMAT_RGBA32, sshot->pixels, sshot->pitch);
                    IMG_SavePNG(sshot, dump);
                    SDL_FreeSurface(sshot);
                }
            }
        }
#endif

        SDL_RenderPresent(renderer_);

        ui_ops_.clear();
    }

    stats_.frames_presented++;
}

void Sdl2GpuBackend::SetViewport(const Viewport& vp) { if (raster_) raster_->SetViewport(vp); }
void Sdl2GpuBackend::SetScissor(const ScissorRect& sc) { if (raster_) raster_->SetScissor(sc); }
void Sdl2GpuBackend::ClearRenderTarget(const ClearColor& c) {
    if (renderer_) {
        SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(renderer_,
            static_cast<Uint8>(std::clamp(c.r, 0.0f, 1.0f) * 255.0f),
            static_cast<Uint8>(std::clamp(c.g, 0.0f, 1.0f) * 255.0f),
            static_cast<Uint8>(std::clamp(c.b, 0.0f, 1.0f) * 255.0f),
            static_cast<Uint8>(std::clamp(c.a, 0.0f, 1.0f) * 255.0f));
        SDL_RenderClear(renderer_);
    }
    if (raster_) raster_->ClearRenderTarget(c);
}
void Sdl2GpuBackend::ClearDepthStencil(float d, u8 s) { if (raster_) raster_->ClearDepthStencil(d, s); }
void Sdl2GpuBackend::DrawArrays(PrimitiveTopology t, u32 fv, u32 vc) {
    stats_.draw_calls++;
    stats_.vertices_submitted += vc;
    if (renderer_ && t == PrimitiveTopology::Triangles && !vertices_readonly_.empty()) {
        std::vector<SDL_Vertex> sdl_verts;
        sdl_verts.reserve(vc);
        // Match logical rendering size (1280x720) configured via SDL_RenderSetLogicalSize
        const float fw = 1280.0f;
        const float fh = 720.0f;
        const u32 end = std::min(fv + vc, static_cast<u32>(vertices_readonly_.size()));
        for (u32 i = fv; i < end; ++i) {
            const auto& rv = vertices_readonly_[i];
            SDL_Vertex sv;
            sv.position.x = (rv.x * 0.5f + 0.5f) * fw;
            sv.position.y = (0.5f - rv.y * 0.5f) * fh;
            sv.color.r = static_cast<Uint8>(std::clamp(rv.r, 0.0f, 1.0f) * 255.0f);
            sv.color.g = static_cast<Uint8>(std::clamp(rv.g, 0.0f, 1.0f) * 255.0f);
            sv.color.b = static_cast<Uint8>(std::clamp(rv.b, 0.0f, 1.0f) * 255.0f);
            sv.color.a = static_cast<Uint8>(std::clamp(rv.a, 0.0f, 1.0f) * 255.0f);
            sv.tex_coord.x = 0.0f;
            sv.tex_coord.y = 0.0f;
            sdl_verts.push_back(sv);
        }
        if (!sdl_verts.empty()) {
            SDL_RenderGeometry(renderer_, nullptr, sdl_verts.data(), static_cast<int>(sdl_verts.size()), nullptr, 0);
        }
    }
}
void Sdl2GpuBackend::DrawIndexed(PrimitiveTopology t, u32 ic, u32 fi, u32 bv) { if (raster_) raster_->DrawIndexed(t, ic, fi, bv); }
void Sdl2GpuBackend::SetRasterVertices(std::span<const RasterVertex> v) {
    vertices_readonly_ = v;
    if (raster_) raster_->SetRasterVertices(v);
}
void Sdl2GpuBackend::SetRasterIndices(std::span<const u32> i) { if (raster_) raster_->SetRasterIndices(i); }
bool Sdl2GpuBackend::DumpFramePPM(const char* p) { return raster_ ? raster_->DumpFramePPM(p) : false; }

GpuStats Sdl2GpuBackend::GetStats() const noexcept {
    // Report the backend's own aggregate accounting (draw calls, vertices, and
    // frames presented through this window). The internal software rasterizer
    // forwards draws into stats_ via DrawArrays/DrawIndexed, and Present()
    // increments frames_presented regardless of window availability, so this is
    // the single honest picture of what the SDL2 backend actually produced.
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

bool Sdl2GpuBackend::HasVsync() const noexcept {
    if (!renderer_) return false;
    SDL_RendererInfo info{};
    if (SDL_GetRendererInfo(renderer_, &info) == 0) {
        return (info.flags & SDL_RENDERER_PRESENTVSYNC) != 0;
    }
    return false;
}

bool Sdl2GpuBackend::PumpEvents() {
    if (!initialized_) return true;
    SDL_Event ev;
    bool keep_open = true;
    while (SDL_PollEvent(&ev)) {
        if (ev.type == SDL_QUIT) {
            keep_open = false;
        } else if (ev.type == SDL_KEYDOWN) {
            // F11 or Alt+Enter toggles fullscreen
            if (ev.key.keysym.sym == SDLK_F11 ||
                ((ev.key.keysym.mod & KMOD_ALT) && (ev.key.keysym.sym == SDLK_RETURN || ev.key.keysym.sym == SDLK_KP_ENTER))) {
                if (window_) {
                    Uint32 flags = SDL_GetWindowFlags(window_);
                    bool is_fs = (flags & SDL_WINDOW_FULLSCREEN_DESKTOP) != 0;
                    SDL_SetWindowFullscreen(window_, is_fs ? 0 : SDL_WINDOW_FULLSCREEN_DESKTOP);
                }
            }
        } else if (ev.type == SDL_MOUSEMOTION) {
            float lx = static_cast<float>(ev.motion.x);
            float ly = static_cast<float>(ev.motion.y);
            if (renderer_) {
                SDL_RenderWindowToLogical(renderer_, ev.motion.x, ev.motion.y, &lx, &ly);
            }
            pointer_state_.x = lx;
            pointer_state_.y = ly;
        } else if (ev.type == SDL_MOUSEBUTTONDOWN) {
            float lx = static_cast<float>(ev.button.x);
            float ly = static_cast<float>(ev.button.y);
            if (renderer_) {
                SDL_RenderWindowToLogical(renderer_, ev.button.x, ev.button.y, &lx, &ly);
            }
            pointer_state_.x = lx;
            pointer_state_.y = ly;
            if (ev.button.button == SDL_BUTTON_LEFT) {
                pointer_state_.left_down = true;
                pointer_state_.left_clicked = true;
            } else if (ev.button.button == SDL_BUTTON_RIGHT) {
                pointer_state_.right_clicked = true;
            }
        } else if (ev.type == SDL_MOUSEBUTTONUP) {
            if (ev.button.button == SDL_BUTTON_LEFT) {
                pointer_state_.left_down = false;
            }
        } else if (ev.type == SDL_MOUSEWHEEL) {
            pointer_state_.wheel_delta += static_cast<float>(ev.wheel.y);
        }
    }
    return keep_open;
}

Sdl2GpuBackend::PointerEventState Sdl2GpuBackend::ConsumePointerState() {
    PointerEventState state = pointer_state_;
    pointer_state_.left_clicked = false;
    pointer_state_.right_clicked = false;
    pointer_state_.wheel_delta = 0.0f;
    return state;
}


void Sdl2GpuBackend::UiTextOverlay(std::string_view text, float x, float y, float size_px,
                                   float r, float g, float b, float a, int align) {
    if (!ui_available_ || text.empty()) return;
    UiOp op{};
    op.type = UiOp::Type::Text;
    op.text = UiTextOp{std::string(text), x, y, size_px, r, g, b, a, align};
    ui_ops_.push_back(std::move(op));
}

void Sdl2GpuBackend::UiImageOverlay(std::string_view key, std::string_view host_path,
                                    float x, float y, float w, float h) {
    if (!ui_available_ || host_path.empty()) return;
    UiOp op{};
    op.type = UiOp::Type::Image;
    op.image = UiImageOp{std::string(key), std::string(host_path), x, y, w, h};
    ui_ops_.push_back(std::move(op));
}

void Sdl2GpuBackend::UiFillRectOverlay(float x, float y, float w, float h,
                                       float r, float g, float b, float a) {
    if (!ui_available_) return;
    UiOp op{};
    op.type = UiOp::Type::FillRect;
    op.rx = x; op.ry = y; op.rw = w; op.rh = h;
    op.r = r; op.g = g; op.b = b; op.a = a;
    ui_ops_.push_back(std::move(op));
}

void Sdl2GpuBackend::UiRectOutlineOverlay(float x, float y, float w, float h,
                                          float thickness, float r, float g, float b, float a) {
    if (!ui_available_) return;
    UiOp op{};
    op.type = UiOp::Type::RectOutline;
    op.rx = x; op.ry = y; op.rw = w; op.rh = h;
    op.thickness = thickness;
    op.r = r; op.g = g; op.b = b; op.a = a;
    ui_ops_.push_back(std::move(op));
}

void Sdl2GpuBackend::FlushUiOverlay() {
#ifdef NEMU_SDL2_UI
    if (!renderer_) return;

    for (const auto& op : ui_ops_) {
        if (op.type == UiOp::Type::FillRect) {
            SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);
            SDL_SetRenderDrawColor(renderer_,
                static_cast<Uint8>(std::clamp(op.r, 0.0f, 1.0f) * 255.0f),
                static_cast<Uint8>(std::clamp(op.g, 0.0f, 1.0f) * 255.0f),
                static_cast<Uint8>(std::clamp(op.b, 0.0f, 1.0f) * 255.0f),
                static_cast<Uint8>(std::clamp(op.a, 0.0f, 1.0f) * 255.0f));
            SDL_Rect r{static_cast<int>(op.rx), static_cast<int>(op.ry),
                       static_cast<int>(op.rw), static_cast<int>(op.rh)};
            SDL_RenderFillRect(renderer_, &r);
        } else if (op.type == UiOp::Type::RectOutline) {
            SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);
            SDL_SetRenderDrawColor(renderer_,
                static_cast<Uint8>(std::clamp(op.r, 0.0f, 1.0f) * 255.0f),
                static_cast<Uint8>(std::clamp(op.g, 0.0f, 1.0f) * 255.0f),
                static_cast<Uint8>(std::clamp(op.b, 0.0f, 1.0f) * 255.0f),
                static_cast<Uint8>(std::clamp(op.a, 0.0f, 1.0f) * 255.0f));
            int th = std::max(1, static_cast<int>(op.thickness));
            int ix = static_cast<int>(op.rx);
            int iy = static_cast<int>(op.ry);
            int iw = static_cast<int>(op.rw);
            int ih = static_cast<int>(op.rh);
            SDL_Rect top{ix, iy, iw, th};
            SDL_Rect btm{ix, iy + ih - th, iw, th};
            SDL_Rect left{ix, iy, th, ih};
            SDL_Rect right{ix + iw - th, iy, th, ih};
            SDL_RenderFillRect(renderer_, &top);
            SDL_RenderFillRect(renderer_, &btm);
            SDL_RenderFillRect(renderer_, &left);
            SDL_RenderFillRect(renderer_, &right);
        } else if (op.type == UiOp::Type::Image) {
            SDL_Texture* tex = CoverTexture(op.image.host_path);
            if (!tex) continue;
            int iw = 0, ih = 0;
            SDL_QueryTexture(tex, nullptr, nullptr, &iw, &ih);
            SDL_Rect dst{static_cast<int>(op.image.x), static_cast<int>(op.image.y),
                         static_cast<int>(op.image.w), static_cast<int>(op.image.h)};
            if (iw > 0 && ih > 0) {
                const float src_ar = static_cast<float>(iw) / static_cast<float>(ih);
                const float dst_ar = op.image.w / op.image.h;
                SDL_Rect src{0, 0, iw, ih};
                if (src_ar > dst_ar) {
                    const int cw = static_cast<int>(static_cast<float>(ih) * dst_ar);
                    src.x = (iw - cw) / 2;
                    src.w = cw;
                } else if (src_ar < dst_ar) {
                    const int ch = static_cast<int>(static_cast<float>(iw) / dst_ar);
                    src.y = (ih - ch) / 2;
                    src.h = ch;
                }
                SDL_RenderCopy(renderer_, tex, &src, &dst);
            } else {
                SDL_RenderCopy(renderer_, tex, nullptr, &dst);
            }
        } else if (op.type == UiOp::Type::Text) {
            SDL_Texture* tex = TextTexture(op.text);
            if (!tex) continue;
            int tw = 0, th = 0;
            SDL_QueryTexture(tex, nullptr, nullptr, &tw, &th);
            float dx = op.text.x;
            if (op.text.align == 0) dx = op.text.x - static_cast<float>(tw) * 0.5f;
            else if (op.text.align == 1) dx = op.text.x - static_cast<float>(tw);
            SDL_Rect dst{static_cast<int>(dx), static_cast<int>(op.text.y), tw, th};
            SDL_SetTextureAlphaMod(tex, static_cast<Uint8>(std::clamp(op.text.a, 0.0f, 1.0f) * 255.0f));
            SDL_RenderCopy(renderer_, tex, nullptr, &dst);
        }
    }
#endif
}

#ifdef NEMU_SDL2_UI
std::string Sdl2GpuBackend::FontPath() {
    static std::string s_font_path;
    if (!s_font_path.empty()) return s_font_path;

    if (const char* env = std::getenv("NEMU_FONT_PATH")) {
        if (std::filesystem::exists(env)) {
            s_font_path = env;
            return s_font_path;
        }
    }
    static const char* kCandidates[] = {
        "/usr/share/fonts/truetype/noto/NotoSans-Regular.ttf",
        "/usr/share/fonts/opentype/noto/NotoSans-Regular.ttf",
        "/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf",
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
        "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf",
        "/usr/share/fonts/truetype/freefont/FreeSans.ttf",
        "/usr/share/fonts/TTF/DejaVuSans.ttf",
    };
    for (const char* p : kCandidates) {
        if (std::filesystem::exists(p)) {
            s_font_path = p;
            return s_font_path;
        }
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
    if (font) {
        TTF_SetFontHinting(font, TTF_HINTING_LIGHT);
        fonts_[pts] = font;
    }
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
        SDL_SetTextureScaleMode(tex, SDL_ScaleModeLinear);
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
        if (tex) {
            SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);
            SDL_SetTextureScaleMode(tex, SDL_ScaleModeLinear);
        }
    }
    cover_cache_[host_path] = tex;  // cache negative result too
    return tex;
}
#endif // NEMU_SDL2_UI
} // namespace nemu::core::gpu::sdl2

#endif // NEMU_SDL2
