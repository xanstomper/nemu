#pragma once

#include "core/types.hpp"
#include "core/gpu/gpu_interface.hpp"
#include "core/hid/controller_mapping.hpp"
#include "core/filesystem/vfs.hpp"
#include "core/config/config_manager.hpp"
#include <vector>
#include <string>
#include <string_view>
#include <optional>

namespace nemu::frontend {

enum class FrontendView : u32 {
    Library = 0,
    Settings = 1,
    Diagnostics = 2
};

struct HomebrewEntry {
    std::string filename;
    std::string virtual_path;
    size_t file_size{0};
};

class XboxFrontend {
public:
    XboxFrontend(core::filesystem::VirtualFileSystem& vfs, core::config::ConfigManager& config);
    ~XboxFrontend() = default;

    /// Refresh and discover available homebrew binaries from sdmc:/
    void RefreshLibrary();

    /// Process Xbox gamepad input navigation
    void ProcessInput(const core::hid::XboxGamepadState& input);

    /// Render frontend frame through GPU backend
    void Render(core::gpu::IGpuBackend& gpu);

    [[nodiscard]] FrontendView GetCurrentView() const noexcept { return current_view_; }
    [[nodiscard]] size_t GetSelectedIndex() const noexcept { return selected_index_; }
    [[nodiscard]] const std::vector<HomebrewEntry>& GetLibrary() const noexcept { return library_; }

    /// Returns path of selected NRO if launch requested
    [[nodiscard]] std::optional<std::string> ConsumeLaunchRequest();

private:
    core::filesystem::VirtualFileSystem& vfs_;
    core::config::ConfigManager& config_;

    FrontendView current_view_{FrontendView::Library};
    std::vector<HomebrewEntry> library_;
    size_t selected_index_{0};
    std::optional<std::string> launch_requested_;

    bool prev_dpad_up_{false};
    bool prev_dpad_down_{false};
    bool prev_btn_a_{false};
    bool prev_btn_b_{false};
    bool prev_btn_x_{false};
    bool prev_btn_y_{false};
};

} // namespace nemu::frontend
