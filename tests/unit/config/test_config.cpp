#include "core/config/config_manager.hpp"
#include "core/filesystem/vfs.hpp"
#include <iostream>
#include <filesystem>
#include <cstdlib>

#define NEMU_TEST_ASSERT(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "Assertion failed: " #cond " (" << (msg) << ") at " \
                      << __FILE__ << ":" << __LINE__ << std::endl; \
            std::exit(1); \
        } \
    } while (0)

using namespace nemu;
using namespace nemu::core;

int main() {
    std::cout << "[Test: ConfigManager Serialization & Parsing]" << std::endl;

    const std::filesystem::path test_dir = std::filesystem::current_path() / "test_config_sandbox";
    std::error_code ec;
    std::filesystem::remove_all(test_dir, ec);
    std::filesystem::create_directories(test_dir, ec);

    filesystem::VirtualFileSystem vfs;
    NEMU_TEST_ASSERT(vfs.Mount("save:/", test_dir, false), "Mount save:/ to config test sandbox");

    // Test 1: Defaults
    {
        config::ConfigManager cfg_mgr(vfs);
        const auto& cfg = cfg_mgr.GetConfig();
        NEMU_TEST_ASSERT(cfg.render_width == 1280, "Default render width 1280");
        NEMU_TEST_ASSERT(cfg.render_height == 720, "Default render height 720");
        NEMU_TEST_ASSERT(cfg.vsync == true, "Default VSync true");
        NEMU_TEST_ASSERT(cfg.audio_volume == 100, "Default audio volume 100");
        NEMU_TEST_ASSERT(cfg.audio_enabled == true, "Default audio enabled true");
        NEMU_TEST_ASSERT(cfg.button_layout == hid::FaceButtonLayout::NintendoStandard, "Default layout Nintendo");
        NEMU_TEST_ASSERT(cfg.cpu_backend == config::CpuBackendMode::Jit, "Default CPU backend JIT");
        std::cout << "  - Default configuration parameters: PASSED" << std::endl;
    }

    // Test 2: Modify, Save, and Reload
    {
        config::ConfigManager cfg_mgr(vfs);
        auto& cfg = cfg_mgr.GetConfig();
        cfg.render_width = 1920;
        cfg.render_height = 1080;
        cfg.vsync = false;
        cfg.audio_volume = 80;
        cfg.audio_enabled = false;
        cfg.button_layout = hid::FaceButtonLayout::XboxMirrored;
        cfg.inner_deadzone = 0.20f;
        cfg.outer_deadzone = 0.90f;
        cfg.cpu_backend = config::CpuBackendMode::Interpreter;
        cfg.resolution_scale = config::ResolutionScale::SeriesX_1_5x;
        cfg.upscaler = gpu::pipeline::UpscalerMode::FSR_2_0;
        cfg.anti_aliasing = gpu::pipeline::AntiAliasingMode::MSAA_4x;
        cfg.frame_generation = gpu::pipeline::FrameGenMode::AFMF_Extrapolation_2x;
        cfg.console_mode = config::ConsoleMode::Docked;

        NEMU_TEST_ASSERT(cfg_mgr.Save("save:/custom_config.ini"), "Save custom configuration");

        // Load into new instance
        config::ConfigManager loaded_mgr(vfs);
        NEMU_TEST_ASSERT(loaded_mgr.Load("save:/custom_config.ini"), "Load custom configuration");

        const auto& loaded = loaded_mgr.GetConfig();
        NEMU_TEST_ASSERT(loaded.render_width == 1920, "Loaded width 1920");
        NEMU_TEST_ASSERT(loaded.render_height == 1080, "Loaded height 1080");
        NEMU_TEST_ASSERT(loaded.vsync == false, "Loaded VSync false");
        NEMU_TEST_ASSERT(loaded.audio_volume == 80, "Loaded volume 80");
        NEMU_TEST_ASSERT(loaded.audio_enabled == false, "Loaded audio false");
        NEMU_TEST_ASSERT(loaded.button_layout == hid::FaceButtonLayout::XboxMirrored, "Loaded layout XboxMirrored");
        NEMU_TEST_ASSERT(loaded.inner_deadzone > 0.19f && loaded.inner_deadzone < 0.21f, "Loaded inner deadzone");
        NEMU_TEST_ASSERT(loaded.outer_deadzone > 0.89f && loaded.outer_deadzone < 0.91f, "Loaded outer deadzone");
        NEMU_TEST_ASSERT(loaded.cpu_backend == config::CpuBackendMode::Interpreter, "Loaded backend Interpreter");
        NEMU_TEST_ASSERT(loaded.resolution_scale == config::ResolutionScale::SeriesX_1_5x, "Loaded resolution scale");
        NEMU_TEST_ASSERT(loaded.upscaler == gpu::pipeline::UpscalerMode::FSR_2_0, "Loaded FSR 2.0");
        NEMU_TEST_ASSERT(loaded.anti_aliasing == gpu::pipeline::AntiAliasingMode::MSAA_4x, "Loaded 4x MSAA");
        NEMU_TEST_ASSERT(loaded.frame_generation == gpu::pipeline::FrameGenMode::AFMF_Extrapolation_2x, "Loaded Frame Gen");
        std::cout << "  - Custom configuration serialization & reload: PASSED" << std::endl;
    }

    // Test 3: INI format resilience (whitespace, comments, empty lines)
    {
        const std::string raw_ini =
            "# This is a comment\n"
            "; Another comment\n"
            "\n"
            "  render_width = 3840  \n"
            "render_height=2160\n"
            "vsync=1\n"
            "cpu_backend=JIT\n"
            "unknown_key=ignored_value\n";

        std::span<const u8> span(reinterpret_cast<const u8*>(raw_ini.data()), raw_ini.size());
        NEMU_TEST_ASSERT(vfs.WriteFile("save:/noisy.ini", span), "Write noisy INI file");

        config::ConfigManager noisy_mgr(vfs);
        NEMU_TEST_ASSERT(noisy_mgr.Load("save:/noisy.ini"), "Load noisy INI file");

        const auto& loaded = noisy_mgr.GetConfig();
        NEMU_TEST_ASSERT(loaded.render_width == 3840, "Parsed noisy width 3840");
        NEMU_TEST_ASSERT(loaded.render_height == 2160, "Parsed noisy height 2160");
        NEMU_TEST_ASSERT(loaded.vsync == true, "Parsed noisy VSync true");
        NEMU_TEST_ASSERT(loaded.cpu_backend == config::CpuBackendMode::Jit, "Parsed noisy JIT");
        std::cout << "  - INI parser comment/whitespace resilience: PASSED" << std::endl;
    }

    // Test 4: Per-Game Configuration overrides
    {
        config::ConfigManager mgr(vfs);
        constexpr u64 TITLE_ZELDA = 0x0100000000010000ULL;

        config::PerGameConfig game_cfg{
            .has_custom_settings = true,
            .resolution_scale = config::ResolutionScale::Ultra4K_2_0x,
            .upscaler = gpu::pipeline::UpscalerMode::FSR_1_0,
            .fsr_sharpness = 0.90f,
            .anti_aliasing = gpu::pipeline::AntiAliasingMode::MSAA_8x,
            .frame_generation = gpu::pipeline::FrameGenMode::AFMF_Extrapolation_2x,
            .cpu_backend = config::CpuBackendMode::Jit,
            .button_layout = hid::FaceButtonLayout::XboxMirrored
        };

        NEMU_TEST_ASSERT(mgr.SaveGameConfig(TITLE_ZELDA, game_cfg), "Save per-game config");

        config::PerGameConfig loaded_cfg{};
        NEMU_TEST_ASSERT(mgr.LoadGameConfig(TITLE_ZELDA, loaded_cfg), "Load per-game config");
        NEMU_TEST_ASSERT(loaded_cfg.has_custom_settings, "Custom settings enabled");
        NEMU_TEST_ASSERT(loaded_cfg.resolution_scale == config::ResolutionScale::Ultra4K_2_0x, "Loaded custom 4K resolution");
        NEMU_TEST_ASSERT(loaded_cfg.upscaler == gpu::pipeline::UpscalerMode::FSR_1_0, "Loaded custom FSR 1.0");

        auto effective = mgr.GetEffectiveConfigForTitle(TITLE_ZELDA);
        NEMU_TEST_ASSERT(effective.resolution_scale == config::ResolutionScale::Ultra4K_2_0x, "Effective 4K applied");
        NEMU_TEST_ASSERT(effective.upscaler == gpu::pipeline::UpscalerMode::FSR_1_0, "Effective FSR 1.0 applied");
        NEMU_TEST_ASSERT(effective.button_layout == hid::FaceButtonLayout::XboxMirrored, "Effective layout applied");
        std::cout << "  - Per-Game custom configuration overrides: PASSED" << std::endl;
    }

    // Clean up
    std::filesystem::remove_all(test_dir, ec);

    std::cout << "[Test: ConfigManager Serialization & Parsing PASSED]" << std::endl;
    return 0;
}
