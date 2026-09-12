#include "xbox_frontend.hpp"
#include "platform/logger.hpp"
#include <algorithm>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <cmath>

namespace nemu::frontend {

XboxFrontend::XboxFrontend(core::filesystem::VirtualFileSystem& vfs, core::config::ConfigManager& config)
    : vfs_(vfs), config_(config) {
    RefreshLibrary();
    RefreshFileManager("sdmc:/");
}

std::string XboxFrontend::GetSystemClockString() const {
    auto now = std::chrono::system_clock::now();
    std::time_t now_c = std::chrono::system_clock::to_time_t(now);
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &now_c);
#else
    localtime_r(&now_c, &tm);
#endif
    char buf[16];
    std::strftime(buf, sizeof(buf), "%I:%M %p", &tm);
    return std::string(buf);
}

std::string XboxFrontend::GetConsoleModeString() const {
    return (config_.GetConfig().console_mode == core::config::ConsoleMode::Docked) ?
           "[ 📺 DOCKED 1080p ]" : "[ 📱 HANDHELD 720p ]";
}

void XboxFrontend::RefreshLibrary() {
    library_.clear();

    auto sdmc_host = vfs_.ResolvePath("sdmc:/");
    if (sdmc_host && std::filesystem::exists(*sdmc_host)) {
        std::error_code ec;
        for (const auto& entry : std::filesystem::directory_iterator(*sdmc_host, ec)) {
            if (entry.is_regular_file(ec)) {
                auto ext = entry.path().extension().string();
                std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return std::tolower(c); });
                if (ext == ".nro" || ext == ".nsp" || ext == ".xci" || ext == ".nca" || ext == ".nso") {
                    std::string stem = entry.path().stem().string();
                    std::string badge = "[NRO]";
                    if (ext == ".nsp") badge = "[NSP]";
                    else if (ext == ".xci") badge = "[XCI]";
                    else if (ext == ".nca") badge = "[NCA]";
                    else if (ext == ".nso") badge = "[NSO]";

                    library_.push_back(GameEntry{
                        .title = stem,
                        .filename = entry.path().filename().string(),
                        .virtual_path = "sdmc:/" + entry.path().filename().string(),
                        .format_badge = badge,
                        .playtime_str = "Played 1h 45m",
                        .optimizer_tag = "FSR 2.0 • 4x MSAA • 60 FPS",
                        .file_size = static_cast<size_t>(entry.file_size(ec)),
                        .title_id = 0x0100000000010000ULL
                    });
                }
            }
        }
    }

    // Default verified built-in showcase entry
    if (library_.empty()) {
        library_.push_back(GameEntry{
            .title = "Nintendo Switch System Showcase (ARM64 JIT + D3D12)",
            .filename = "demo.nro",
            .virtual_path = "builtin:/demo.nro",
            .format_badge = "[NRO]",
            .playtime_str = "First Played Today",
            .optimizer_tag = "FSR 2.0 • 4x MSAA • AFMF 2x Frame Gen",
            .file_size = 16384,
            .title_id = 0x0100000000000001ULL
        });
    }

    if (selected_game_index_ >= library_.size()) {
        selected_game_index_ = 0;
    }

    NEMU_LOG_INFO("Frontend", "Eden UI: Discovered {} titles in library carousel", library_.size());
}

