#include "platform/logger.hpp"
#include "core/system/emulator.hpp"
#include "core/memory/memory_budget.hpp"
#include "frontend/xbox_frontend.hpp"
#include <iostream>
#include <string>
#include <chrono>
#include <thread>
#include <csignal>
#include <atomic>
#include <cstdlib>
#include <cstdint>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

using namespace nemu;
using namespace nemu::core;

static std::atomic<bool> g_app_running{true};

#ifdef NEMU_SDL2
#include <SDL.h>
#include "core/gpu/sdl2/sdl2_backend.hpp"
#include "core/hid/controller_mapping.hpp"

// Desktop keyboard -> Xbox gamepad bridge so the full Eden UI is navigable on
// a PC without a physical controller (same input the console sends).
static core::hid::XboxGamepadState PollSdlKeyboard() {
    core::hid::XboxGamepadState out{};
    SDL_PumpEvents();
    const Uint8* k = SDL_GetKeyboardState(nullptr);
    if (!k) return out;
    out.dpad_up    = k[SDL_SCANCODE_UP]    || k[SDL_SCANCODE_W];
    out.dpad_down  = k[SDL_SCANCODE_DOWN]  || k[SDL_SCANCODE_S];
    out.dpad_left  = k[SDL_SCANCODE_LEFT]  || k[SDL_SCANCODE_A];
    out.dpad_right = k[SDL_SCANCODE_RIGHT] || k[SDL_SCANCODE_D];
    out.a          = k[SDL_SCANCODE_RETURN] || k[SDL_SCANCODE_SPACE] || k[SDL_SCANCODE_J];
    out.b          = k[SDL_SCANCODE_ESCAPE] || k[SDL_SCANCODE_K];
    out.x          = k[SDL_SCANCODE_I];
    out.y          = k[SDL_SCANCODE_U];
    out.start      = k[SDL_SCANCODE_RETURN] && k[SDL_SCANCODE_LCTRL];
    out.back       = k[SDL_SCANCODE_BACKSPACE];
    return out;
}
#endif

static void SignalHandler(int) {
    g_app_running = false;
}

