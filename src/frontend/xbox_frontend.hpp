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

enum class ActiveSubView : u32 {
    None = 0,
    NSO = 1,
    News = 2,
    EShop = 3,
    Album = 4,
    Controllers = 5,
    SystemSettings = 6,
    PowerMenu = 7,
    GameOptions = 8,
    UserProfile = 9
};

struct GameEntry {
    std::string title;
    std::string filename;
    std::string virtual_path;
    std::string format_badge;   // "[NSP]", "[XCI]", "[NRO]"
    std::string playtime_str;   // "Played 2h 15m" or "First Played Today"
    std::string optimizer_tag;  // "FSR 2.0 • 4x MSAA • 60 FPS"
    std::string cover_host_path; // host path to cover art (jpg/png), empty = placeholder tile
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

    /// Process mouse / touch pointer input (hovering, clicking, wheel scrolling)
    void ProcessPointer(float mouse_x, float mouse_y, bool left_down, bool left_click, bool right_click, float wheel_delta);

    [[nodiscard]] ActiveSubView GetActiveSubView() const noexcept { return active_subview_; }
    void SetActiveSubView(ActiveSubView view) noexcept { active_subview_ = view; }
    [[nodiscard]] bool ConsumeExitRequested() noexcept {
        bool r = exit_requested_;
        exit_requested_ = false;
        return r;
    }

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

    /// RetroArch-style recursive directory scanner
    void ScanDirectory(std::string_view dir_path);

    /// Playlist persistence (saves/loads scanned game entries across sessions)
    void SavePlaylist();
    void LoadPlaylist();

    /// Add a game entry directly to library
    void AddGameToLibrary(const GameEntry& entry);

    /// In-game RetroArch Quick Menu
    [[nodiscard]] bool IsQuickMenuOpen() const noexcept { return show_quick_menu_; }
    void SetQuickMenuOpen(bool open) noexcept { show_quick_menu_ = open; }
    [[nodiscard]] size_t GetQuickMenuRow() const noexcept { return quick_menu_row_; }
    [[nodiscard]] u32 GetStateSlot() const noexcept { return current_state_slot_; }

    [[nodiscard]] bool ConsumeRestartRequested() noexcept {
        bool r = restart_requested_;
        restart_requested_ = false;
        return r;
    }
    [[nodiscard]] bool ConsumeCloseGameRequested() noexcept {
        bool r = close_game_requested_;
        close_game_requested_ = false;
        return r;
    }
    [[nodiscard]] bool ConsumeSaveStateRequested() noexcept {
        bool r = save_state_requested_;
        save_state_requested_ = false;
        return r;
    }
    [[nodiscard]] bool ConsumeLoadStateRequested() noexcept {
        bool r = load_state_requested_;
        load_state_requested_ = false;
        return r;
    }

    /// Process in-game input (handles Quick Menu toggle and navigation)
    bool ProcessInGameInput(const core::hid::XboxGamepadState& input);

    /// Render RetroArch Quick Menu overlay
    void RenderQuickMenu(core::gpu::IGpuBackend& gpu);

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
    [[nodiscard]] std::string_view GetToastMessage() const noexcept { return toast_message_; }

private:
    void HandleLibraryInput(const core::hid::XboxGamepadState& input, bool pressed_up, bool pressed_down, bool pressed_left, bool pressed_right, bool pressed_a, bool pressed_b, bool pressed_start, bool pressed_y);
    void HandleFileManagerInput(const core::hid::XboxGamepadState& input, bool pressed_up, bool pressed_down, bool pressed_a, bool pressed_b, bool pressed_x, bool pressed_y);
    void HandleOptimizersInput(const core::hid::XboxGamepadState& input, bool pressed_up, bool pressed_down, bool pressed_left, bool pressed_right, bool pressed_a);
    void HandleControllersInput(const core::hid::XboxGamepadState& input, bool pressed_up, bool pressed_down, bool pressed_left, bool pressed_right, bool pressed_a, core::hid::XboxControllerDriver* driver);
    void HandleSystemInput(const core::hid::XboxGamepadState& input, bool pressed_up, bool pressed_down, bool pressed_left, bool pressed_right, bool pressed_a);
    void HandleGameOptionsInput(const core::hid::XboxGamepadState& input, bool pressed_up, bool pressed_down, bool pressed_left, bool pressed_right, bool pressed_a, bool pressed_b);
    void HandleQuickMenuInput(const core::hid::XboxGamepadState& input, bool pressed_up, bool pressed_down, bool pressed_left, bool pressed_right, bool pressed_a, bool pressed_b);

    void ScanDirectoryRecursive(const std::filesystem::path& host_path, std::string_view vpath_prefix);
    void ShowToast(std::string message);

    void BuildUiGeometry(std::vector<core::gpu::RasterVertex>& out, core::gpu::IGpuBackend* gpu);
    void BuildQuickMenuGeometry(std::vector<core::gpu::RasterVertex>& out);