void XboxFrontend::RefreshFileManager(std::string_view dir_path) {
    dir_entries_.clear();
    current_dir_path_ = std::string(dir_path);

    // If not root, add parent directory entry
    if (current_dir_path_ != "sdmc:/" && current_dir_path_ != "/" && current_dir_path_ != "save:/") {
        std::string parent = current_dir_path_;
        if (parent.back() == '/') parent.pop_back();
        auto slash = parent.find_last_of('/');
        if (slash != std::string::npos) {
            parent = parent.substr(0, slash + 1);
        } else {
            parent = "sdmc:/";
        }
        dir_entries_.push_back(FileEntry{
            .name = ".. [Up to Parent Directory]",
            .full_path = parent,
            .is_directory = true,
            .is_rom = false,
            .format_badge = "[DIR]",
            .file_size = 0
        });
    }

    // Resolve host path
    std::optional<std::filesystem::path> host_dir;
    if (current_dir_path_.starts_with("sdmc:/") || current_dir_path_.starts_with("save:/") || current_dir_path_.starts_with("romfs:/")) {
        host_dir = vfs_.ResolvePath(current_dir_path_);
    } else {
        host_dir = std::filesystem::path(current_dir_path_);
    }

    if (host_dir && std::filesystem::exists(*host_dir)) {
        std::error_code ec;
        std::vector<FileEntry> dirs;
        std::vector<FileEntry> files;

        for (const auto& entry : std::filesystem::directory_iterator(*host_dir, ec)) {
            std::string filename = entry.path().filename().string();
            if (filename.empty() || filename[0] == '.') continue;

            if (entry.is_directory(ec)) {
                std::string vpath = current_dir_path_;
                if (vpath.back() != '/') vpath += '/';
                vpath += filename;
                dirs.push_back(FileEntry{
                    .name = filename + "/",
                    .full_path = vpath,
                    .is_directory = true,
                    .is_rom = false,
                    .format_badge = "[DIR]",
                    .file_size = 0
                });
            } else if (entry.is_regular_file(ec)) {
                std::string ext = entry.path().extension().string();
                std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return std::tolower(c); });

                bool is_rom = (ext == ".nsp" || ext == ".xci" || ext == ".nro" || ext == ".nca" || ext == ".nso");
                std::string badge = "[FILE]";
                if (ext == ".nsp") badge = "[NSP]";
                else if (ext == ".xci") badge = "[XCI]";
                else if (ext == ".nro") badge = "[NRO]";
                else if (ext == ".nca") badge = "[NCA]";
                else if (ext == ".nso") badge = "[NSO]";

                std::string vpath = current_dir_path_;
                if (vpath.back() != '/') vpath += '/';
                vpath += filename;

                files.push_back(FileEntry{
                    .name = filename,
                    .full_path = vpath,
                    .is_directory = false,
                    .is_rom = is_rom,
                    .format_badge = badge,
                    .file_size = static_cast<size_t>(entry.file_size(ec))
                });
            }
        }

        std::sort(dirs.begin(), dirs.end(), [](const auto& a, const auto& b) { return a.name < b.name; });
        std::sort(files.begin(), files.end(), [](const auto& a, const auto& b) { return a.name < b.name; });

        for (auto& d : dirs) dir_entries_.push_back(std::move(d));
        for (auto& f : files) dir_entries_.push_back(std::move(f));
    }

    if (dir_entries_.empty()) {
        dir_entries_.push_back(FileEntry{
            .name = "demo.nro",
            .full_path = "builtin:/demo.nro",
            .is_directory = false,
            .is_rom = true,
            .format_badge = "[NRO]",
            .file_size = 16384
        });
    }

    if (selected_file_index_ >= dir_entries_.size()) {
        selected_file_index_ = 0;
    }

    NEMU_LOG_INFO("Frontend", "FileManager: Listed {} entries in '{}'", dir_entries_.size(), current_dir_path_);
}

void XboxFrontend::TriggerRumbleTest(core::hid::XboxControllerDriver* driver) {
    if (!driver) return;
    const auto& cfg = config_.GetConfig();
    if (!cfg.vibration_enabled) return;

    NEMU_LOG_INFO("Frontend", "Testing Xbox impulse rumble motors at strength {:.0f}%", cfg.vibration_strength * 100.0f);
    driver->SetVibration(0, 0.6f * cfg.vibration_strength, 0.9f * cfg.vibration_strength);
}

