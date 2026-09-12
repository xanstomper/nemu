#include "config_manager.hpp"
#include "platform/logger.hpp"
#include <sstream>

namespace nemu::core::config {

ConfigManager::ConfigManager(filesystem::VirtualFileSystem& vfs)
    : vfs_(vfs) {
}

std::string_view ConfigManager::GetResolutionScaleName(ResolutionScale scale) noexcept {
    switch (scale) {
        case ResolutionScale::Handheld_0_5x: return "0.5x (540p Performance)";
        case ResolutionScale::SeriesS_0_75x: return "0.75x (720p Balanced)";
        case ResolutionScale::Native_1_0x:   return "1.0x (1080p Native Docked)";
        case ResolutionScale::SeriesX_1_5x:  return "1.5x (1440p 2K Enhanced)";
        case ResolutionScale::Ultra4K_2_0x:  return "2.0x (2160p 4K Ultra)";
        default:                             return "1.0x (Native)";
    }
}

std::string_view ConfigManager::GetConsoleModeName(ConsoleMode mode) noexcept {
    switch (mode) {
        case ConsoleMode::Docked:   return "Docked (1080p High Performance)";
        case ConsoleMode::Handheld: return "Handheld (720p Energy Saver)";
        default:                    return "Docked";
    }
}

std::string_view ConfigManager::GetControllerTypeName(ControllerType type) noexcept {
    switch (type) {
        case ControllerType::ProController: return "Nintendo Switch Pro Controller";
        case ControllerType::JoyConDual:    return "Dual Joy-Con Grip";
        case ControllerType::Handheld:      return "Handheld Built-in Controls";
        default:                            return "Pro Controller";
    }
}

std::string_view ConfigManager::GetSystemLanguageName(SystemLanguage lang) noexcept {
    switch (lang) {
        case SystemLanguage::English:  return "English (US/UK)";
        case SystemLanguage::Japanese: return "Japanese (日本語)";
        case SystemLanguage::French:   return "French (Français)";
        case SystemLanguage::German:   return "German (Deutsch)";
        case SystemLanguage::Spanish:  return "Spanish (Español)";
        case SystemLanguage::Italian:  return "Italian (Italiano)";
        default:                       return "English";
    }
}

bool ConfigManager::Load(std::string_view config_path) {
    auto data_opt = vfs_.ReadFile(config_path);
    if (!data_opt) {
        NEMU_LOG_INFO("Config", "No config found at '{}'; using default settings", config_path);
        return false;
    }

    std::string content(reinterpret_cast<const char*>(data_opt->data()), data_opt->size());
    std::istringstream stream(content);
    std::string line;

    while (std::getline(stream, line)) {
        if (line.empty() || line[0] == '#' || line[0] == ';') continue;

        const size_t eq_pos = line.find('=');
        if (eq_pos == std::string::npos) continue;

        std::string key = line.substr(0, eq_pos);
        std::string val = line.substr(eq_pos + 1);

        while (!key.empty() && std::isspace(key.back())) key.pop_back();
        while (!key.empty() && std::isspace(key.front())) key.erase(key.begin());
        while (!val.empty() && std::isspace(val.back())) val.pop_back();
        while (!val.empty() && std::isspace(val.front())) val.erase(val.begin());

        if (key == "render_width") config_.render_width = static_cast<u32>(std::stoul(val));
        else if (key == "render_height") config_.render_height = static_cast<u32>(std::stoul(val));
        else if (key == "vsync") config_.vsync = (val == "true" || val == "1");
        else if (key == "resolution_scale") config_.resolution_scale = static_cast<ResolutionScale>(std::stoul(val));
        else if (key == "upscaler") config_.upscaler = static_cast<gpu::pipeline::UpscalerMode>(std::stoul(val));
        else if (key == "fsr_sharpness") config_.fsr_sharpness = std::stof(val);
        else if (key == "anti_aliasing") config_.anti_aliasing = static_cast<gpu::pipeline::AntiAliasingMode>(std::stoul(val));
        else if (key == "frame_generation") config_.frame_generation = static_cast<gpu::pipeline::FrameGenMode>(std::stoul(val));
        else if (key == "console_mode") config_.console_mode = (val == "Handheld") ? ConsoleMode::Handheld : ConsoleMode::Docked;
        else if (key == "audio_volume") config_.audio_volume = static_cast<u32>(std::stoul(val));
        else if (key == "audio_enabled") config_.audio_enabled = (val == "true" || val == "1");
        else if (key == "surround_enabled") config_.surround_enabled = (val == "true" || val == "1");
        else if (key == "controller_type") config_.controller_type = static_cast<ControllerType>(std::stoul(val));
        else if (key == "button_layout") config_.button_layout = (val == "XboxMirrored") ? hid::FaceButtonLayout::XboxMirrored : hid::FaceButtonLayout::NintendoStandard;
        else if (key == "inner_deadzone") config_.inner_deadzone = std::stof(val);
        else if (key == "outer_deadzone") config_.outer_deadzone = std::stof(val);
        else if (key == "vibration_enabled") config_.vibration_enabled = (val == "true" || val == "1");
        else if (key == "vibration_strength") config_.vibration_strength = std::stof(val);
        else if (key == "cpu_backend") config_.cpu_backend = (val == "Interpreter") ? CpuBackendMode::Interpreter : CpuBackendMode::Jit;
        else if (key == "fastmem_enabled") config_.fastmem_enabled = (val == "true" || val == "1");
        else if (key == "multithreaded_cpu") config_.multithreaded_cpu = (val == "true" || val == "1");
        else if (key == "system_language") config_.system_language = static_cast<SystemLanguage>(std::stoul(val));
    }

    NEMU_LOG_INFO("Config", "Loaded configuration from '{}' (resolution: {}x{}, upscaler: {}, AA: {}, JIT: {})",
        config_path, config_.render_width, config_.render_height,
        gpu::pipeline::GraphicsOptimizer::GetUpscalerName(config_.upscaler),
        gpu::pipeline::GraphicsOptimizer::GetAntiAliasingName(config_.anti_aliasing),
        config_.cpu_backend == CpuBackendMode::Jit ? "JIT" : "Interpreter");
    return true;
}

bool ConfigManager::Save(std::string_view config_path) {
    std::ostringstream ss;
    ss << "# Nemu Switch Emulator Configuration\n";
    ss << "render_width=" << config_.render_width << "\n";
    ss << "render_height=" << config_.render_height << "\n";
    ss << "vsync=" << (config_.vsync ? "true" : "false") << "\n";
    ss << "resolution_scale=" << static_cast<u32>(config_.resolution_scale) << "\n";
    ss << "upscaler=" << static_cast<u32>(config_.upscaler) << "\n";
    ss << "fsr_sharpness=" << config_.fsr_sharpness << "\n";
    ss << "anti_aliasing=" << static_cast<u32>(config_.anti_aliasing) << "\n";
    ss << "frame_generation=" << static_cast<u32>(config_.frame_generation) << "\n";
    ss << "console_mode=" << (config_.console_mode == ConsoleMode::Handheld ? "Handheld" : "Docked") << "\n";
    ss << "audio_volume=" << config_.audio_volume << "\n";
    ss << "audio_enabled=" << (config_.audio_enabled ? "true" : "false") << "\n";
    ss << "surround_enabled=" << (config_.surround_enabled ? "true" : "false") << "\n";
    ss << "controller_type=" << static_cast<u32>(config_.controller_type) << "\n";
    ss << "button_layout=" << (config_.button_layout == hid::FaceButtonLayout::XboxMirrored ? "XboxMirrored" : "NintendoStandard") << "\n";
    ss << "inner_deadzone=" << config_.inner_deadzone << "\n";
    ss << "outer_deadzone=" << config_.outer_deadzone << "\n";
    ss << "vibration_enabled=" << (config_.vibration_enabled ? "true" : "false") << "\n";
    ss << "vibration_strength=" << config_.vibration_strength << "\n";
    ss << "cpu_backend=" << (config_.cpu_backend == CpuBackendMode::Interpreter ? "Interpreter" : "JIT") << "\n";
    ss << "fastmem_enabled=" << (config_.fastmem_enabled ? "true" : "false") << "\n";
    ss << "multithreaded_cpu=" << (config_.multithreaded_cpu ? "true" : "false") << "\n";
    ss << "system_language=" << static_cast<u32>(config_.system_language) << "\n";

    const std::string content = ss.str();
    std::span<const u8> data(reinterpret_cast<const u8*>(content.data()), content.size());
    return vfs_.WriteFile(config_path, data);
}

} // namespace nemu::core::config
