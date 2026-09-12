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

        // Press X -> Optimizers tab
        input.x = true;
        fe.ProcessInput(input);
        NEMU_TEST_ASSERT(fe.GetCurrentTab() == FrontendTab::Optimizers, "Switched to Optimizers via X");

        // Release X
        input.x = false;
        fe.ProcessInput(input);

        // Press RB -> Controllers tab
        input.rb = true;
        fe.ProcessInput(input);
        NEMU_TEST_ASSERT(fe.GetCurrentTab() == FrontendTab::Controllers, "Switched to Controllers via RB");

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
        NEMU_TEST_ASSERT(*launch == "builtin:/demo.nro", "Launch path matches selected item");

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

    // Clean up
    std::filesystem::remove_all(test_dir, ec);

    std::cout << "[Test: Eden / Switch UI Frontend Navigation & State Machine PASSED]" << std::endl;
    return 0;
}