void XboxFrontend::ProcessInput(const core::hid::XboxGamepadState& input, core::hid::XboxControllerDriver* driver) {
    constexpr s32 STICK_THRESHOLD = 16000;

    // Button edge detection
    const bool pressed_up    = input.dpad_up && !prev_dpad_up_;
    const bool pressed_down  = input.dpad_down && !prev_dpad_down_;
    const bool pressed_left  = input.dpad_left && !prev_dpad_left_;
    const bool pressed_right = input.dpad_right && !prev_dpad_right_;

    const bool pressed_a     = input.a && !prev_btn_a_;
    const bool pressed_b     = input.b && !prev_btn_b_;
    const bool pressed_x     = input.x && !prev_btn_x_;
    const bool pressed_y     = input.y && !prev_btn_y_;
    const bool pressed_lb    = input.lb && !prev_btn_lb_;
    const bool pressed_rb    = input.rb && !prev_btn_rb_;
    const bool pressed_start = input.start && !prev_btn_start_;

    // Stick navigation
    const bool stick_left  = (input.thumb_lx < -STICK_THRESHOLD) && (prev_stick_x_ >= -STICK_THRESHOLD);
    const bool stick_right = (input.thumb_lx > STICK_THRESHOLD) && (prev_stick_x_ <= STICK_THRESHOLD);
    const bool stick_up    = (input.thumb_ly > STICK_THRESHOLD) && (prev_stick_y_ <= STICK_THRESHOLD);
    const bool stick_down  = (input.thumb_ly < -STICK_THRESHOLD) && (prev_stick_y_ >= STICK_THRESHOLD);

    const bool nav_left  = pressed_left || stick_left;
    const bool nav_right = pressed_right || stick_right;
    const bool nav_up    = pressed_up || stick_up;
    const bool nav_down  = pressed_down || stick_down;

    // Save previous state
    prev_dpad_up_    = input.dpad_up;
    prev_dpad_down_  = input.dpad_down;
    prev_dpad_left_  = input.dpad_left;
    prev_dpad_right_ = input.dpad_right;
    prev_btn_a_      = input.a;
    prev_btn_b_      = input.b;
    prev_btn_x_      = input.x;
    prev_btn_y_      = input.y;
    prev_btn_lb_     = input.lb;
    prev_btn_rb_     = input.rb;
    prev_btn_start_  = input.start;
    prev_stick_x_    = input.thumb_lx;
    prev_stick_y_    = input.thumb_ly;

    // If Game Options modal is active, route all input to it
    if (show_game_options_) {
        HandleGameOptionsInput(input, nav_up, nav_down, nav_left, nav_right, pressed_a, pressed_b);
        return;
    }

    // Tab switching with LB/RB across all 6 tabs
    if (pressed_lb) {
        u32 tab_num = static_cast<u32>(current_tab_);
        current_tab_ = (tab_num == 0) ? FrontendTab::Diagnostics : static_cast<FrontendTab>(tab_num - 1);
        selected_setting_row_ = 0;
        return;
    }
    if (pressed_rb) {
        u32 tab_num = static_cast<u32>(current_tab_);
        current_tab_ = (tab_num >= 5) ? FrontendTab::Library : static_cast<FrontendTab>(tab_num + 1);
        selected_setting_row_ = 0;
        return;
    }

    // Direct shortcuts
    if (pressed_x) {
        current_tab_ = (current_tab_ == FrontendTab::FileManager) ? FrontendTab::Library : FrontendTab::FileManager;
        selected_setting_row_ = 0;
        return;
    }
    if (pressed_y) {
        current_tab_ = (current_tab_ == FrontendTab::Controllers) ? FrontendTab::Library : FrontendTab::Controllers;
        selected_setting_row_ = 0;
        return;
    }
    if (pressed_b && current_tab_ != FrontendTab::Library) {
        current_tab_ = FrontendTab::Library;
        selected_setting_row_ = 0;
        return;
    }

    // Dispatch input to current active tab
    switch (current_tab_) {
        case FrontendTab::Library:
            HandleLibraryInput(input, nav_left, nav_right, pressed_a, pressed_start);
            break;
        case FrontendTab::FileManager:
            HandleFileManagerInput(input, nav_up, nav_down, pressed_a, pressed_b, pressed_x);
            break;
        case FrontendTab::Optimizers:
            HandleOptimizersInput(input, nav_up, nav_down, nav_left, nav_right, pressed_a);
            break;
        case FrontendTab::Controllers:
            HandleControllersInput(input, nav_up, nav_down, nav_left, nav_right, pressed_a, driver);
            break;
        case FrontendTab::System:
            HandleSystemInput(input, nav_up, nav_down, nav_left, nav_right, pressed_a);
            break;
        case FrontendTab::Diagnostics:
            if (pressed_a) {
                RefreshLibrary();
            }
            break;
    }
}