static int MainInternal(int argc, char** argv) {
    platform::Logger::Instance().SetMinLevel(platform::LogLevel::Info);
    std::signal(SIGINT, SignalHandler);
    std::signal(SIGTERM, SignalHandler);

    NEMU_LOG_INFO("Init", "=========================================================");
    NEMU_LOG_INFO("Init", "  NEMU: Nintendo Switch Emulator for Xbox Series S/X     ");
    NEMU_LOG_INFO("Init", "  Target: Microsoft Xbox Developer Mode (UWP Full Trust) ");
    NEMU_LOG_INFO("Init", "  Milestone 10: Complete Interactive Emulation Runtime   ");
    NEMU_LOG_INFO("Init", "=========================================================");

    std::string target_title;
    std::string initial_subview;
    // Headless boot probe is compiled on ALL platforms (Linux + Windows/Xbox).
    // It is a scriptable NRO boot verifier for CI and on-device bring-up:
    // --run <path.nro> [--max-frames=N] loads a title, runs a bounded number of
    // frames, and reports a BOOT verdict via stdout + exit code.
    std::string run_boot_path;   // --run <path.nro> headless boot probe
    u64 run_max_frames = 3;
    u64 texture_budget_mib = 0; // --texture-budget=<MiB> resident texture cap
    bool demo_mode = false;
    bool ui_test_mode = false;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--demo") {
            demo_mode = true;
        } else if (arg == "--ui-test") {
            ui_test_mode = true;
        } else if (arg.rfind("--subview=", 0) == 0) {
            initial_subview = arg.substr(10);
        } else if (arg == "--run") {
            if (i + 1 < argc) run_boot_path = argv[++i];
        } else if (arg.rfind("--max-frames=", 0) == 0) {
            run_max_frames = std::max<u64>(1, std::strtoull(arg.substr(13).c_str(), nullptr, 10));
        } else if (arg.rfind("--texture-budget=", 0) == 0) {
            // MiB cap for resident texture memory (5 GiB budget tuning).
            texture_budget_mib = std::strtoull(arg.substr(17).c_str(), nullptr, 10);
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
        .title_path = target_title,
        .texture_budget_bytes = (texture_budget_mib != 0)
                                    ? (texture_budget_mib * 1024 * 1024)
                                    : 0
    };

    system::Emulator emulator(emu_cfg);
    if (!emulator.Initialize()) {
        NEMU_LOG_FATAL("Init", "Failed to initialize Nemu emulator engine");
        return 1;
    }

    // Headless boot probe (all platforms): --run <path.nro> loads a title and
    // runs a bounded number of frames, then reports a BOOT verdict. Scriptable
    // acceptance test for CI (Linux) AND on-device bring-up (Xbox Dev Mode
    // console over Device Portal / SSH-style invocation).
    if (!run_boot_path.empty()) {
        NEMU_LOG_INFO("BootProbe", "Headless run probe: '{}'", run_boot_path);
        if (!emulator.LoadTitle(run_boot_path)) {
            NEMU_LOG_ERROR("BootProbe", "FAILED: could not load title '{}'", run_boot_path);
            return 2;
        }
        std::cout << "[NEMU-BOOT] Loaded OK; running up to " << run_max_frames
                  << " frame(s) ..." << std::endl;
        emulator.Run(run_max_frames);
        const u64 frames = emulator.GetFrameCount();
        const bool advanced = frames > 0;
        std::cout << "[NEMU-BOOT] frames_executed=" << frames
                  << " -> " << (advanced ? "BOOTED (advanced frames)" : "NO-FRAMES (stalled)")
                  << std::endl;
        NEMU_LOG_INFO("BootProbe", "Headless run complete: {} frame(s)", frames);
        return advanced ? 0 : 3;
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
    frontend.SetLdnNetwork(emulator.GetLdnNetwork());

    if (!initial_subview.empty()) {
        if (initial_subview == "settings") frontend.SetActiveSubView(frontend::ActiveSubView::SystemSettings);
        else if (initial_subview == "controllers") frontend.SetActiveSubView(frontend::ActiveSubView::Controllers);
        else if (initial_subview == "powermenu") frontend.SetActiveSubView(frontend::ActiveSubView::PowerMenu);
        else if (initial_subview == "gameoptions") frontend.SetActiveSubView(frontend::ActiveSubView::GameOptions);
        else if (initial_subview == "album") frontend.SetActiveSubView(frontend::ActiveSubView::Album);
        else if (initial_subview == "news") frontend.SetActiveSubView(frontend::ActiveSubView::News);
        else if (initial_subview == "nso") frontend.SetActiveSubView(frontend::ActiveSubView::NSO);
        else if (initial_subview == "eshop") frontend.SetActiveSubView(frontend::ActiveSubView::EShop);
        else if (initial_subview == "profile") frontend.SetActiveSubView(frontend::ActiveSubView::UserProfile);
    }

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
#ifdef NEMU_SDL2
        {
            auto backend = emulator.GetGpuBackend();
            if (backend) {
                auto sdl_backend = std::dynamic_pointer_cast<gpu::sdl2::Sdl2GpuBackend>(backend);
                if (sdl_backend) {
                    if (!sdl_backend->PumpEvents()) {
                        g_app_running = false;
                        break;
                    }
                    auto ptr = sdl_backend->ConsumePointerState();
                    frontend.ProcessPointer(ptr.x, ptr.y, ptr.left_down, ptr.left_clicked, ptr.right_clicked, ptr.wheel_delta);
                }
            }
            auto kb = PollSdlKeyboard();
            input_state.dpad_up    = input_state.dpad_up    || kb.dpad_up;
            input_state.dpad_down  = input_state.dpad_down  || kb.dpad_down;
            input_state.dpad_left  = input_state.dpad_left  || kb.dpad_left;
            input_state.dpad_right = input_state.dpad_right || kb.dpad_right;
            input_state.a          = input_state.a          || kb.a;
            input_state.b          = input_state.b          || kb.b;
            input_state.x          = input_state.x          || kb.x;
            input_state.y          = input_state.y          || kb.y;
            input_state.start      = input_state.start      || kb.start;
            input_state.back       = input_state.back       || kb.back;
        }
#endif

        // Exit request from Switch Power Menu or Exit combo (Back + Start)
        if (frontend.ConsumeExitRequested()) {
            NEMU_LOG_INFO("Frontend", "Exit requested via Switch Power Menu");
            break;
        }

        if (input_state.back && input_state.start) {
            NEMU_LOG_INFO("Frontend", "Exit combo (Back + Start) detected; terminating application");
            break;
        }

        // Forward gamepad state to Eden UI state machine
        frontend.ProcessInput(input_state, controller.get());

        // Apply config changes made in the Switch UI to live emulator subsystems
        if (frontend.ConsumeConfigChanged()) {
            emulator.ApplyRuntimeConfig();
        }

        // Push real controller connection state (polled from the driver)
        if (controller) {
            frontend::XboxFrontend::ControllerStatus cs;
            for (size_t i = 0; i < 4; ++i) cs.connected[i] = controller->IsConnected(i);
            cs.xinput_available = controller->IsXInputAvailable();
            frontend.PushControllerStatus(cs);
        }

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
#ifdef NEMU_SDL2
                    {
                        // Pump the window event queue every frame so the OS
                        // never marks us unresponsive and keyboard state stays
                        // fresh (this was the source of the freeze).
                        auto backend = emulator.GetGpuBackend();
                        auto sdl_backend = std::dynamic_pointer_cast<gpu::sdl2::Sdl2GpuBackend>(backend);
                        if (sdl_backend) {
                            if (!sdl_backend->PumpEvents()) {
                                g_app_running = false;
                                emulator.Stop();
                                break;
                            }
                            auto ptr = sdl_backend->ConsumePointerState();
                            if (ptr.right_clicked) {
                                // Right-click opens the Quick Menu in-game
                                if (!frontend.IsQuickMenuOpen()) {
                                    frontend.SetQuickMenuOpen(true);
                                }
                            }
                        }
                        auto kb = PollSdlKeyboard();
                        in_game_input.dpad_up    = in_game_input.dpad_up    || kb.dpad_up;
                        in_game_input.dpad_down  = in_game_input.dpad_down  || kb.dpad_down;
                        in_game_input.dpad_left  = in_game_input.dpad_left  || kb.dpad_left;
                        in_game_input.dpad_right = in_game_input.dpad_right || kb.dpad_right;
                        in_game_input.a          = in_game_input.a          || kb.a;
                        in_game_input.b          = in_game_input.b          || kb.b;
                        in_game_input.x          = in_game_input.x          || kb.x;
                        in_game_input.y          = in_game_input.y          || kb.y;
                        in_game_input.start      = in_game_input.start      || kb.start;
                        in_game_input.back       = in_game_input.back       || kb.back;
                    }
#endif

                    // Hard exit combo always available, even inside emulation
                    if (in_game_input.back && in_game_input.start) {
                        NEMU_LOG_INFO("Frontend", "Exit combo during emulation; terminating");
                        g_app_running = false;
                        emulator.Stop();
                        break;
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
                        if (frontend.ConsumeScreenshotRequested()) {
                            // Capture the real frame to save:/screenshots/ (PPM; Album lists it live)
                            std::error_code ec;
                            std::filesystem::create_directories("save/screenshots", ec);
                            auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                                std::chrono::system_clock::now().time_since_epoch()).count();
                            std::string path = "save/screenshots/nemu_" + std::to_string(now_ms) + ".ppm";
                            if (emulator.GetGpuBackend()->DumpFramePPM(path.c_str())) {
                                NEMU_LOG_INFO("Frontend", "Screenshot saved: {}", path);
                            } else {
                                NEMU_LOG_WARN("Frontend", "Screenshot failed (backend does not support frame dump)");
                            }
                        }

                        std::this_thread::sleep_for(std::chrono::milliseconds(16));
                        continue;
                    }

                    // Quick Menu closed: ensure emulator resumed
                    if (emulator.GetState() == system::EmulatorState::Paused) {
                        emulator.Resume();
                    }

                    // Step emulation frame quantum (2x throughput when
                    // fast-forwarding, like RetroArch's fast forward)
                    {
                        const auto ff_steps = frontend.IsFastForwardActive() ? 2u : 1u;
                        bool emulation_ended = false;
                        for (auto it = 0u; it < ff_steps; ++it) {
                            if (!emulator.StepFrame()) {
                                emulation_ended = true;
                                break;
                            }
                        }
                        if (emulation_ended) {
                            break;
                        }
                    }

                    // ~60 FPS pacing (drop the sleep while fast-forwarding so
                    // the host runs ahead instead of blocking)
                    if (!frontend.IsFastForwardActive()) {
                        std::this_thread::sleep_for(std::chrono::microseconds(16666));
                    }
                }

                if (!reload_requested) {
                    break;
                }
            }

            NEMU_LOG_INFO("Frontend", "Emulation concluded; returning to Eden UI Home Screen");
            frontend.EndPlaytimeSession();
            frontend.RefreshLibrary();
        }

        // Render Eden UI frame
        frontend.Render(*emulator.GetGpuBackend());

        // Push live emulator telemetry for the Diagnostics screen
        {
            frontend::XboxFrontend::LiveDiagnostics d;
            d.frame_count = emulator.GetFrameCount();
            d.total_instructions = emulator.GetTotalInstructions();
            if (auto* jit = emulator.GetJitCompiler()) {
                d.jit_blocks_compiled = jit->GetStats().blocks_compiled;
                d.jit_blocks_executed = jit->GetStats().blocks_executed;
            }
            const auto& gs = emulator.GetGpuBackend()->GetStats();
            d.gpu_draw_calls = gs.draw_calls;
            d.gpu_frames_presented = gs.frames_presented;
            d.emulator_running = (emulator.GetState() == system::EmulatorState::Running);
            d.backend_name = std::string(emulator.GetGpuBackend()->GetBackendName());
            if (auto audio = emulator.GetAudioBackend()) {
                d.audio_backend_name = std::string(audio->GetBackendName());
            }
            // Xbox Dev Mode 5 GiB RAM budget visibility (Tier-C3).
            d.mem_used_bytes = memory::MemoryBudget::TotalEstimated();
            d.mem_peak_bytes = memory::MemoryBudget::Peak();
            d.mem_cap_bytes = memory::MemoryBudget::kXboxDevCapBytes;
            frontend.PushDiagnostics(d);
        }
        ++ui_frames;

        if (ui_test_mode && ui_frames >= 60) {
            NEMU_LOG_INFO("Frontend", "UI test mode completed 60 frames successfully");
            break;
        }

        // Maintain buttery-smooth 60 FPS UI pacing without double-sleeping over VSync
        bool has_vsync = false;
#ifdef NEMU_SDL2
        {
            auto backend = emulator.GetGpuBackend();
            if (backend) {
                auto sdl_backend = std::dynamic_pointer_cast<gpu::sdl2::Sdl2GpuBackend>(backend);
                if (sdl_backend && sdl_backend->HasVsync()) {
                    has_vsync = true;
                }
            }
        }
#endif
        if (!has_vsync) {
            static auto last_frame_time = std::chrono::steady_clock::now();
            auto now = std::chrono::steady_clock::now();
            auto elapsed_us = std::chrono::duration_cast<std::chrono::microseconds>(now - last_frame_time).count();
            if (elapsed_us < 16666) {
                std::this_thread::sleep_for(std::chrono::microseconds(16666 - elapsed_us));
            }
            last_frame_time = std::chrono::steady_clock::now();
        }
    }

    NEMU_LOG_INFO("Init", "=========================================================");
    NEMU_LOG_INFO("Init", "  Nemu execution finished successfully.                  ");
    NEMU_LOG_INFO("Init", "=========================================================");
    return 0;
}

int main(int argc, char** argv) {
    return MainInternal(argc, argv);
}

#ifdef _WIN32
int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int) {
    return MainInternal(__argc, __argv);
}
#endif

