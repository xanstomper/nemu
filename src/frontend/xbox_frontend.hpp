#pragma once

#include "core/types.hpp"
#include "core/gpu/gpu_interface.hpp"
#include "core/hid/controller_mapping.hpp"
#include "core/hid/xbox_controller_driver.hpp"
#include "core/filesystem/vfs.hpp"
#include "core/config/config_manager.hpp"
#include "core/gpu/pipeline/graphics_optimizer.hpp"
#include <vector>
#include <string>
#include <string_view>
#include <optional>
#include <memory>
#include <chrono>

namespace nemu::frontend {

enum class FrontendTab : u32 {
    Library = 0,     // Switch Game Carousel & Eden cards
    FileManager = 1, // RetroArch / Eden style Filesystem Explorer
    Optimizers = 2,  // FSR Upscalers, MSAA, Frame Gen, Resolution Scale
    Controllers = 3, // Pro Controller, Joy-Con, Deadzones, HD Rumble
    System = 4,      // Docked/Handheld mode, Language, Audio
    Diagnostics = 5  // JIT stats, GPU stats, Fastmem VEH fault telemetry
};

struct GameEntry {
    std::string title;
    std::string filename;
    std::string virtual_path;
    std::string format_badge;   // "[NSP]", "[XCI]", "[NRO]"
    std::string playtime_str;   // "Played 2h 15m" or "First Played Today"
    std::string optimizer_tag;  // "FSR 2.0 • 4x MSAA • 60 FPS"
    size_t file_size{0};
    u64 title_id{0};
};

struct FileEntry {
    std::string name;
    std::string full_path;
    bool is_directory{false};
    bool is_rom{false};
    std::string format_badge; // "[DIR]", "[NSP]", "[XCI]", "[NRO]", "[NCA]"
    size_t file_size{0};
};

class XboxFrontend {
public:
    XboxFrontend(core::filesystem::VirtualFileSystem& vfs, core::config::ConfigManager& config);
    ~XboxFrontend() = default;

    /// Refresh and scan available games from sdmc:/, romfs:/, and packages
    void RefreshLibrary();

    /// Refresh and list directory contents for the in-app File Manager
    void RefreshFileManager(std::string_view dir_path = "sdmc:/");

    /// Process Xbox gamepad navigation input
    void ProcessInput(const core::hid::XboxGamepadState& input, core::hid::XboxControllerDriver* driver = nullptr);

    /// Render Eden / Switch UI frame
    void Render(core::gpu::IGpuBackend& gpu);

    [[nodiscard]] FrontendTab GetCurrentTab() const noexcept { return current_tab_; }
    [[nodiscard]] size_t GetSelectedGameIndex() const noexcept { return selected_game_index_; }
    [[nodiscard]] size_t GetSelectedSettingRow() const noexcept { return selected_setting_row_; }
    [[nodiscard]] const std::vector<GameEntry>& GetLibrary() const noexcept { return library_; }

    [[nodiscard]] std::string_view GetCurrentDirectory() const noexcept { return current_dir_path_; }
    [[nodiscard]] const std::vector<FileEntry>& GetDirectoryEntries() const noexcept { return dir_entries_; }
    [[nodiscard]] size_t GetSelectedFileIndex() const noexcept { return selected_file_index_; }

    [[nodiscard]] bool IsGameOptionsOpen() const noexcept { return show_game_options_; }
    [[nodiscard]] size_t GetGameOptionsRow() const noexcept { return game_options_row_; }

    /// Returns path of selected title if launch requested
    [[nodiscard]] std::optional<std::string> ConsumeLaunchRequest();

    /// Switch active tab
    void SetTab(FrontendTab tab) noexcept { current_tab_ = tab; }

    /// Trigger gamepad vibration test
    void TriggerRumbleTest(core::hid::XboxControllerDriver* driver);

    /// Get current status header strings (clock, user, mode, battery)
    [[nodiscard]] std::string GetSystemClockString() const;
    [[nodiscard]] std::string GetProfileName() const { return profile_name_; }
    [[nodiscard]] std::string GetConsoleModeString() const;

private:
    void HandleLibraryInput(const core::hid::XboxGamepadState& input, bool pressed_left, bool pressed_right, bool pressed_a, bool pressed_start);
    void HandleFileManagerInput(const core::hid::XboxGamepadState& input, bool pressed_up, bool pressed_down, bool pressed_a, bool pressed_b, bool pressed_x);
    void HandleOptimizersInput(const core::hid::XboxGamepadState& input, bool pressed_up, bool pressed_down, bool pressed_left, bool pressed_right, bool pressed_a);
    void HandleControllersInput(const core::hid::XboxGamepadState& input, bool pressed_up, bool pressed_down, bool pressed_left, bool pressed_right, bool pressed_a, core::hid::XboxControllerDriver* driver);
    void HandleSystemInput(const core::hid::XboxGamepadState& input, bool pressed_up, bool pressed_down, bool pressed_left, bool pressed_right, bool pressed_a);
    void HandleGameOptionsInput(const core::hid::XboxGamepadState& input, bool pressed_up, bool pressed_down, bool pressed_left, bool pressed_right, bool pressed_a, bool pressed_b);

    core::filesystem::VirtualFileSystem& vfs_;
    core::config::ConfigManager& config_;

    FrontendTab current_tab_{FrontendTab::Library};
    std::vector<GameEntry> library_;
    size_t selected_game_index_{0};
    size_t selected_setting_row_{0};
    std::optional<std::string> launch_requested_;

    // File Manager state
    std::string current_dir_path_{"sdmc:/"};
    std::vector<FileEntry> dir_entries_;
    size_t selected_file_index_{0};

    // Game Options modal overlay state
    bool show_game_options_{false};
    size_t game_options_row_{0};

    std::string profile_name_{"Player 1 (Xbox Full Trust)"};

    // Controller edge detection
    bool prev_dpad_up_{false};
    bool prev_dpad_down_{false};
    bool prev_dpad_left_{false};
    bool prev_dpad_right_{false};
    bool prev_btn_a_{false};
    bool prev_btn_b_{false};
    bool prev_btn_x_{false};
    bool prev_btn_y_{false};
    bool prev_btn_lb_{false};
    bool prev_btn_rb_{false};
    bool prev_btn_start_{false};

    // Stick repeat timers
    s32 prev_stick_x_{0};
    s32 prev_stick_y_{0};

    // Frame timing & animations
    u64 ui_frame_count_{0};
    float focus_animation_timer_{0.0f};
};

} // namespace nemu::frontend
