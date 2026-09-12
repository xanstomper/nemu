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
    Optimizers = 1,  // FSR Upscalers, MSAA, Frame Gen, Resolution Scale
    Controllers = 2, // Pro Controller, Joy-Con, Deadzones, HD Rumble
    System = 3,      // Docked/Handheld mode, Language, Audio
    Diagnostics = 4  // JIT stats, GPU stats, Fastmem VEH fault telemetry
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

class XboxFrontend {
public:
    XboxFrontend(core::filesystem::VirtualFileSystem& vfs, core::config::ConfigManager& config);
    ~XboxFrontend() = default;

    /// Refresh and scan available games from sdmc:/, romfs:/, and packages
    void RefreshLibrary();

    /// Process Xbox gamepad navigation input
    void ProcessInput(const core::hid::XboxGamepadState& input, core::hid::XboxControllerDriver* driver = nullptr);

    /// Render Eden / Switch UI frame
    void Render(core::gpu::IGpuBackend& gpu);

    [[nodiscard]] FrontendTab GetCurrentTab() const noexcept { return current_tab_; }
    [[nodiscard]] size_t GetSelectedGameIndex() const noexcept { return selected_game_index_; }
    [[nodiscard]] size_t GetSelectedSettingRow() const noexcept { return selected_setting_row_; }
    [[nodiscard]] const std::vector<GameEntry>& GetLibrary() const noexcept { return library_; }

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
    void HandleLibraryInput(const core::hid::XboxGamepadState& input, bool pressed_left, bool pressed_right, bool pressed_a);
    void HandleOptimizersInput(const core::hid::XboxGamepadState& input, bool pressed_up, bool pressed_down, bool pressed_left, bool pressed_right, bool pressed_a);
    void HandleControllersInput(const core::hid::XboxGamepadState& input, bool pressed_up, bool pressed_down, bool pressed_left, bool pressed_right, bool pressed_a, core::hid::XboxControllerDriver* driver);
    void HandleSystemInput(const core::hid::XboxGamepadState& input, bool pressed_up, bool pressed_down, bool pressed_left, bool pressed_right, bool pressed_a);

    core::filesystem::VirtualFileSystem& vfs_;
    core::config::ConfigManager& config_;

    FrontendTab current_tab_{FrontendTab::Library};
    std::vector<GameEntry> library_;
    size_t selected_game_index_{0};
    size_t selected_setting_row_{0};
    std::optional<std::string> launch_requested_;

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
