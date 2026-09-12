#pragma once

#include "core/types.hpp"
#include "core/filesystem/vfs.hpp"
#include "core/hid/controller_mapping.hpp"
#include "core/gpu/pipeline/graphics_optimizer.hpp"
#include <string>
#include <string_view>

namespace nemu::core::config {

enum class CpuBackendMode : u32 {
    Interpreter = 0,
    Jit = 1
};

enum class ResolutionScale : u32 {
    Handheld_0_5x = 0, // 540p Performance
    SeriesS_0_75x = 1, // 720p Balanced
    Native_1_0x   = 2, // 1080p Native Docked
    SeriesX_1_5x  = 3, // 1440p Series X Enhanced
    Ultra4K_2_0x  = 4, // 2160p 4K Ultra
};

enum class ConsoleMode : u32 {
    Docked = 0,
    Handheld = 1,
};

enum class ControllerType : u32 {
    ProController = 0,
    JoyConDual = 1,
    Handheld = 2,
};

enum class SystemLanguage : u32 {
    English = 0,
    Japanese = 1,
    French = 2,
    German = 3,
    Spanish = 4,
    Italian = 5,
};

struct EmulatorConfig {
    // Graphics & Optimizers
    u32 render_width{1280};
    u32 render_height{720};
    ResolutionScale resolution_scale{ResolutionScale::Native_1_0x};
    gpu::pipeline::UpscalerMode upscaler{gpu::pipeline::UpscalerMode::FSR_2_0};
    float fsr_sharpness{0.8f};
    gpu::pipeline::AntiAliasingMode anti_aliasing{gpu::pipeline::AntiAliasingMode::MSAA_4x};
    gpu::pipeline::FrameGenMode frame_generation{gpu::pipeline::FrameGenMode::AFMF_Extrapolation_2x};
    bool vsync{true};
    ConsoleMode console_mode{ConsoleMode::Docked};

    // Audio Settings
    u32 audio_volume{100};
    bool audio_enabled{true};
    bool surround_enabled{false};

    // Controller Emulation Settings
    ControllerType controller_type{ControllerType::ProController};
    hid::FaceButtonLayout button_layout{hid::FaceButtonLayout::NintendoStandard};
    float inner_deadzone{0.15f};
    float outer_deadzone{0.95f};
    bool vibration_enabled{true};
    float vibration_strength{1.0f};

    // CPU / Core Optimizers
    CpuBackendMode cpu_backend{CpuBackendMode::Jit};
    bool fastmem_enabled{true};
    bool multithreaded_cpu{true};

    // System Settings
    SystemLanguage system_language{SystemLanguage::English};
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

    static std::string_view GetResolutionScaleName(ResolutionScale scale) noexcept;
    static std::string_view GetConsoleModeName(ConsoleMode mode) noexcept;
    static std::string_view GetControllerTypeName(ControllerType type) noexcept;
    static std::string_view GetSystemLanguageName(SystemLanguage lang) noexcept;

private:
    filesystem::VirtualFileSystem& vfs_;
    EmulatorConfig config_{};
};

} // namespace nemu::core::config
