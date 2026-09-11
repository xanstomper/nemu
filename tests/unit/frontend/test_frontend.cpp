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
    std::cout << "[Test: XboxFrontend Navigation & State Machine]" << std::endl;

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
        NEMU_TEST_ASSERT(fe.GetCurrentView() == FrontendView::Library, "Default view is Library");
        std::cout << "  - Initial library fallback discovery: PASSED" << std::endl;
    }

    // Test 2: View navigation via gamepad buttons
    {
        core::hid::XboxGamepadState input{};

        // Press X -> Settings view
        input.x = true;
        fe.ProcessInput(input);
        NEMU_TEST_ASSERT(fe.GetCurrentView() == FrontendView::Settings, "Switched to Settings via X");

        // Release X
        input.x = false;
        fe.ProcessInput(input);

        // Press A in Settings -> Toggle VSync
        bool initial_vsync = config.GetConfig().vsync;
        input.a = true;
        fe.ProcessInput(input);
        NEMU_TEST_ASSERT(config.GetConfig().vsync != initial_vsync, "Toggled VSync via A");

        input.a = false;
        fe.ProcessInput(input);

        // Press Y -> Diagnostics view
        input.y = true;
        fe.ProcessInput(input);
        NEMU_TEST_ASSERT(fe.GetCurrentView() == FrontendView::Diagnostics, "Switched to Diagnostics via Y");

        input.y = false;
        fe.ProcessInput(input);

        // Press B -> Return to Library
        input.b = true;
        fe.ProcessInput(input);
        NEMU_TEST_ASSERT(fe.GetCurrentView() == FrontendView::Library, "Returned to Library via B");
        std::cout << "  - View switching and settings interaction: PASSED" << std::endl;
    }

    // Test 3: Launch request consumption
    {
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

    // Test 4: Frontend rendering loop with Null GPU backend
    {
        core::gpu::NullGpuBackend null_gpu;
        NEMU_TEST_ASSERT(null_gpu.Initialize(1280, 720), "Initialize null GPU backend");
        fe.Render(null_gpu);
        NEMU_TEST_ASSERT(null_gpu.GetStats().draw_calls > 0, "Render issued draw calls");
        null_gpu.Shutdown();
        std::cout << "  - Headless GPU rendering pass: PASSED" << std::endl;
    }

    // Clean up
    std::filesystem::remove_all(test_dir, ec);

    std::cout << "[Test: XboxFrontend Navigation & State Machine PASSED]" << std::endl;
    return 0;
}
