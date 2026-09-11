#pragma once

#include "core/types.hpp"
#include "core/filesystem/vfs.hpp"
#include "core/hid/controller_mapping.hpp"
#include <string>

namespace nemu::core::config {

enum class CpuBackendMode : u32 {
    Interpreter = 0,
    Jit = 1
};

struct EmulatorConfig {
    // Graphics
    u32 render_width{1280};
    u32 render_height{720};
    bool vsync{true};

    // Audio
    u32 audio_volume{100};
    bool audio_enabled{true};

    // Input
    hid::FaceButtonLayout button_layout{hid::FaceButtonLayout::NintendoStandard};
    float inner_deadzone{0.15f};
    float outer_deadzone{0.95f};

    // CPU / Core
    CpuBackendMode cpu_backend{CpuBackendMode::Jit};
};

class ConfigManager {
public:
    explicit ConfigManager(filesystem::VirtualFileSystem& vfs);
    ~ConfigManager() = default;

    /// Load config from VFS (falls back to defaults if not found)
    bool Load(std::string_view config_path = "save:/config.ini");

    /// Save config to VFS
    bool Save(std::string_view config_path = "save:/config.ini");

    [[nodiscard]] EmulatorConfig& GetConfig() noexcept { return config_; }
    [[nodiscard]] const EmulatorConfig& GetConfig() const noexcept { return config_; }

private:
    filesystem::VirtualFileSystem& vfs_;
    EmulatorConfig config_{};
};

} // namespace nemu::core::config