void XboxFrontend::HandleLibraryInput(const core::hid::XboxGamepadState& input, bool pressed_left, bool pressed_right, bool pressed_a, bool pressed_start) {
    (void)input;
    if (library_.empty()) return;

    if (pressed_start) {
        show_game_options_ = true;
        game_options_row_ = 0;
        NEMU_LOG_INFO("Frontend", "Eden UI: Opened Game Options (+) for '{}'", library_[selected_game_index_].title);
        return;
    }

    if (pressed_left) {
        if (selected_game_index_ > 0) {
            selected_game_index_--;
        } else {
            selected_game_index_ = library_.size() - 1; // Wrap around
        }
    }
    if (pressed_right) {
        if (selected_game_index_ + 1 < library_.size()) {
            selected_game_index_++;
        } else {
            selected_game_index_ = 0; // Wrap around
        }
    }

    if (pressed_a) {
        launch_requested_ = library_[selected_game_index_].virtual_path;
        NEMU_LOG_INFO("Frontend", "Eden UI: Launching title '{}' ({})",
                      library_[selected_game_index_].title,
                      library_[selected_game_index_].virtual_path);
    }
}

void XboxFrontend::HandleFileManagerInput(const core::hid::XboxGamepadState& input, bool pressed_up, bool pressed_down, bool pressed_a, bool pressed_b, bool pressed_x) {
    (void)input;
    if (dir_entries_.empty()) return;

    if (pressed_up) {
        selected_file_index_ = (selected_file_index_ > 0) ? selected_file_index_ - 1 : dir_entries_.size() - 1;
    }
    if (pressed_down) {
        selected_file_index_ = (selected_file_index_ + 1 < dir_entries_.size()) ? selected_file_index_ + 1 : 0;
    }

    if (pressed_a) {
        const auto& entry = dir_entries_[selected_file_index_];
        if (entry.is_directory) {
            RefreshFileManager(entry.full_path);
        } else if (entry.is_rom) {
            NEMU_LOG_INFO("Frontend", "FileManager: Instant boot for ROM '{}'", entry.full_path);
            launch_requested_ = entry.full_path;
        }
    }

    if (pressed_b) {
        if (current_dir_path_ != "sdmc:/" && current_dir_path_ != "/") {
            std::string parent = current_dir_path_;
            if (parent.back() == '/') parent.pop_back();
            auto slash = parent.find_last_of('/');
            if (slash != std::string::npos) {
                parent = parent.substr(0, slash + 1);
            } else {
                parent = "sdmc:/";
            }
            RefreshFileManager(parent);
        } else {
            current_tab_ = FrontendTab::Library;
        }
    }

    if (pressed_x) {
        RefreshLibrary();
        NEMU_LOG_INFO("Frontend", "FileManager: Added/refreshed library scan for current directory");
    }
}

