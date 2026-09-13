#include "frontend/xbox_frontend.hpp"
#include "core/filesystem/vfs.hpp"
#include "core/config/config_manager.hpp"
#include "core/gpu/null_backend.hpp"
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
using namespace nemu::frontend;

int main() {
    std::cout << "[Test: Eden / Switch UI Frontend Navigation & State Machine]" << std::endl;

    const std::filesystem::path test_dir = std::filesystem::current_path() / "test_fe_sandbox";
    std::error_code ec;
    std::filesystem::remove_all(test_dir, ec);
    std::filesystem::create_directories(test_dir, ec);

    core::filesystem::VirtualFileSystem vfs;
    NEMU_TEST_ASSERT(vfs.Mount("save:/", test_dir / "save", false), "Mount save:/");
    NEMU_TEST_ASSERT(vfs.Mount("sdmc:/", test_dir / "sdmc", false), "Mount sdmc:/");

    core::config::ConfigManager config(vfs);
    XboxFrontend fe(vfs, config);

    // Test 1: Fallback built-in demo when SDMC empty
    {
        fe.RefreshLibrary();
        const auto& lib = fe.GetLibrary();
        NEMU_TEST_ASSERT(!lib.empty(), "Library contains at least fallback entry");
        NEMU_TEST_ASSERT(lib[0].virtual_path == "builtin:/demo.nro", "Fallback demo NRO path");
        NEMU_TEST_ASSERT(fe.GetCurrentTab() == FrontendTab::Library, "Default tab is Library");
        NEMU_TEST_ASSERT(!fe.GetSystemClockString().empty(), "Clock string generated");
        NEMU_TEST_ASSERT(!fe.GetConsoleModeString().empty(), "Console mode string generated");
        std::cout << "  - Initial Eden library fallback discovery: PASSED" << std::endl;
    }

    // Test 2: Tab navigation via gamepad buttons (LB/RB, X, Y, B)
    {
        core::hid::XboxGamepadState input{};

        // Press X -> FileManager tab
        input.x = true;
        fe.ProcessInput(input);
        NEMU_TEST_ASSERT(fe.GetCurrentTab() == FrontendTab::FileManager, "Switched to FileManager via X");

        // Release X
        input.x = false;
        fe.ProcessInput(input);

        // Press B -> Return to Library
        input.b = true;
        fe.ProcessInput(input);
        NEMU_TEST_ASSERT(fe.GetCurrentTab() == FrontendTab::Library, "Returned to Library via B");

        input.b = false;
        fe.ProcessInput(input);

        // Press RB -> FileManager tab
        input.rb = true;
        fe.ProcessInput(input);
        NEMU_TEST_ASSERT(fe.GetCurrentTab() == FrontendTab::FileManager, "Switched to FileManager via RB");

        input.rb = false;
        fe.ProcessInput(input);

        // Press B -> Return to Library
        input.b = true;
        fe.ProcessInput(input);
        NEMU_TEST_ASSERT(fe.GetCurrentTab() == FrontendTab::Library, "Returned to Library via B");

        input.b = false;
        fe.ProcessInput(input);

        // Press Y -> Controllers shortcut
        input.y = true;
        fe.ProcessInput(input);
        NEMU_TEST_ASSERT(fe.GetCurrentTab() == FrontendTab::Controllers, "Switched to Controllers via Y");

        input.y = false;
        fe.ProcessInput(input);

        // Press B -> Return to Library
        input.b = true;
        fe.ProcessInput(input);
        NEMU_TEST_ASSERT(fe.GetCurrentTab() == FrontendTab::Library, "Returned to Library via B");
        std::cout << "  - Eden Tab navigation and shortcut switching: PASSED" << std::endl;
    }

    // Test 3: Optimizers setting adjustments
    {
        fe.SetTab(FrontendTab::Optimizers);
        NEMU_TEST_ASSERT(fe.GetCurrentTab() == FrontendTab::Optimizers, "Set tab to Optimizers");

        core::hid::XboxGamepadState input{};
        // Right arrow on row 0 -> Changes resolution scale
        input.dpad_right = true;
        fe.ProcessInput(input);
        input.dpad_right = false;
        fe.ProcessInput(input);

        // Down arrow -> row 1 (Upscaler mode)
        input.dpad_down = true;
        fe.ProcessInput(input);
        input.dpad_down = false;
        fe.ProcessInput(input);
        NEMU_TEST_ASSERT(fe.GetSelectedSettingRow() == 1, "Moved to row 1 (Upscaler)");

        // Right arrow -> Changes upscaler (FSR 1.0 -> FSR 2.0 etc.)
        input.dpad_right = true;
        fe.ProcessInput(input);
        input.dpad_right = false;
        fe.ProcessInput(input);

        std::cout << "  - Optimizers (FSR / MSAA / Frame Gen) adjustment: PASSED" << std::endl;
    }

    // Test 4: Launch request consumption
    {
        fe.SetTab(FrontendTab::Library);
        core::hid::XboxGamepadState input{};
        input.a = true;
        fe.ProcessInput(input);

        auto launch = fe.ConsumeLaunchRequest();
        NEMU_TEST_ASSERT(launch.has_value(), "Launch request recorded");
        const auto& lib = fe.GetLibrary();
        NEMU_TEST_ASSERT(*launch == lib[fe.GetSelectedGameIndex()].virtual_path, "Launch path matches selected item");

        auto second_consume = fe.ConsumeLaunchRequest();
        NEMU_TEST_ASSERT(!second_consume.has_value(), "Launch request consumed (one-shot)");
        std::cout << "  - Launch request generation and consumption: PASSED" << std::endl;
    }

    // Test 5: Graphics Optimizer verification (FSR, MSAA resolve, Frame Gen)
    {
        using namespace nemu::core::gpu::pipeline;
        constexpr u32 W = 64, H = 64;
        std::vector<u32> src(W * H, 0xFF55AAFF);
        std::vector<u32> dst(W * 2 * H * 2, 0);

        // FSR 2.0 spatial upscaler
        bool fsr_ok = GraphicsOptimizer::ApplyUpscale(
            src, W, H, dst, W * 2, H * 2, UpscalerMode::FSR_2_0, 0.8f
        );
        NEMU_TEST_ASSERT(fsr_ok, "FSR 2.0 Upscaler applied");

        // Anti-aliasing FXAA
        bool aa_ok = GraphicsOptimizer::ApplyAntiAliasing(dst, W * 2, H * 2, AntiAliasingMode::FXAA);
        NEMU_TEST_ASSERT(aa_ok, "FXAA applied");

        // 4x MSAA resolve
        std::vector<u32> ms_buffer(W * H * 4, 0xFF112233);
        std::vector<u32> resolved(W * H, 0);
        bool msaa_ok = GraphicsOptimizer::ApplyMsaaResolve(ms_buffer, resolved, W, H, 4);
        NEMU_TEST_ASSERT(msaa_ok, "4x MSAA resolved");

        // Frame Generation (2x Frame Multiplier)
        std::vector<u32> frame_prev(W * H, 0xFF000000);
        std::vector<u32> frame_curr(W * H, 0xFFFFFFFF);
        std::vector<u32> frame_inter(W * H, 0);
        bool fg_ok = GraphicsOptimizer::GenerateIntermediateFrame(frame_prev, frame_curr, frame_inter, W, H);
        NEMU_TEST_ASSERT(fg_ok, "AFMF 2x Frame Generation generated intermediate frame");

        std::cout << "  - FSR 2.0, 4x MSAA, and AFMF Frame Gen pipeline: PASSED" << std::endl;
    }

    // Test 6: Frontend rendering loop with Null GPU backend
    {
        core::gpu::NullGpuBackend null_gpu;
        NEMU_TEST_ASSERT(null_gpu.Initialize(1280, 720), "Initialize null GPU backend");
        fe.Render(null_gpu);
        NEMU_TEST_ASSERT(null_gpu.GetStats().draw_calls > 0, "Render issued draw calls");
        null_gpu.Shutdown();
        std::cout << "  - Eden / Switch UI rendering pass: PASSED" << std::endl;
    }

    // Test 7: File Manager navigation & instant ROM boot
    {
        const auto games_dir = test_dir / "sdmc" / "games";
        std::filesystem::create_directories(games_dir, ec);
        const std::string dummy_rom = "dummy_zelda_rom_content";
        std::span<const u8> rom_data(reinterpret_cast<const u8*>(dummy_rom.data()), dummy_rom.size());
        vfs.WriteFile("sdmc:/games/botw.nsp", rom_data);

        fe.SetTab(FrontendTab::FileManager);
        fe.RefreshFileManager("sdmc:/");
        NEMU_TEST_ASSERT(!fe.GetDirectoryEntries().empty(), "FileManager entries not empty");

        // Navigate into games folder
        fe.RefreshFileManager("sdmc:/games");
        const auto& entries = fe.GetDirectoryEntries();
        bool found_nsp = false;
        for (const auto& e : entries) {
            if (e.name == "botw.nsp") {
                found_nsp = true;
                NEMU_TEST_ASSERT(e.is_rom, "botw.nsp recognized as ROM");
                NEMU_TEST_ASSERT(e.format_badge == "[NSP]", "botw.nsp format badge [NSP]");
            }
        }
        NEMU_TEST_ASSERT(found_nsp, "Found botw.nsp in directory");

        // Move down to botw.nsp (index 1, below '..')
        core::hid::XboxGamepadState input{};
        input.dpad_down = true;
        fe.ProcessInput(input);
        input.dpad_down = false;
        fe.ProcessInput(input);
        NEMU_TEST_ASSERT(fe.GetSelectedFileIndex() == 1, "Selected botw.nsp");

        // Press A on the ROM -> Instant Boot
        input.a = true;
        fe.ProcessInput(input);
        auto launch = fe.ConsumeLaunchRequest();
        NEMU_TEST_ASSERT(launch.has_value(), "FileManager instant boot triggered");
        NEMU_TEST_ASSERT(*launch == "sdmc:/games/botw.nsp", "Correct launch path");
        std::cout << "  - FileManager directory exploration & instant ROM boot: PASSED" << std::endl;
    }

    // Test 8: Switch Game Options (+) Overlay Modal
    {
        fe.SetTab(FrontendTab::Library);
        core::hid::XboxGamepadState input{};

        // Press Start / Menu (+)
        input.start = true;
        fe.ProcessInput(input);
        NEMU_TEST_ASSERT(fe.IsGameOptionsOpen(), "Game Options (+) modal opened");

        input.start = false;
        fe.ProcessInput(input);

        // Press Down to row 1 (Upscaler mode), Right to change
        input.dpad_down = true;
        fe.ProcessInput(input);
        input.dpad_down = false;
        fe.ProcessInput(input);
        NEMU_TEST_ASSERT(fe.GetGameOptionsRow() == 1, "Moved to row 1 in Game Options");

        input.dpad_right = true;
        fe.ProcessInput(input);
        input.dpad_right = false;
        fe.ProcessInput(input);

        // Press B to close modal
        input.b = true;
        fe.ProcessInput(input);
        NEMU_TEST_ASSERT(!fe.IsGameOptionsOpen(), "Game Options modal closed via B");
        std::cout << "  - Switch Game Options (+) modal overlay & per-game settings: PASSED" << std::endl;
    }

    // Test 9: RetroArch-style Recursive Directory Scanner
    {
        const auto roms_dir = test_dir / "sdmc" / "switch_games";
        std::filesystem::create_directories(roms_dir / "indies", ec);
        const std::string dummy_rom = "dummy_hk_content";
        std::span<const u8> rom_data(reinterpret_cast<const u8*>(dummy_rom.data()), dummy_rom.size());
        vfs.WriteFile("sdmc:/switch_games/indies/hollow_knight.nsp", rom_data);
        vfs.WriteFile("sdmc:/switch_games/celeste.xci", rom_data);

        fe.ScanDirectory("sdmc:/switch_games");
        const auto& lib = fe.GetLibrary();
        bool found_hk = false;
        bool found_celeste = false;
        for (const auto& g : lib) {
            if (g.virtual_path.find("hollow_knight.nsp") != std::string::npos) {
                found_hk = true;
                NEMU_TEST_ASSERT(g.title == "Hollow Knight", "Recognized title name Hollow Knight");
                NEMU_TEST_ASSERT(g.title_id == 0x0100BF900806A000ULL, "Recognized Hollow Knight Title ID");
                NEMU_TEST_ASSERT(g.format_badge == "[NSP]", "Format badge [NSP]");
            }
            if (g.virtual_path.find("celeste.xci") != std::string::npos) {
                found_celeste = true;
                NEMU_TEST_ASSERT(g.format_badge == "[XCI]", "Format badge [XCI]");
            }
        }
        NEMU_TEST_ASSERT(found_hk, "Found scanned hollow_knight.nsp in library");
        NEMU_TEST_ASSERT(found_celeste, "Found scanned celeste.xci in library");
        std::cout << "  - RetroArch recursive ROM scanner with auto-detection: PASSED" << std::endl;
    }

    // Test 10: RetroArch Persistent Playlist (save:/playlist.txt)
    {
        fe.SavePlaylist();

        // Instantiate secondary frontend and verify playlist restoration
        XboxFrontend fe2(vfs, config);
        fe2.LoadPlaylist();
        const auto& lib2 = fe2.GetLibrary();
        bool restored_hk = false;
        for (const auto& g : lib2) {
            if (g.title == "Hollow Knight") {
                restored_hk = true;
                NEMU_TEST_ASSERT(g.title_id == 0x0100BF900806A000ULL, "Restored Title ID matches");
            }
        }
        NEMU_TEST_ASSERT(restored_hk, "Playlist successfully restored Hollow Knight");
        std::cout << "  - RetroArch persistent playlist save/load across reboots: PASSED" << std::endl;
    }

    // Test 11: In-Game RetroArch Quick Menu Toggle, Navigation & Commands
    {
        NEMU_TEST_ASSERT(!fe.IsQuickMenuOpen(), "Quick Menu initially closed");

        // Toggle open via Back button
        core::hid::XboxGamepadState input{};
        input.back = true;
        NEMU_TEST_ASSERT(fe.ProcessInGameInput(input), "Back button processed");
        NEMU_TEST_ASSERT(fe.IsQuickMenuOpen(), "Quick Menu opened via Back button");

        input.back = false;
        fe.ProcessInGameInput(input);
        NEMU_TEST_ASSERT(fe.IsQuickMenuOpen(), "Quick Menu remains open after button release");

        // Toggle closed via L3 + R3 stick clicks
        input.lsb = true;
        input.rsb = true;
        NEMU_TEST_ASSERT(fe.ProcessInGameInput(input), "L3+R3 combo processed");
        NEMU_TEST_ASSERT(!fe.IsQuickMenuOpen(), "Quick Menu toggled closed via L3+R3");

        input.lsb = false;
        input.rsb = false;
        fe.ProcessInGameInput(input);

        // Re-open with Back
        input.back = true;
        fe.ProcessInGameInput(input);
        input.back = false;
        fe.ProcessInGameInput(input);
        NEMU_TEST_ASSERT(fe.IsQuickMenuOpen(), "Quick Menu reopened");
        NEMU_TEST_ASSERT(fe.GetQuickMenuRow() == 0, "Row 0: Resume Game");

        // Navigate down to Row 1 (Restart Game)
        input.dpad_down = true;
        fe.ProcessInGameInput(input);
        input.dpad_down = false;
        fe.ProcessInGameInput(input);
        NEMU_TEST_ASSERT(fe.GetQuickMenuRow() == 1, "Moved to Row 1 (Restart Game)");

        // Press A -> Triggers restart request
        input.a = true;
        fe.ProcessInGameInput(input);
        input.a = false;
        fe.ProcessInGameInput(input);
        NEMU_TEST_ASSERT(fe.ConsumeRestartRequested(), "Restart request triggered and consumed");
        NEMU_TEST_ASSERT(!fe.ConsumeRestartRequested(), "Restart request one-shot");

        // Reopen Quick Menu
        input.back = true;
        fe.ProcessInGameInput(input);
        input.back = false;
        fe.ProcessInGameInput(input);
        NEMU_TEST_ASSERT(fe.IsQuickMenuOpen(), "Quick Menu reopened");

        // Navigate to Row 4 (State Slot)
        for (int i = 0; i < 4; ++i) {
            input.dpad_down = true;
            fe.ProcessInGameInput(input);
            input.dpad_down = false;
            fe.ProcessInGameInput(input);
        }
        NEMU_TEST_ASSERT(fe.GetQuickMenuRow() == 4, "Moved to Row 4 (State Slot)");
        NEMU_TEST_ASSERT(fe.GetStateSlot() == 0, "Initial slot is 0");

        // Right -> slot 1
        input.dpad_right = true;
        fe.ProcessInGameInput(input);
        input.dpad_right = false;
        fe.ProcessInGameInput(input);
        NEMU_TEST_ASSERT(fe.GetStateSlot() == 1, "State slot incremented to 1");

        // Left -> slot 0
        input.dpad_left = true;
        fe.ProcessInGameInput(input);
        input.dpad_left = false;
        fe.ProcessInGameInput(input);
        NEMU_TEST_ASSERT(fe.GetStateSlot() == 0, "State slot decremented to 0");

        // Left again -> wraps to slot 9
        input.dpad_left = true;
        fe.ProcessInGameInput(input);
        input.dpad_left = false;
        fe.ProcessInGameInput(input);
        NEMU_TEST_ASSERT(fe.GetStateSlot() == 9, "State slot wrapped to 9");

        // Navigate to Row 8 (Close Content)
        for (int i = 0; i < 4; ++i) {
            input.dpad_down = true;
            fe.ProcessInGameInput(input);
            input.dpad_down = false;
            fe.ProcessInGameInput(input);
        }
        NEMU_TEST_ASSERT(fe.GetQuickMenuRow() == 8, "Moved to Row 8 (Close Content)");

        // Press A -> Close content requested and menu closed
        input.a = true;
        fe.ProcessInGameInput(input);
        input.a = false;
        fe.ProcessInGameInput(input);
        NEMU_TEST_ASSERT(fe.ConsumeCloseGameRequested(), "Close game requested");
        NEMU_TEST_ASSERT(!fe.IsQuickMenuOpen(), "Quick Menu closed upon returning to Eden UI");

        std::cout << "  - In-Game RetroArch Quick Menu navigation, slots & close content: PASSED" << std::endl;
    }

    // Test 12: In-Game RetroArch Quick Menu Rendering pass
    {
        core::gpu::NullGpuBackend null_gpu;
        NEMU_TEST_ASSERT(null_gpu.Initialize(1280, 720), "Initialize null GPU");
        fe.SetQuickMenuOpen(true);
        fe.RenderQuickMenu(null_gpu);
        NEMU_TEST_ASSERT(null_gpu.GetStats().draw_calls > 0, "RenderQuickMenu issued draw calls");
        null_gpu.Shutdown();
        std::cout << "  - In-Game RetroArch Quick Menu rendering pass: PASSED" << std::endl;
    }

    // Clean up
    std::filesystem::remove_all(test_dir, ec);

    std::cout << "[Test: Eden / Switch UI Frontend Navigation & State Machine PASSED]" << std::endl;
    return 0;
}
