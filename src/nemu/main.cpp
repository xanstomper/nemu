#include "platform/logger.hpp"
#include "core/system/emulator.hpp"
#include "frontend/xbox_frontend.hpp"
#include <iostream>
#include <string>
#include <chrono>
#include <thread>
#include <csignal>
#include <atomic>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

using namespace nemu;
using namespace nemu::core;

static std::atomic<bool> g_app_running{true};

static void SignalHandler(int) {
    g_app_running = false;
}

int main(int argc, char** argv) {
    platform::Logger::Instance().SetMinLevel(platform::LogLevel::Info);
    std::signal(SIGINT, SignalHandler);
    std::signal(SIGTERM, SignalHandler);

    NEMU_LOG_INFO("Init", "=========================================================");
    NEMU_LOG_INFO("Init", "  NEMU: Nintendo Switch Emulator for Xbox Series S/X     ");
    NEMU_LOG_INFO("Init", "  Target: Microsoft Xbox Developer Mode (UWP Full Trust) ");
    NEMU_LOG_INFO("Init", "  Milestone 10: Complete Interactive Emulation Runtime   ");
    NEMU_LOG_INFO("Init", "=========================================================");

    std::string target_title;
    bool demo_mode = false;
    bool ui_test_mode = false;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--demo") {
            demo_mode = true;
        } else if (arg == "--ui-test") {
            ui_test_mode = true;
        } else if (!arg.starts_with("--")) {
            target_title = arg;
        }
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

    // If direct title or automated demo flag requested, execute immediately
    if (demo_mode) {
        NEMU_LOG_INFO("Init", "Automated demo mode requested; running built-in verified demo...");
        if (emulator.LoadBuiltinDemo()) {
            emulator.Run(60);
        }
        return 0;
    }

    if (!target_title.empty()) {
        NEMU_LOG_INFO("Init", "Launching target title directly: {}", target_title);
        if (emulator.LoadTitle(target_title)) {
            emulator.Run();
        } else {
            NEMU_LOG_ERROR("Init", "Could not load title '{}', falling back to Eden Frontend", target_title);
        }
    }

    // Initialize Xbox Frontend for GUI navigation and game library browsing
    frontend::XboxFrontend frontend(*emulator.GetVfs(), *emulator.GetConfigManager());
    auto controller = emulator.GetControllerDriver();

    NEMU_LOG_INFO("Frontend", "Entering interactive Eden / Switch UI event loop...");
    u64 ui_frames = 0;

    while (g_app_running) {
#ifdef _WIN32
        MSG msg;
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) {
                g_app_running = false;
                break;
            }
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        if (!g_app_running) break;
#endif

        // Poll Xbox Controller input
        core::hid::XboxGamepadState input_state{};
        if (controller) {
            auto polled = controller->Poll(0);
            if (polled) {
                input_state = *polled;
            }
        }

        // Exit combo on Xbox controller: Back + Start in Home UI
        if (input_state.back && input_state.start) {
            NEMU_LOG_INFO("Frontend", "Exit combo (Back + Start) detected; terminating application");
            break;
        }

        // Forward gamepad state to Eden UI state machine
        frontend.ProcessInput(input_state, controller.get());

        // Check if user requested to launch a game from carousel
        auto launch_req = frontend.ConsumeLaunchRequest();
        if (launch_req) {
            std::string current_title = *launch_req;
            NEMU_LOG_INFO("Frontend", "Launching requested title: {}", current_title);

            while (g_app_running && !current_title.empty()) {
                if (!emulator.LoadTitle(current_title)) {
                    NEMU_LOG_ERROR("Frontend", "Failed to load title: {}", current_title);
                    break;
                }
                emulator.Start();

                bool reload_requested = false;
                while (g_app_running && (emulator.GetState() == system::EmulatorState::Running ||
                                         emulator.GetState() == system::EmulatorState::Paused ||
                                         frontend.IsQuickMenuOpen())) {
#ifdef _WIN32
                    MSG in_game_msg;
                    while (PeekMessageW(&in_game_msg, nullptr, 0, 0, PM_REMOVE)) {
                        if (in_game_msg.message == WM_QUIT) {
                            g_app_running = false;
                            emulator.Stop();
                            break;
                        }
                        TranslateMessage(&in_game_msg);
                        DispatchMessageW(&in_game_msg);
                    }
                    if (!g_app_running) break;
#endif
                    // Poll Xbox controller input
                    core::hid::XboxGamepadState in_game_input{};
                    if (controller) {
                        auto polled = controller->Poll(0);
                        if (polled) {
                            in_game_input = *polled;
                        }
                    }

                    // Process input through frontend (handles Quick Menu toggle, navigation, buttons)
                    frontend.ProcessInGameInput(in_game_input);

                    if (frontend.IsQuickMenuOpen()) {
                        // RetroArch Quick Menu overlay active: pause game execution & render menu
                        if (emulator.GetState() == system::EmulatorState::Running) {
                            emulator.Pause();
                        }
                        frontend.RenderQuickMenu(*emulator.GetGpuBackend());

                        if (frontend.ConsumeRestartRequested()) {
                            NEMU_LOG_INFO("Frontend", "QuickMenu: Restarting current title");
                            emulator.Stop();
                            reload_requested = true;
                            break;
                        }
                        if (frontend.ConsumeCloseGameRequested()) {
                            NEMU_LOG_INFO("Frontend", "QuickMenu: Closing content");
                            emulator.Stop();
                            break;
                        }
                        if (frontend.ConsumeSaveStateRequested()) {
                            emulator.SaveState(frontend.GetStateSlot());
                        }
                        if (frontend.ConsumeLoadStateRequested()) {
                            emulator.LoadState(frontend.GetStateSlot());
                        }

                        std::this_thread::sleep_for(std::chrono::milliseconds(16));
                        continue;
                    }

                    // Quick Menu closed: ensure emulator resumed
                    if (emulator.GetState() == system::EmulatorState::Paused) {
                        emulator.Resume();
                    }

                    // Step emulation frame quantum
                    if (!emulator.StepFrame()) {
                        break;
                    }

                    // ~60 FPS pacing
                    std::this_thread::sleep_for(std::chrono::microseconds(16666));
                }

                if (!reload_requested) {
                    break;
                }
            }

            NEMU_LOG_INFO("Frontend", "Emulation concluded; returning to Eden UI Home Screen");
            frontend.RefreshLibrary();
        }

        // Render Eden UI frame
        frontend.Render(*emulator.GetGpuBackend());
        ++ui_frames;

        if (ui_test_mode && ui_frames >= 60) {
            NEMU_LOG_INFO("Frontend", "UI test mode completed 60 frames successfully");
            break;
        }

        // Maintain ~60 FPS UI pacing
        std::this_thread::sleep_for(std::chrono::milliseconds(16));
    }

    NEMU_LOG_INFO("Init", "=========================================================");
    NEMU_LOG_INFO("Init", "  Nemu execution finished successfully.                  ");
    NEMU_LOG_INFO("Init", "=========================================================");
    return 0;
}

#ifdef _WIN32
int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int) {
    return main(__argc, __argv);
}
#endif

