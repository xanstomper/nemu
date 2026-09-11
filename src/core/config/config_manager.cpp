#include "config_manager.hpp"
#include "platform/logger.hpp"
#include <sstream>

namespace nemu::core::config {

ConfigManager::ConfigManager(filesystem::VirtualFileSystem& vfs)
    : vfs_(vfs) {
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

        // Trim whitespace
        while (!key.empty() && std::isspace(key.back())) key.pop_back();
        while (!key.empty() && std::isspace(key.front())) key.erase(key.begin());
        while (!val.empty() && std::isspace(val.back())) val.pop_back();
        while (!val.empty() && std::isspace(val.front())) val.erase(val.begin());

        if (key == "render_width") config_.render_width = static_cast<u32>(std::stoul(val));
        else if (key == "render_height") config_.render_height = static_cast<u32>(std::stoul(val));
        else if (key == "vsync") config_.vsync = (val == "true" || val == "1");
        else if (key == "audio_volume") config_.audio_volume = static_cast<u32>(std::stoul(val));
        else if (key == "audio_enabled") config_.audio_enabled = (val == "true" || val == "1");
        else if (key == "button_layout") config_.button_layout = (val == "XboxMirrored") ? hid::FaceButtonLayout::XboxMirrored : hid::FaceButtonLayout::NintendoStandard;
        else if (key == "inner_deadzone") config_.inner_deadzone = std::stof(val);
        else if (key == "outer_deadzone") config_.outer_deadzone = std::stof(val);
        else if (key == "cpu_backend") config_.cpu_backend = (val == "Interpreter") ? CpuBackendMode::Interpreter : CpuBackendMode::Jit;
    }

    NEMU_LOG_INFO("Config", "Loaded configuration from '{}' (resolution: {}x{}, backend: {})",
        config_path, config_.render_width, config_.render_height,
        config_.cpu_backend == CpuBackendMode::Jit ? "JIT" : "Interpreter");
    return true;
}

bool ConfigManager::Save(std::string_view config_path) {
    std::ostringstream ss;
    ss << "# Nemu Switch Emulator Configuration\n";
    ss << "render_width=" << config_.render_width << "\n";
    ss << "render_height=" << config_.render_height << "\n";
    ss << "vsync=" << (config_.vsync ? "true" : "false") << "\n";
    ss << "audio_volume=" << config_.audio_volume << "\n";
    ss << "audio_enabled=" << (config_.audio_enabled ? "true" : "false") << "\n";
    ss << "button_layout=" << (config_.button_layout == hid::FaceButtonLayout::XboxMirrored ? "XboxMirrored" : "NintendoStandard") << "\n";
    ss << "inner_deadzone=" << config_.inner_deadzone << "\n";
    ss << "outer_deadzone=" << config_.outer_deadzone << "\n";
    ss << "cpu_backend=" << (config_.cpu_backend == CpuBackendMode::Interpreter ? "Interpreter" : "JIT") << "\n";

    const std::string content = ss.str();
    std::span<const u8> data(reinterpret_cast<const u8*>(content.data()), content.size());
    return vfs_.WriteFile(config_path, data);
}

} // namespace nemu::core::config