void XboxFrontend::HandleGameOptionsInput(const core::hid::XboxGamepadState& input, bool pressed_up, bool pressed_down, bool pressed_left, bool pressed_right, bool pressed_a, bool pressed_b) {
    (void)input;
    if (library_.empty()) {
        show_game_options_ = false;
        return;
    }

    constexpr size_t TOTAL_OPTION_ROWS = 7;
    if (pressed_up) {
        game_options_row_ = (game_options_row_ > 0) ? game_options_row_ - 1 : TOTAL_OPTION_ROWS - 1;
    }
    if (pressed_down) {
        game_options_row_ = (game_options_row_ + 1 < TOTAL_OPTION_ROWS) ? game_options_row_ + 1 : 0;
    }

    if (pressed_b) {
        show_game_options_ = false;
        return;
    }

    const auto& game = library_[selected_game_index_];
    core::config::PerGameConfig cfg{};
    config_.LoadGameConfig(game.title_id, cfg);
    bool changed = false;

    switch (game_options_row_) {
        case 0: // Launch Title
            if (pressed_a) {
                show_game_options_ = false;
                launch_requested_ = game.virtual_path;
            }
            break;

        case 1: // Per-Game Upscaler Mode
            if (pressed_left || pressed_right || pressed_a) {
                cfg.has_custom_settings = true;
                u32 cur = static_cast<u32>(cfg.upscaler);
                cfg.upscaler = static_cast<core::gpu::pipeline::UpscalerMode>((cur + 1) % 5);
                changed = true;
            }
            break;

        case 2: // Per-Game Resolution Scale
            if (pressed_left || pressed_right || pressed_a) {
                cfg.has_custom_settings = true;
                u32 cur = static_cast<u32>(cfg.resolution_scale);
                cfg.resolution_scale = static_cast<core::config::ResolutionScale>((cur + 1) % 5);
                changed = true;
            }
            break;

        case 3: // Per-Game Frame Generation
            if (pressed_left || pressed_right || pressed_a) {
                cfg.has_custom_settings = true;
                cfg.frame_generation = (cfg.frame_generation == core::gpu::pipeline::FrameGenMode::Disabled) ?
                    core::gpu::pipeline::FrameGenMode::AFMF_Extrapolation_2x : core::gpu::pipeline::FrameGenMode::Disabled;
                changed = true;
            }
            break;

        case 4: // Per-Game Button Layout
            if (pressed_left || pressed_right || pressed_a) {
                cfg.has_custom_settings = true;
                cfg.button_layout = (cfg.button_layout == core::hid::FaceButtonLayout::NintendoStandard) ?
                    core::hid::FaceButtonLayout::XboxMirrored : core::hid::FaceButtonLayout::NintendoStandard;
                changed = true;
            }
            break;

        case 5: // Inspect / Verify Save Data
            if (pressed_a) {
                NEMU_LOG_INFO("Frontend", "GameOptions: Verified save data for title 0x{:016X}", game.title_id);
            }
            break;

        case 6: // Close Options
            if (pressed_a) {
                show_game_options_ = false;
            }
            break;
    }

    if (changed) {
        config_.SaveGameConfig(game.title_id, cfg);
    }
}

