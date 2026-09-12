#include "core/system/emulator.hpp"
#include <iostream>
#include <cstdlib>
#include <filesystem>

#define NEMU_TEST_ASSERT(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "Assertion failed: " #cond " (" << (msg) << ") at " \
                      << __FILE__ << ":" << __LINE__ << std::endl; \
            std::exit(1); \
        } \
    } while (0)

using namespace nemu::core;
using namespace nemu::core::system;

int main() {
    std::cout << "[Test: Nemu Unified System Runtime & Interactive Emulator Engine]" << std::endl;

    const std::filesystem::path test_sdmc = "/tmp/nemu_emu_test_sdmc";
    const std::filesystem::path test_save = "/tmp/nemu_emu_test_save";
    std::filesystem::remove_all(test_sdmc);
    std::filesystem::remove_all(test_save);

    EmulatorConfig config{
        .render_width = 1280,
        .render_height = 720,
        .vsync = false,
        .audio_enabled = false,
        .jit_enabled = true,
        .sdmc_root = test_sdmc.string(),
        .save_root = test_save.string(),
        .title_path = ""
    };

    Emulator emu(config);

    // 1. Test Subsystem Initialization
    NEMU_TEST_ASSERT(emu.Initialize(), "Emulator must initialize cleanly");
    NEMU_TEST_ASSERT(emu.GetState() == EmulatorState::Ready, "Emulator state ready");
    NEMU_TEST_ASSERT(emu.GetVfs() != nullptr, "VFS initialized");
    NEMU_TEST_ASSERT(emu.GetGpuBackend() != nullptr, "GPU backend initialized");
    NEMU_TEST_ASSERT(emu.GetMaxwell3D() != nullptr, "Maxwell3D initialized");
    NEMU_TEST_ASSERT(emu.GetAudioBackend() != nullptr, "Audio backend initialized");
    NEMU_TEST_ASSERT(emu.GetHidManager() != nullptr, "HID manager initialized");
    NEMU_TEST_ASSERT(emu.GetProcess() != nullptr, "Process initialized");
    std::cout << "  - Subsystem initialization: PASSED" << std::endl;

    // 2. Test Builtin Demo Loading & Thread Setup
    NEMU_TEST_ASSERT(emu.LoadBuiltinDemo(), "Must load builtin demo NRO");
    NEMU_TEST_ASSERT(emu.GetMainThread() != nullptr, "Main thread created");
    NEMU_TEST_ASSERT(emu.GetProcess()->GetThreads().size() >= 1, "Thread registered in process thread list");
    std::cout << "  - Title loading & thread registration: PASSED" << std::endl;

    // 3. Test Host Xbox Controller Input Injection & Frame Stepping
    hid::XboxGamepadState test_pad{};
    test_pad.a = true;
    test_pad.thumb_lx = 12000;
    emu.GetControllerDriver()->InjectState(0, test_pad);

    emu.Start();
    NEMU_TEST_ASSERT(emu.GetState() == EmulatorState::Running, "State is Running after Start()");

    // Step 5 frames
    for (int f = 0; f < 5; ++f) {
        emu.StepFrame();
    }
    NEMU_TEST_ASSERT(emu.GetFrameCount() == 5, "Frame count == 5");
    NEMU_TEST_ASSERT(emu.GetTotalInstructions() > 0, "CPU executed instructions across frames");
    std::cout << "  - Interactive frame stepping & CPU/GPU synchronization: PASSED" << std::endl;

    // 4. Test Pause & Resume
    emu.Pause();
    NEMU_TEST_ASSERT(emu.GetState() == EmulatorState::Paused, "State is Paused");
    emu.Resume();
    NEMU_TEST_ASSERT(emu.GetState() == EmulatorState::Running, "State is Running after Resume");
    std::cout << "  - Pause / Resume lifecycle: PASSED" << std::endl;

    // 5. Test Save State & Load State
    emu.GetMainThread()->GetCpuState().SetX(5, 0x1337BEEFULL);
    NEMU_TEST_ASSERT(emu.SaveState(2), "Save state slot 2 must succeed");

    // Modify CPU state
    emu.GetMainThread()->GetCpuState().SetX(5, 0xDEADBEEFULL);
    NEMU_TEST_ASSERT(emu.GetMainThread()->GetCpuState().GetX(5) == 0xDEADBEEFULL, "CPU register modified");

    // Restore CPU state from slot 2
    NEMU_TEST_ASSERT(emu.LoadState(2), "Load state slot 2 must succeed");
    NEMU_TEST_ASSERT(emu.GetMainThread()->GetCpuState().GetX(5) == 0x1337BEEFULL, "CPU register restored from state");
    std::cout << "  - Save State / Load State serialization: PASSED" << std::endl;

    // 6. Test Clean Shutdown
    emu.Shutdown();
    NEMU_TEST_ASSERT(emu.GetState() == EmulatorState::Terminated, "State is Terminated after Shutdown");
    std::cout << "  - Clean shutdown: PASSED" << std::endl;

    std::filesystem::remove_all(test_sdmc);
    std::filesystem::remove_all(test_save);

    std::cout << "[Test: Nemu Unified System Runtime & Interactive Emulator Engine PASSED]" << std::endl;
    return 0;
}
