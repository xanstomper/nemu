#include "platform/logger.hpp"
#include "core/debug/crash_handler.hpp"
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
#include <cstdio>
#ifndef _WIN32
#include <unistd.h>
#endif

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
    out.x          = k[SDL_SCANCODE_X]     || k[SDL_SCANCODE_I];
    out.y          = k[SDL_SCANCODE_Y]     || k[SDL_SCANCODE_U];
    out.lb         = k[SDL_SCANCODE_Q]     || k[SDL_SCANCODE_1];
    out.rb         = k[SDL_SCANCODE_E]     || k[SDL_SCANCODE_2];
    out.start      = (k[SDL_SCANCODE_RETURN] && k[SDL_SCANCODE_LCTRL]) || k[SDL_SCANCODE_P];
    out.back       = k[SDL_SCANCODE_BACKSPACE] || k[SDL_SCANCODE_GRAVE];
    return out;
}
#endif

static void SignalHandler(int) {
    g_app_running = false;
}

static int MainInternal(int argc, char** argv) {
    // Install the crash handler FIRST — before anything else can fail — so an
    // early launch/init fault (which is exactly what you'd otherwise be blind
    // to on the console) still writes a pinpointing report to LOCAL:/crash/.
    debug::CrashHandler::Install(debug::CrashHandler::kDefaultCrashDir);

    platform::Logger::Instance().SetMinLevel(platform::LogLevel::Trace);
    std::signal(SIGINT, SignalHandler);
    std::signal(SIGTERM, SignalHandler);

    // Definitive "launch began" heartbeat: qa_xbox.sh greps for this to tell a
    // successful process start from a crash-before-first-line.
    {
#ifdef _WIN32
        const long long app_pid = static_cast<long long>(GetCurrentProcessId());
#else
        const long long app_pid = static_cast<long long>(::getpid());
#endif
        NEMU_LOG_INFO("Init", "APP STARTED (PID {})", app_pid);
    }

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
    u16 gdb_port = 0;           // --gdb=<port> force auto-start the RSP stub
    debug::GdbStub gdb_stub;    // bring-up RSP stub (auto-start when --gdb given)
    bool demo_mode = false;
    bool ui_test_mode = false;
    std::string initial_view;
    std::string initial_dialog;
    std::string initial_filter;
    std::string initial_sort;
    std::string initial_menu;
    size_t initial_prop_tab = 0;
    size_t initial_settings_cat = 0;
    u64 ui_target_frames = 60;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--demo") {
            demo_mode = true;
        } else if (arg == "--ui-test") {
            ui_test_mode = true;
        } else if (arg.rfind("--subview=", 0) == 0) {
            initial_subview = arg.substr(10);
        } else if (arg.rfind("--view=", 0) == 0) {
            initial_view = arg.substr(7);
        } else if (arg.rfind("--dialog=", 0) == 0) {
            initial_dialog = arg.substr(9);
        } else if (arg.rfind("--filter=", 0) == 0) {
            initial_filter = arg.substr(9);
        } else if (arg.rfind("--sort=", 0) == 0) {
            initial_sort = arg.substr(7);
        } else if (arg.rfind("--menu=", 0) == 0) {
            initial_menu = arg.substr(7);
        } else if (arg.rfind("--prop-tab=", 0) == 0) {
            initial_prop_tab = std::strtoull(arg.substr(11).c_str(), nullptr, 10);
        } else if (arg.rfind("--settings-cat=", 0) == 0) {
            initial_settings_cat = std::strtoull(arg.substr(15).c_str(), nullptr, 10);
        } else if (arg.rfind("--ui-frames=", 0) == 0) {
            ui_target_frames = std::max<u64>(1, std::strtoull(arg.substr(12).c_str(), nullptr, 10));
        } else if (arg == "--run") {
            if (i + 1 < argc) run_boot_path = argv[++i];
        } else if (arg.rfind("--max-frames=", 0) == 0) {
            run_max_frames = std::max<u64>(1, std::strtoull(arg.substr(13).c_str(), nullptr, 10));
        } else if (arg.rfind("--texture-budget=", 0) == 0) {
            // MiB cap for resident texture memory (5 GiB budget tuning).
            texture_budget_mib = std::strtoull(arg.substr(17).c_str(), nullptr, 10);
        } else if (arg.rfind("--gdb=", 0) == 0) {
            // Force auto-start the GDB RSP stub at this port BEFORE the UI is
            // up, so a launch failure is remotely attachable (breaks the
            // chicken-and-egg of a debuggable app that won't launch its UI).
            gdb_port = static_cast<u16>(
                std::clamp<u64>(std::strtoull(arg.substr(6).c_str(), nullptr, 10), 1, 65535));
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

    // On-console crash visibility: the handler was installed at the very top
    // of main; here we only attach the title provider (needs the emulator).
    debug::CrashHandler::SetTitleProvider([&emulator](unsigned long long& out_tid,
                                             std::string& out_name) {
        if (auto p = emulator.GetProcess()) {
            out_tid  = p->GetPid();
            out_name = p->GetName();
        }
    });

    // Bring-up live debugging: when --gdb=<port> is passed, auto-start the RSP
    // stub NOW (before any title load / UI) so a launch failure is remotely
    // attachable without needing the UI to come up. Feeds it live memory + the
    // main thread's CpuState. (The frontend's settings toggle starts its own
    // instance later on the same port only if gdb_port == 0.)
    if (gdb_port != 0) {
        if (auto proc = emulator.GetProcess()) {
            gdb_stub.SetMemory(&proc->GetVirtualMemory());
        }
        gdb_stub.SetCpuStateProvider([&emulator]() -> cpu::CpuState* {
            auto th = emulator.GetMainThread();
            return th ? &th->GetCpuState() : nullptr;
        });
        if (gdb_stub.Start(gdb_port)) {
            NEMU_LOG_INFO("GDB", "Bring-up GDB RSP stub listening on TCP {}", gdb_port);
        } else {
            NEMU_LOG_WARN("GDB", "Failed to start bring-up GDB stub on TCP {}", gdb_port);
        }
    }

    // Headless boot probe (all platforms): --run <path.nro> loads a title and
    // runs a bounded number of frames, then reports a BOOT verdict. Scriptable
    // acceptance test for CI (Linux) AND on-device bring-up (Xbox Dev Mode
    // console over Device Portal / SSH-style invocation).
    if (!run_boot_path.empty()) {
        NEMU_LOG_INFO("BootProbe", "Headless run probe: '{}'", run_boot_path);
        if (emulator.GetGuestThreadPool()) {
            emulator.GetGuestThreadPool()->SetTraceEnabled(true);
        }
        if (!emulator.LoadTitle(run_boot_path)) {
            NEMU_LOG_ERROR("BootProbe", "FAILED: could not load title '{}'", run_boot_path);
            return 2;
        }
        std::cout << "[NEMU-BOOT] Loaded OK; running up to " << run_max_frames
                  << " frame(s) ..." << std::endl;
        emulator.Run(run_max_frames);
        const u64 frames = emulator.GetFrameCount();

        // Verdict. Advancing a frame only proves the host loop ran, NOT that the
        // guest booted -- a guest spinning on one bad PC advances every frame
        // while making no progress, which is exactly how NEMU reported "BOOTED"
        // over a 30M-instruction fault loop. Require positive evidence instead:
        //   1. a thread that stalled on repeated faults at one PC = hard fail
        //   2. a guest that faulted at all during boot = at best degraded
        //   3. instructions must actually have retired
        //   4. the guest must actually be IDLING, not burning its whole budget.
        //
        // (4) matters as much as the others and was learned the hard way: a
        // guest stuck in an infinite loop with no faults retires the *entire*
        // per-frame quantum forever and scored a perfect "BOOTED" with zero
        // errors, while never reaching its event loop or drawing a frame. A
        // title sitting at a menu blocks in svcWaitSynchronization and retires
        // orders of magnitude less per frame. Consuming >=90% of the budget every
        // frame means "still busy-spinning", not "booted".
        const u64 instrs = emulator.GetTotalInstructions();
        const auto& mem_faults = emulator.GetProcess()->GetVirtualMemory().GetFaultStats();
        const u64 mem_fault_count = mem_faults.total_faults.load(std::memory_order_relaxed);

        // Per-frame CPU quantum, mirrors Emulator::StepCpuQuantum(). The quantum
        // is split across the guest cores and only threads that are Ready
        // actually consume it, so a genuinely idle title lands well below the
        // nominal value; 70% sustained across the whole run means the guest is
        // looping rather than idling.
        constexpr u64 kFrameQuantum = 20000;
        constexpr double kBusySpinFraction = 0.70;
        const double ins_per_frame =
            frames > 0 ? static_cast<double>(instrs) / static_cast<double>(frames) : 0.0;
        const bool busy_spinning =
            frames > 0 && ins_per_frame >= kBusySpinFraction * static_cast<double>(kFrameQuantum);

        const char* verdict = "BOOTED";
        bool booted = true;
        if (emulator.StalledOnFault()) {
            verdict = "FAILED (guest stalled on repeated CPU faults)";
            booted = false;
        } else if (frames == 0) {
            verdict = "NO-FRAMES (stalled)";
            booted = false;
        } else if (instrs == 0) {
            verdict = "FAILED (no guest instructions retired)";
            booted = false;
        } else if (busy_spinning) {
            // Fault-free but not progressing: the guest is looping, so it has
            // not reached its event loop / menu even though nothing crashed.
            verdict = "BUSY-SPIN (guest retires the full per-frame quantum: "
                      "looping, not idling at a menu)";
            booted = false;
        } else if (mem_fault_count > 0) {
            verdict = "DEGRADED (booted, but guest memory faults occurred)";
        }

        std::cout << "[NEMU-BOOT] frames_executed=" << frames
                  << " instructions=" << instrs
                  << " memory_faults=" << mem_fault_count
                  << (mem_fault_count > 0
                          ? " last_fault_addr=0x" + [&] {
                                char buf[32];
                                std::snprintf(buf, sizeof(buf), "%016llX",
                                              static_cast<unsigned long long>(
                                                  mem_faults.last_fault_address.load(std::memory_order_relaxed)));
                                return std::string(buf);
                            }()
                          : std::string())
                  << " -> " << verdict
                  << std::endl;

        // Tail trace: dump the recent-PC ring on demand even when the boot looks
        // clean. A guest can burn its entire per-frame instruction budget
        // without faulting -- the frame counter and instruction counter both
        // look healthy while the guest is spinning and never reaches its
        // event loop -- so "BOOTED" alone cannot distinguish a real menu from a
        // spin. `NEMU_TRACE_TAIL=1` prints where the guest actually is.
        if (std::getenv("NEMU_TRACE_TAIL") != nullptr) {
            const auto tail = emulator.GetBootTrace();
            if (!tail.empty()) {
                std::string line;
                vaddr_t prev = ~0ULL;
                for (vaddr_t pc : tail) {
                    if (pc == prev) continue;
                    prev = pc;
                    char buf[32];
                    std::snprintf(buf, sizeof(buf), "%016llX",
                                  static_cast<unsigned long long>(pc));
                    line += buf;
                    line += ' ';
                    if (line.size() > 4000) break;
                }
                NEMU_LOG_INFO("BootProbe", "TAIL guest PCs (newest first): {}", line);
            }
        }

        if (!booted || mem_fault_count > 0) {
            NEMU_LOG_ERROR("BootProbe",
                           "Boot %s: %s (frames={}, instructions={}, memory_faults={}). "
                           "See the first CPU/Memory ERROR above for the real blocker.",
                           booted ? "DEGRADED" : "FAILED",
                           verdict, frames, instrs, mem_fault_count);

            // Dump the recent PC history so a failure (or a degraded boot whose
            // guest is faulting in a loop) can be post-mortem'd without re-running
            // under a debugger. Newest first, with adjacent duplicates collapsed
            // so a spin reads as one entry.
            const auto trace = emulator.GetBootTrace();
            if (!trace.empty()) {
                std::string line;
                vaddr_t prev = ~0ULL;
                for (vaddr_t pc : trace) {
                    if (pc == prev) continue;
                    prev = pc;
                    char buf[32];
                    std::snprintf(buf, sizeof(buf), "%016llX",
                                  static_cast<unsigned long long>(pc));
                    line += buf;
                    line += ' ';
                    if (line.size() > 8000) break;
                }
                NEMU_LOG_ERROR("BootProbe", "Recent guest PCs (newest first): {}", line);
            }
        }
        NEMU_LOG_INFO("BootProbe", "Headless run complete: {} frame(s)", frames);
        // Distinct exit codes so CI can gate on a real boot: 0 = booted,
        // 2 = title failed to load, 3 = no frames, 4 = guest stalled on faults.
        if (!booted) {
            return emulator.StalledOnFault() || instrs == 0 ? 4 : 3;
        }
        return 0;
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

    // Live remote debugging: feed the GDB RSP stub the emulator's current guest
    // CPU state so a remote gdb-multiarch/LLDB attached to :24689 reads REAL
    // registers/pc/sp (not zeros). Best-effort live reads while the guest runs.
    frontend.SetGdbCpuStateProvider([&emulator]() -> cpu::CpuState* {
        auto th = emulator.GetMainThread();
        return th ? &th->GetCpuState() : nullptr;
    });

    if (!initial_subview.empty()) {
        if (initial_subview == "settings") {
            frontend.SetActiveSubView(frontend::ActiveSubView::SystemSettings);
            frontend.SetSettingsCategory(initial_settings_cat);
        }
        else if (initial_subview == "controllers") frontend.SetActiveSubView(frontend::ActiveSubView::Controllers);
        else if (initial_subview == "powermenu") frontend.SetActiveSubView(frontend::ActiveSubView::PowerMenu);
        else if (initial_subview == "gameoptions") frontend.SetActiveSubView(frontend::ActiveSubView::GameOptions);
        else if (initial_subview == "album") frontend.SetActiveSubView(frontend::ActiveSubView::Album);
        else if (initial_subview == "news") frontend.SetActiveSubView(frontend::ActiveSubView::News);
        else if (initial_subview == "nso") frontend.SetActiveSubView(frontend::ActiveSubView::NSO);
        else if (initial_subview == "eshop") frontend.SetActiveSubView(frontend::ActiveSubView::EShop);
        else if (initial_subview == "profile") frontend.SetActiveSubView(frontend::ActiveSubView::UserProfile);
    }

    if (!initial_view.empty()) {
        if (initial_view == "grid") frontend.SetGameListMode(frontend::GameListMode::Grid);
        else if (initial_view == "list") frontend.SetGameListMode(frontend::GameListMode::List);
        else if (initial_view == "carousel") frontend.SetGameListMode(frontend::GameListMode::Carousel);
    }
    if (!initial_filter.empty()) {
        if (initial_filter == "all") frontend.SetFilterCategory(frontend::LibraryFilterCategory::All);
        else if (initial_filter == "installed") frontend.SetFilterCategory(frontend::LibraryFilterCategory::Installed);
        else if (initial_filter == "favorites") frontend.SetFilterCategory(frontend::LibraryFilterCategory::Favorites);
        else if (initial_filter == "updates") frontend.SetFilterCategory(frontend::LibraryFilterCategory::Updates);
        else if (initial_filter == "dlc") frontend.SetFilterCategory(frontend::LibraryFilterCategory::DLC);
    }
    if (!initial_sort.empty()) {
        if (initial_sort == "title") frontend.SetSortMode(frontend::LibrarySortMode::TitleAsc);
        else if (initial_sort == "time") frontend.SetSortMode(frontend::LibrarySortMode::PlayTime);
        else if (initial_sort == "size") frontend.SetSortMode(frontend::LibrarySortMode::FileSize);
        else if (initial_sort == "compat") frontend.SetSortMode(frontend::LibrarySortMode::Compatibility);
    }
    if (!initial_dialog.empty()) {
        if (initial_dialog == "properties") {
            frontend.SetPerGamePropertiesOpen(true);
            frontend.SetPerGameTab(initial_prop_tab);
        }
        else if (initial_dialog == "nand") frontend.SetInstallNandDialogOpen(true);
        else if (initial_dialog == "mods") frontend.SetModManagerOpen(true);
        else if (initial_dialog == "cheats") frontend.SetCheatManagerOpen(true);
        else if (initial_dialog == "multiplayer") frontend.SetMultiplayerLobbyOpen(true);
        else if (initial_dialog == "about") frontend.SetAboutDialogOpen(true);
        else if (initial_dialog == "amiibo") frontend.ToggleAmiiboScanner();
        else if (initial_dialog == "tas") frontend.SetTasOverlayOpen(true);
        else if (initial_dialog == "quickmenu") frontend.SetQuickMenuOpen(true);
        else if (initial_dialog == "context") frontend.SetContextMenuOpen(true);
        else if (initial_dialog == "gameoptions") frontend.SetActiveSubView(frontend::ActiveSubView::GameOptions);
    }
    if (!initial_menu.empty()) {
        if (initial_menu == "file") frontend.OpenTopMenuCategory(0);
        else if (initial_menu == "emulation") frontend.OpenTopMenuCategory(1);
        else if (initial_menu == "view") frontend.OpenTopMenuCategory(2);
        else if (initial_menu == "multiplayer") frontend.OpenTopMenuCategory(3);
        else if (initial_menu == "tools") frontend.OpenTopMenuCategory(4);
        else if (initial_menu == "help") frontend.OpenTopMenuCategory(5);
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
            input_state.lb         = input_state.lb         || kb.lb;
            input_state.rb         = input_state.rb         || kb.rb;
            input_state.start      = input_state.start      || kb.start;
            input_state.back       = input_state.back       || kb.back;

            // Direct desktop keyboard shortcuts for Eden features
            const Uint8* kstate = SDL_GetKeyboardState(nullptr);
            if (kstate) {
                bool ctrl = kstate[SDL_SCANCODE_LCTRL] || kstate[SDL_SCANCODE_RCTRL];
                bool alt  = kstate[SDL_SCANCODE_LALT]  || kstate[SDL_SCANCODE_RALT];

                static bool prev_f1{false}, prev_f6{false}, prev_f7{false}, prev_f8{false}, prev_f9{false}, prev_f11{false}, prev_f12{false};
                static bool prev_ctrl_o{false}, prev_ctrl_i{false}, prev_ctrl_p{false}, prev_ctrl_m{false}, prev_ctrl_a{false}, prev_ctrl_c{false}, prev_ctrl_t{false};
                static bool prev_tab{false}, prev_alt_enter{false};

                // F1: About / Documentation dialog
                if (kstate[SDL_SCANCODE_F1] && !prev_f1) frontend.ToggleAboutDialog();
                // F6: Grid View
                if (kstate[SDL_SCANCODE_F6] && !prev_f6) frontend.SetGameListMode(frontend::GameListMode::Grid);
                // F7: List View
                if (kstate[SDL_SCANCODE_F7] && !prev_f7) frontend.SetGameListMode(frontend::GameListMode::List);
                // F8: Carousel View
                if (kstate[SDL_SCANCODE_F8] && !prev_f8) frontend.SetGameListMode(frontend::GameListMode::Carousel);
                // F9: LDN Multiplayer Lobby
                if (kstate[SDL_SCANCODE_F9] && !prev_f9) frontend.ToggleMultiplayerLobby();
                // F11: Fullscreen
                if (kstate[SDL_SCANCODE_F11] && !prev_f11) frontend.ToggleFullscreen();
                // F12: Screenshot
                if (kstate[SDL_SCANCODE_F12] && !prev_f12) frontend.RequestScreenshot();

                // Ctrl+O: Open Content Manager / File Browser (EShop)
                if (ctrl && kstate[SDL_SCANCODE_O] && !prev_ctrl_o) frontend.SetActiveSubView(frontend::ActiveSubView::EShop);
                // Ctrl+I: Install to NAND
                if (ctrl && kstate[SDL_SCANCODE_I] && !prev_ctrl_i) frontend.ToggleInstallNandDialog();
                // Ctrl+P: System Settings
                if (ctrl && kstate[SDL_SCANCODE_P] && !prev_ctrl_p) frontend.SetActiveSubView(frontend::ActiveSubView::SystemSettings);
                // Ctrl+M: Mod Manager
                if (ctrl && kstate[SDL_SCANCODE_M] && !prev_ctrl_m) frontend.ToggleModManager();
                // Ctrl+A: Virtual Amiibo Scanner
                if (ctrl && kstate[SDL_SCANCODE_A] && !prev_ctrl_a) frontend.ToggleAmiiboScanner();
                // Ctrl+C: Controllers Configuration
                if (ctrl && kstate[SDL_SCANCODE_C] && !prev_ctrl_c) frontend.SetActiveSubView(frontend::ActiveSubView::Controllers);
                // Ctrl+T: TAS Speedrun Overlay
                if (ctrl && kstate[SDL_SCANCODE_T] && !prev_ctrl_t) frontend.ToggleTasOverlay();

                // Alt+Enter: Fullscreen
                if (alt && kstate[SDL_SCANCODE_RETURN] && !prev_alt_enter) frontend.ToggleFullscreen();
                // Tab: Toggle Desktop Top Menu Bar
                if (kstate[SDL_SCANCODE_TAB] && !prev_tab) frontend.ToggleTopMenu();

                prev_f1 = kstate[SDL_SCANCODE_F1];
                prev_f6 = kstate[SDL_SCANCODE_F6];
                prev_f7 = kstate[SDL_SCANCODE_F7];
                prev_f8 = kstate[SDL_SCANCODE_F8];
                prev_f9 = kstate[SDL_SCANCODE_F9];
                prev_f11 = kstate[SDL_SCANCODE_F11];
                prev_f12 = kstate[SDL_SCANCODE_F12];
                prev_ctrl_o = ctrl && kstate[SDL_SCANCODE_O];
                prev_ctrl_i = ctrl && kstate[SDL_SCANCODE_I];
                prev_ctrl_p = ctrl && kstate[SDL_SCANCODE_P];
                prev_ctrl_m = ctrl && kstate[SDL_SCANCODE_M];
                prev_ctrl_a = ctrl && kstate[SDL_SCANCODE_A];
                prev_ctrl_c = ctrl && kstate[SDL_SCANCODE_C];
                prev_ctrl_t = ctrl && kstate[SDL_SCANCODE_T];
                prev_alt_enter = alt && kstate[SDL_SCANCODE_RETURN];
                prev_tab = kstate[SDL_SCANCODE_TAB];
            }
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
                        in_game_input.lb         = in_game_input.lb         || kb.lb;
                        in_game_input.rb         = in_game_input.rb         || kb.rb;
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

        // Render Eden UI frame (or QuickMenu overlay if open)
        if (frontend.IsQuickMenuOpen()) {
            frontend.RenderQuickMenu(*emulator.GetGpuBackend());
        } else {
            frontend.Render(*emulator.GetGpuBackend());
        }

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

        if (ui_test_mode && ui_frames >= ui_target_frames) {
            NEMU_LOG_INFO("Frontend", "UI test mode completed {} frames successfully", ui_frames);
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