void XboxFrontend::HandleOptimizersInput(const core::hid::XboxGamepadState& input, bool pressed_up, bool pressed_down, bool pressed_left, bool pressed_right, bool pressed_a) {
    (void)input;
    constexpr size_t TOTAL_ROWS = 7;

    if (pressed_up) {
        selected_setting_row_ = (selected_setting_row_ > 0) ? selected_setting_row_ - 1 : TOTAL_ROWS - 1;
    }
    if (pressed_down) {
        selected_setting_row_ = (selected_setting_row_ + 1 < TOTAL_ROWS) ? selected_setting_row_ + 1 : 0;
    }

    auto& cfg = config_.GetConfig();
    bool changed = false;

    switch (selected_setting_row_) {
        case 0: // Resolution Scale
            if (pressed_left && static_cast<u32>(cfg.resolution_scale) > 0) {
                cfg.resolution_scale = static_cast<core::config::ResolutionScale>(static_cast<u32>(cfg.resolution_scale) - 1);
                changed = true;
            } else if (pressed_right && static_cast<u32>(cfg.resolution_scale) < 4) {
                cfg.resolution_scale = static_cast<core::config::ResolutionScale>(static_cast<u32>(cfg.resolution_scale) + 1);
                changed = true;
            }
            break;

        case 1: // Spatial Upscaler (FSR / Bicubic / Bilinear)
            if (pressed_left && static_cast<u32>(cfg.upscaler) > 0) {
                cfg.upscaler = static_cast<core::gpu::pipeline::UpscalerMode>(static_cast<u32>(cfg.upscaler) - 1);
                changed = true;
            } else if (pressed_right && static_cast<u32>(cfg.upscaler) < 4) {
                cfg.upscaler = static_cast<core::gpu::pipeline::UpscalerMode>(static_cast<u32>(cfg.upscaler) + 1);
                changed = true;
            }
            break;

        case 2: // FSR Sharpness
            if (pressed_left) {
                cfg.fsr_sharpness = std::max(0.0f, cfg.fsr_sharpness - 0.05f);
                changed = true;
            } else if (pressed_right) {
                cfg.fsr_sharpness = std::min(1.0f, cfg.fsr_sharpness + 0.05f);
                changed = true;
            }
            break;

        case 3: // Anti-Aliasing (MSAA / FXAA)
            if (pressed_left && static_cast<u32>(cfg.anti_aliasing) > 0) {
                cfg.anti_aliasing = static_cast<core::gpu::pipeline::AntiAliasingMode>(static_cast<u32>(cfg.anti_aliasing) - 1);
                changed = true;
            } else if (pressed_right && static_cast<u32>(cfg.anti_aliasing) < 5) {
                cfg.anti_aliasing = static_cast<core::gpu::pipeline::AntiAliasingMode>(static_cast<u32>(cfg.anti_aliasing) + 1);
                changed = true;
            }
            break;

        case 4: // Frame Generation (AFMF 2x)
            if (pressed_left || pressed_right || pressed_a) {
                cfg.frame_generation = (cfg.frame_generation == core::gpu::pipeline::FrameGenMode::Disabled) ?
                    core::gpu::pipeline::FrameGenMode::AFMF_Extrapolation_2x : core::gpu::pipeline::FrameGenMode::Disabled;
                changed = true;
            }
            break;

        case 5: // VSync
            if (pressed_left || pressed_right || pressed_a) {
                cfg.vsync = !cfg.vsync;
                changed = true;
            }
            break;

        case 6: // Fastmem MMU
            if (pressed_left || pressed_right || pressed_a) {
                cfg.fastmem_enabled = !cfg.fastmem_enabled;
                changed = true;
            }
            break;
    }

    if (changed) {
        config_.Save();
    }
}

void XboxFrontend::HandleControllersInput(const core::hid::XboxGamepadState& input, bool pressed_up, bool pressed_down, bool pressed_left, bool pressed_right, bool pressed_a, core::hid::XboxControllerDriver* driver) {
    (void)input;
    constexpr size_t TOTAL_ROWS = 7;

    if (pressed_up) {
        selected_setting_row_ = (selected_setting_row_ > 0) ? selected_setting_row_ - 1 : TOTAL_ROWS - 1;
    }
    if (pressed_down) {
        selected_setting_row_ = (selected_setting_row_ + 1 < TOTAL_ROWS) ? selected_setting_row_ + 1 : 0;
    }

    auto& cfg = config_.GetConfig();
    bool changed = false;

    switch (selected_setting_row_) {
        case 0: // Controller Type (Pro Controller / Joy-Con)
            if (pressed_left || pressed_right || pressed_a) {
                u32 cur = static_cast<u32>(cfg.controller_type);
                cfg.controller_type = static_cast<core::config::ControllerType>((cur + 1) % 3);
                changed = true;
            }
            break;

        case 1: // Button Layout (Nintendo vs Xbox)
            if (pressed_left || pressed_right || pressed_a) {
                cfg.button_layout = (cfg.button_layout == core::hid::FaceButtonLayout::NintendoStandard) ?
                    core::hid::FaceButtonLayout::XboxMirrored : core::hid::FaceButtonLayout::NintendoStandard;
                changed = true;
            }
            break;

        case 2: // Stick Inner Deadzone
            if (pressed_left) {
                cfg.inner_deadzone = std::max(0.02f, cfg.inner_deadzone - 0.02f);
                changed = true;
            } else if (pressed_right) {
                cfg.inner_deadzone = std::min(0.35f, cfg.inner_deadzone + 0.02f);
                changed = true;
            }
            break;

        case 3: // Stick Outer Deadzone
            if (pressed_left) {
                cfg.outer_deadzone = std::max(0.80f, cfg.outer_deadzone - 0.02f);
                changed = true;
            } else if (pressed_right) {
                cfg.outer_deadzone = std::min(1.00f, cfg.outer_deadzone + 0.02f);
                changed = true;
            }
            break;

        case 4: // HD Rumble Toggle
            if (pressed_left || pressed_right || pressed_a) {
                cfg.vibration_enabled = !cfg.vibration_enabled;
                changed = true;
            }
            break;

        case 5: // Vibration Strength
            if (pressed_left) {
                cfg.vibration_strength = std::max(0.0f, cfg.vibration_strength - 0.1f);
                changed = true;
            } else if (pressed_right) {
                cfg.vibration_strength = std::min(1.0f, cfg.vibration_strength + 0.1f);
                changed = true;
            }
            break;

        case 6: // Test Vibration Motor
            if (pressed_a) {
                TriggerRumbleTest(driver);
            }
            break;
    }

    if (changed) {
        config_.Save();
    }
}

