#include "xbox_frontend.hpp"
#include "platform/logger.hpp"
#include <algorithm>

namespace nemu::frontend {

XboxFrontend::XboxFrontend(core::filesystem::VirtualFileSystem& vfs, core::config::ConfigManager& config)
    : vfs_(vfs), config_(config) {
    RefreshLibrary();
}

void XboxFrontend::RefreshLibrary() {
    library_.clear();

    // Check host path if mounted
    auto sdmc_host = vfs_.ResolvePath("sdmc:/");
    if (sdmc_host && std::filesystem::exists(*sdmc_host)) {
        std::error_code ec;
        for (const auto& entry : std::filesystem::directory_iterator(*sdmc_host, ec)) {
            if (entry.is_regular_file(ec)) {
                auto ext = entry.path().extension().string();
                std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return std::tolower(c); });
                if (ext == ".nro" || ext == ".nsp" || ext == ".xci" || ext == ".nca") {
                    library_.push_back(HomebrewEntry{
                        .filename = entry.path().filename().string(),
                        .virtual_path = "sdmc:/" + entry.path().filename().string(),
                        .file_size = entry.file_size(ec)
                    });
                }
            }
        }
    }

    // If no external NRO files found, provide default built-in homebrew test entry
    if (library_.empty()) {
        library_.push_back(HomebrewEntry{
            .filename = "Built-in Homebrew Demo (ARM64 Arithmetic & GPU Clear)",
            .virtual_path = "builtin:/demo.nro",
            .file_size = 16384
        });
    }

    if (selected_index_ >= library_.size()) {
        selected_index_ = 0;
    }

    NEMU_LOG_INFO("Frontend", "Discovered {} homebrew titles in library", library_.size());
}

void XboxFrontend::ProcessInput(const core::hid::XboxGamepadState& input) {
    // Edge detection
    const bool pressed_up    = input.dpad_up && !prev_dpad_up_;
    const bool pressed_down  = input.dpad_down && !prev_dpad_down_;
    const bool pressed_a     = input.a && !prev_btn_a_;
    const bool pressed_b     = input.b && !prev_btn_b_;
    const bool pressed_x     = input.x && !prev_btn_x_;
    const bool pressed_y     = input.y && !prev_btn_y_;

    prev_dpad_up_   = input.dpad_up;
    prev_dpad_down_ = input.dpad_down;
    prev_btn_a_     = input.a;
    prev_btn_b_     = input.b;
    prev_btn_x_     = input.x;
    prev_btn_y_     = input.y;

    if (pressed_up) {
        if (selected_index_ > 0) {
            selected_index_--;
        }
    }

    if (pressed_down) {
        if (current_view_ == FrontendView::Library && selected_index_ + 1 < library_.size()) {
            selected_index_++;
        }
    }

    // View switching
    if (pressed_x) {
        current_view_ = (current_view_ == FrontendView::Settings) ? FrontendView::Library : FrontendView::Settings;
        selected_index_ = 0;
    }

    if (pressed_y) {
        current_view_ = (current_view_ == FrontendView::Diagnostics) ? FrontendView::Library : FrontendView::Diagnostics;
        selected_index_ = 0;
    }

    if (pressed_b) {
        current_view_ = FrontendView::Library;
        selected_index_ = 0;
    }

    // Action execution
    if (pressed_a) {
        if (current_view_ == FrontendView::Library && !library_.empty()) {
            launch_requested_ = library_[selected_index_].virtual_path;
            NEMU_LOG_INFO("Frontend", "User requested launch: {}", *launch_requested_);
        } else if (current_view_ == FrontendView::Settings) {
            // Toggle settings
            auto& cfg = config_.GetConfig();
            cfg.vsync = !cfg.vsync;
            config_.Save();
            NEMU_LOG_INFO("Frontend", "Toggled VSync: {}", cfg.vsync);
        }
    }
}

void XboxFrontend::Render(core::gpu::IGpuBackend& gpu) {
    gpu.BeginFrame();

    // Dark charcoal slate theme (#18191C)
    core::gpu::ClearColor bg{
        .r = 0.094f,
        .g = 0.098f,
        .b = 0.110f,
        .a = 1.0f
    };
    gpu.ClearRenderTarget(bg);

    // Draw UI geometry (e.g. background panels / cursor)
    gpu.DrawArrays(core::gpu::PrimitiveTopology::Triangles, 0, 6);

    gpu.EndFrame();
    gpu.Present();
}

std::optional<std::string> XboxFrontend::ConsumeLaunchRequest() {
    auto req = launch_requested_;
    launch_requested_ = std::nullopt;
    return req;
}

} // namespace nemu::frontend