    // Nintendo Switch HOME view (avatar/clock/battery header, game tile row,
    // bottom shortcut bar)
    void DrawSwitchHomeChrome(std::vector<core::gpu::RasterVertex>& out, core::gpu::IGpuBackend* gpu, bool draw_shortcuts);
    void DrawSwitchHomeView(std::vector<core::gpu::RasterVertex>& out, core::gpu::IGpuBackend* gpu);

    // Nintendo Switch Sub-views & Modals
    void DrawSwitchSettings(std::vector<core::gpu::RasterVertex>& out, core::gpu::IGpuBackend* gpu);
    void DrawSwitchControllers(std::vector<core::gpu::RasterVertex>& out, core::gpu::IGpuBackend* gpu);
    void DrawSwitchPowerMenu(std::vector<core::gpu::RasterVertex>& out, core::gpu::IGpuBackend* gpu);
    void DrawSwitchGameOptions(std::vector<core::gpu::RasterVertex>& out, core::gpu::IGpuBackend* gpu);
    void DrawSwitchNso(std::vector<core::gpu::RasterVertex>& out, core::gpu::IGpuBackend* gpu);
    void DrawSwitchNews(std::vector<core::gpu::RasterVertex>& out, core::gpu::IGpuBackend* gpu);
    void DrawSwitchEShop(std::vector<core::gpu::RasterVertex>& out, core::gpu::IGpuBackend* gpu);
    void DrawSwitchAlbum(std::vector<core::gpu::RasterVertex>& out, core::gpu::IGpuBackend* gpu);
    void DrawSwitchProfile(std::vector<core::gpu::RasterVertex>& out, core::gpu::IGpuBackend* gpu);

    void HandleSettingsInput(const core::hid::XboxGamepadState& input, bool pressed_up, bool pressed_down, bool pressed_left, bool pressed_right, bool pressed_a, bool pressed_b);
    void HandleControllersSubInput(const core::hid::XboxGamepadState& input, bool pressed_up, bool pressed_down, bool pressed_left, bool pressed_right, bool pressed_a, bool pressed_b, core::hid::XboxControllerDriver* driver);
    void HandlePowerMenuInput(bool pressed_up, bool pressed_down, bool pressed_a, bool pressed_b);

    /// Resolve cover art for a library entry (rom-sidecar jpg/png, or a
    /// covers/ directory lookup by title id). Empty result = placeholder tile.
    void AttachCover(GameEntry& entry);

    core::filesystem::VirtualFileSystem& vfs_;
    core::config::ConfigManager& config_;

    ActiveSubView active_subview_{ActiveSubView::None};
    bool exit_requested_{false};

    size_t settings_category_{0};
    size_t settings_row_{0};
    size_t controllers_sub_row_{0};
    size_t power_menu_row_{0};
    size_t news_active_article_{0};
    size_t album_active_photo_{0};
    size_t profile_active_row_{0};

    float pointer_x_{0.0f};
    float pointer_y_{0.0f};
    bool pointer_active_{false};
    bool pointer_dragging_{false};
    float pointer_drag_start_x_{0.0f};
    float pointer_drag_start_offset_{0.0f};
    std::optional<size_t> hover_game_index_{std::nullopt};
    std::optional<size_t> hover_shortcut_index_{std::nullopt};
    float glow_anim_timer_{0.0f};

    FrontendTab current_tab_{FrontendTab::Library};
    std::vector<GameEntry> library_;
    size_t selected_game_index_{0};
    size_t selected_setting_row_{0};
    std::optional<std::string> launch_requested_;

    // Switch HOME view state: tile row vs. bottom shortcut bar
    bool home_in_shortcuts_{false};
    size_t home_shortcut_index_{0};
    float home_scroll_offset_{0.0f}; // animated tile-row offset (tiles)
    size_t home_last_selected_{SIZE_MAX};
    float home_title_alpha_{1.0f};   // fades in when the selection changes

    // File Manager state
    std::string current_dir_path_{"sdmc:/"};
    std::vector<FileEntry> dir_entries_;
    size_t selected_file_index_{0};

    // Game Options modal overlay state
    bool show_game_options_{false};
    size_t game_options_row_{0};

    // In-Game RetroArch Quick Menu state
    bool show_quick_menu_{false};
    size_t quick_menu_row_{0};
    u32 current_state_slot_{0};
    bool restart_requested_{false};
    bool close_game_requested_{false};
    bool save_state_requested_{false};
    bool load_state_requested_{false};

    // In-game button edge detection
    bool prev_btn_back_in_game_{false};
    bool prev_stick_l_in_game_{false};
    bool prev_stick_r_in_game_{false};
    bool prev_qm_up_{false};
    bool prev_qm_down_{false};
    bool prev_qm_left_{false};
    bool prev_qm_right_{false};
    bool prev_qm_a_{false};
    bool prev_qm_b_{false};

    std::string profile_name_{"Player 1 (Xbox Full Trust)"};
    std::string toast_message_{};
    float toast_timer_{0.0f};

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