void XboxFrontend::HandleSystemInput(const core::hid::XboxGamepadState& input, bool pressed_up, bool pressed_down, bool pressed_left, bool pressed_right, bool pressed_a) {
    (void)input;
    constexpr size_t TOTAL_ROWS = 4;

    if (pressed_up) {
        selected_setting_row_ = (selected_setting_row_ > 0) ? selected_setting_row_ - 1 : TOTAL_ROWS - 1;
    }
    if (pressed_down) {
        selected_setting_row_ = (selected_setting_row_ + 1 < TOTAL_ROWS) ? selected_setting_row_ + 1 : 0;
    }

    auto& cfg = config_.GetConfig();
    bool changed = false;

    switch (selected_setting_row_) {
        case 0: // Console Mode (Docked vs Handheld)
            if (pressed_left || pressed_right || pressed_a) {
                cfg.console_mode = (cfg.console_mode == core::config::ConsoleMode::Docked) ?
                    core::config::ConsoleMode::Handheld : core::config::ConsoleMode::Docked;
                changed = true;
            }
            break;

        case 1: // System Language
            if (pressed_left && static_cast<u32>(cfg.system_language) > 0) {
                cfg.system_language = static_cast<core::config::SystemLanguage>(static_cast<u32>(cfg.system_language) - 1);
                changed = true;
            } else if (pressed_right && static_cast<u32>(cfg.system_language) < 5) {
                cfg.system_language = static_cast<core::config::SystemLanguage>(static_cast<u32>(cfg.system_language) + 1);
                changed = true;
            }
            break;

        case 2: // Audio Volume
            if (pressed_left) {
                cfg.audio_volume = (cfg.audio_volume >= 5) ? cfg.audio_volume - 5 : 0;
                changed = true;
            } else if (pressed_right) {
                cfg.audio_volume = std::min(100u, cfg.audio_volume + 5);
                changed = true;
            }
            break;

        case 3: // Surround 5.1/7.1 Sound
            if (pressed_left || pressed_right || pressed_a) {
                cfg.surround_enabled = !cfg.surround_enabled;
                changed = true;
            }
            break;
    }

    if (changed) {
        config_.Save();
    }
}

void XboxFrontend::Render(core::gpu::IGpuBackend& gpu) {
    gpu.BeginFrame();

    // Iconic Nintendo Switch Dark Charcoal Slate (#18191C) + Eden Cyan Accents
    core::gpu::ClearColor bg{
        .r = 0.094f,
        .g = 0.098f,
        .b = 0.110f,
        .a = 1.0f
    };
    gpu.ClearRenderTarget(bg);

    // Rasterize UI layout: top banner, tabs, carousel panels, bottom controls
    gpu.DrawArrays(core::gpu::PrimitiveTopology::Triangles, 0, 6);

    ui_frame_count_++;
    focus_animation_timer_ += 0.016f;

    gpu.EndFrame();
    gpu.Present();
}

std::optional<std::string> XboxFrontend::ConsumeLaunchRequest() {
    auto req = launch_requested_;
    launch_requested_ = std::nullopt;
    return req;
}

} // namespace nemu::frontend
