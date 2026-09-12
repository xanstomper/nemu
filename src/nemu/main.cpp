#include "platform/logger.hpp"
#include "core/system/emulator.hpp"
#include "frontend/xbox_frontend.hpp"
#include <iostream>
#include <string>
#include <chrono>
#include <thread>

using namespace nemu;
using namespace nemu::core;

int main(int argc, char** argv) {
    platform::Logger::Instance().SetMinLevel(platform::LogLevel::Info);

    NEMU_LOG_INFO("Init", "=========================================================");
    NEMU_LOG_INFO("Init", "  NEMU: Nintendo Switch Emulator for Xbox Series S/X     ");
    NEMU_LOG_INFO("Init", "  Target: Microsoft Xbox Developer Mode (UWP Full Trust) ");
    NEMU_LOG_INFO("Init", "  Milestone 10: Complete Interactive Emulation Runtime   ");
    NEMU_LOG_INFO("Init", "=========================================================");

    // Determine target title from command line if specified
    std::string target_title;
    if (argc > 1) {
        target_title = argv[1];
    }

    // Configure and initialize the unified emulator runtime
    system::EmulatorConfig emu_cfg{
        .render_width = 1280,
        .render_height = 720,
        .vsync = true,
        .audio_enabled = true,
        .jit_enabled = true,
        .sdmc_root = "./sdmc",
        .save_root = "./save",
        .title_path = target_title
    };

    system::Emulator emulator(emu_cfg);
    if (!emulator.Initialize()) {
        NEMU_LOG_FATAL("Init", "Failed to initialize Nemu emulator engine");
        return 1;
    }

    // Initialize Xbox Frontend for GUI navigation and game library browsing
    frontend::XboxFrontend frontend(*emulator.GetVfs(), *emulator.GetConfigManager());
    frontend.Render(*emulator.GetGpuBackend());

    if (!target_title.empty()) {
        NEMU_LOG_INFO("Init", "Launching target title: {}", target_title);
        if (emulator.LoadTitle(target_title)) {
            emulator.Run();
        } else {
            NEMU_LOG_ERROR("Init", "Could not load title '{}', returning to Frontend", target_title);
        }
    } else {
        auto launch_req = frontend.ConsumeLaunchRequest();
        if (launch_req) {
            NEMU_LOG_INFO("Init", "Frontend launch request: {}", *launch_req);
            if (emulator.LoadTitle(*launch_req)) {
                emulator.Run();
            }
        } else if (!frontend.GetLibrary().empty()) {
            const std::string first_title = frontend.GetLibrary()[0].virtual_path;
            NEMU_LOG_INFO("Init", "Auto-launching first library entry: {}", first_title);
            if (emulator.LoadTitle(first_title)) {
                emulator.Run();
            }
        } else {
            NEMU_LOG_INFO("Init", "No external title specified; running built-in verified demo...");
            if (emulator.LoadBuiltinDemo()) {
                emulator.Run(60); // Run 60 frames of baseline demo
            }
        }
    }

    NEMU_LOG_INFO("Init", "=========================================================");
    NEMU_LOG_INFO("Init", "  Nemu execution finished successfully.                  ");
    NEMU_LOG_INFO("Init", "=========================================================");
    return 0;
}
