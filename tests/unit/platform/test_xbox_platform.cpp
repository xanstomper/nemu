#include "platform/xbox_memory_governor.hpp"
#include "platform/xbox_storage_broker.hpp"
#include "platform/xbox_thread_affinity.hpp"
#include "platform/xbox_lifecycle.hpp"
#include "core/hid/xbox_controller_driver.hpp"
#include <iostream>
#include <cassert>
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
using namespace nemu::platform;
using namespace nemu::core::hid;

int main() {
    std::cout << "=================================================" << std::endl;
    std::cout << "   NEMU XBOX SERIES S/X PLATFORM SUBSYSTEM TESTS  " << std::endl;
    std::cout << "=================================================" << std::endl;

    // -------------------------------------------------------------
    // 1. Xbox 5.1GB Memory Governor & 3-Tier Threshold Policy
    // -------------------------------------------------------------
    {
        std::cout << "[Test 1: Xbox Memory Governor & 3-Tier Hysteresis]" << std::endl;
        XboxMemoryGovernor gov;
        NEMU_TEST_ASSERT(gov.GetPressureLevel() == MemoryPressureLevel::Nominal, "Initial state nominal");

        bool texture_trimmed = false;
        bool dxgi_trimmed = false;
        bool shader_deferred = false;

        gov.RegisterTextureTrimCallback([&]() { texture_trimmed = true; });
        gov.RegisterDxgiTrimCallback([&]() { dxgi_trimmed = true; });
        gov.RegisterShaderDeferralCallback([&](bool defer) { shader_deferred = defer; });

        // Simulate 2000 MB: Nominal
        gov.SimulateCommitBytes(2000ULL * 1024 * 1024);
        NEMU_TEST_ASSERT(!gov.EvaluateAndEnforce(), "No trim under 3840MB");
        NEMU_TEST_ASSERT(gov.GetPressureLevel() == MemoryPressureLevel::Nominal, "State is Nominal");

        // Simulate 3900 MB: Elevated (above 3840 MB, below 4096 MB)
        gov.SimulateCommitBytes(3900ULL * 1024 * 1024);
        NEMU_TEST_ASSERT(!gov.EvaluateAndEnforce(), "No trim in Elevated state");
        NEMU_TEST_ASSERT(gov.GetPressureLevel() == MemoryPressureLevel::Elevated, "State is Elevated");

        // Simulate 4200 MB: Critical (exceeds 4096 MB threshold)
        gov.SimulateCommitBytes(4200ULL * 1024 * 1024);
        NEMU_TEST_ASSERT(gov.EvaluateAndEnforce(), "Trimming triggered in Critical state");
        NEMU_TEST_ASSERT(gov.GetPressureLevel() == MemoryPressureLevel::Critical, "State is Critical");
        NEMU_TEST_ASSERT(texture_trimmed, "Texture trim callback fired");
        NEMU_TEST_ASSERT(dxgi_trimmed, "DXGI trim callback fired");
        NEMU_TEST_ASSERT(shader_deferred, "Shader compilation deferred");

        // Hysteresis test: drop to 4000 MB (below 4096 MB, but above 3968 MB) -> MUST STAY CRITICAL
        texture_trimmed = false;
        gov.SimulateCommitBytes(4000ULL * 1024 * 1024);
        gov.EvaluateAndEnforce();
        NEMU_TEST_ASSERT(gov.GetPressureLevel() == MemoryPressureLevel::Critical, "Hysteresis prevents premature release");

        // Drop to 3900 MB (below 3968 MB) -> releases to Elevated
        gov.SimulateCommitBytes(3900ULL * 1024 * 1024);
        gov.EvaluateAndEnforce();
        NEMU_TEST_ASSERT(gov.GetPressureLevel() == MemoryPressureLevel::Elevated, "Released to Elevated below 3968 MB");
        NEMU_TEST_ASSERT(!shader_deferred, "Shader compilation resumed");

        // Verify HUD status string
        std::string hud = gov.FormatHudString();
        NEMU_TEST_ASSERT(hud.find("RAM:") != std::string::npos, "HUD contains RAM prefix");
        NEMU_TEST_ASSERT(hud.find("5.1GB") != std::string::npos, "HUD contains 5.1GB ceiling");

        gov.ResetSimulatedCommit();
        std::cout << "  - Memory Governor & Hysteresis: PASSED" << std::endl;
    }

    // -------------------------------------------------------------
    // 2. Xbox Brokered Storage & FutureAccessList
    // -------------------------------------------------------------
    {
        std::cout << "[Test 2: Xbox Brokered Storage & FutureAccessList]" << std::endl;
        XboxStorageBroker broker;
        auto drives = broker.EnumerateDrives();
        NEMU_TEST_ASSERT(!drives.empty(), "Drives enumerated");

        bool found_local = false;
        bool found_d = false;
        for (const auto& d : drives) {
            if (d.drive_letter == "LOCAL:/") found_local = true;
            if (d.drive_letter == "D:/") found_d = true;
        }
        NEMU_TEST_ASSERT(found_local, "LOCAL:/ drive cataloged");
        NEMU_TEST_ASSERT(found_d, "External USB D:/ drive cataloged");

        // Test direct streaming without staging
        NEMU_TEST_ASSERT(broker.CanDirectStream("D:/games/botw.nsp"), "Direct stream from USB D:/");
        NEMU_TEST_ASSERT(broker.CanDirectStream("sdmc:/switch/demo.nro"), "Direct stream from sdmc:/");

        // Test FutureAccessList persistent authorization token registration
        const std::filesystem::path test_folder = std::filesystem::current_path();
        NEMU_TEST_ASSERT(broker.RegisterFolderToken("token_usb_games", test_folder), "Token registered");
        auto resolved = broker.ResolveToken("token_usb_games");
        NEMU_TEST_ASSERT(resolved.has_value(), "Token resolved");
        NEMU_TEST_ASSERT(*resolved == test_folder, "Resolved path matches original");

        auto tokens = broker.GetRegisteredTokens();
        NEMU_TEST_ASSERT(!tokens.empty(), "Token list not empty");

        broker.ClearTokens();
        NEMU_TEST_ASSERT(broker.GetRegisteredTokens().empty(), "Tokens cleared");
        std::cout << "  - Brokered Storage & FutureAccessList: PASSED" << std::endl;
    }

    // -------------------------------------------------------------
    // 3. Xbox 6-Core Thread Affinity Governor
    // -------------------------------------------------------------
    {
        std::cout << "[Test 3: Xbox Thread Affinity Masking]" << std::endl;
        NEMU_TEST_ASSERT(XboxThreadAffinity::GetAffinityMask(XboxThreadRole::OsAndUi) == 0x03ULL, "OS/UI mask is 0x03 (Cores 0-1)");
        NEMU_TEST_ASSERT(XboxThreadAffinity::GetAffinityMask(XboxThreadRole::CpuEmulator) == 0x0CULL, "CPU JIT mask is 0x0C (Cores 2-3)");
        NEMU_TEST_ASSERT(XboxThreadAffinity::GetAffinityMask(XboxThreadRole::GpuCommandEngine) == 0x30ULL, "GPU mask is 0x30 (Cores 4-5)");
        NEMU_TEST_ASSERT(XboxThreadAffinity::GetAffinityMask(XboxThreadRole::AudioAndAuxiliary) == 0xC0ULL, "Audio mask is 0xC0 (Cores 6-7)");

        NEMU_TEST_ASSERT(XboxThreadAffinity::GetHostCoreCount() >= 1, "Core count query succeeds");
        NEMU_TEST_ASSERT(XboxThreadAffinity::PinCurrentThread(XboxThreadRole::CpuEmulator, "TestThread"), "Pinning thread succeeds");
        std::cout << "  - Thread Affinity Governor: PASSED" << std::endl;
    }

    // -------------------------------------------------------------
    // 4. Xbox UWP Application Lifecycle Manager
    // -------------------------------------------------------------
    {
        std::cout << "[Test 4: Xbox UWP Lifecycle (Suspending / Resuming)]" << std::endl;
        XboxLifecycleManager lifecycle;
        NEMU_TEST_ASSERT(lifecycle.GetState() == AppLifecycleState::Running, "Initial state running");

        bool suspend_notified = false;
        bool resume_notified = false;

        lifecycle.RegisterSuspendingCallback([&]() { suspend_notified = true; });
        lifecycle.RegisterResumingCallback([&]() { resume_notified = true; });

        lifecycle.OnSuspending();
        NEMU_TEST_ASSERT(lifecycle.GetState() == AppLifecycleState::Suspended, "State is Suspended");
        NEMU_TEST_ASSERT(suspend_notified, "Suspend hook invoked");

        lifecycle.OnResuming();
        NEMU_TEST_ASSERT(lifecycle.GetState() == AppLifecycleState::Running, "State restored to Running");
        NEMU_TEST_ASSERT(resume_notified, "Resume hook invoked");
        std::cout << "  - Application Lifecycle State Machine: PASSED" << std::endl;
    }

    // -------------------------------------------------------------
    // 5. Xbox Wireless Controller 4-Motor Impulse Triggers
    // -------------------------------------------------------------
    {
        std::cout << "[Test 5: Xbox Impulse Trigger Rumble & Thread-Safe Snapshot]" << std::endl;
        XboxControllerDriver driver;

        // Test 4-motor vibration setting
        NEMU_TEST_ASSERT(driver.SetVibration4(0, 0.5f, 0.8f, 0.3f, 0.9f), "4-motor vibration set");
        auto vib = driver.GetLastVibration(0);
        NEMU_TEST_ASSERT(std::abs(vib[0] - 0.5f) < 0.001f, "Left motor matches");
        NEMU_TEST_ASSERT(std::abs(vib[1] - 0.8f) < 0.001f, "Right motor matches");
        NEMU_TEST_ASSERT(std::abs(vib[2] - 0.3f) < 0.001f, "Left impulse trigger matches");
        NEMU_TEST_ASSERT(std::abs(vib[3] - 0.9f) < 0.001f, "Right impulse trigger matches");

        // Test decoupled snapshot
        XboxGamepadState state{};
        state.a = true;
        state.trigger_r = 1.0f;
        driver.InjectState(0, state);

        auto snap = driver.GetSnapshot(0);
        NEMU_TEST_ASSERT(snap.has_value(), "Snapshot available");
        NEMU_TEST_ASSERT(snap->a, "Snapshot button A matches");
        NEMU_TEST_ASSERT(snap->trigger_r == 1.0f, "Snapshot trigger matches");

        driver.ClearInjectedState(0);
        std::cout << "  - Xbox Impulse Triggers & Snapshots: PASSED" << std::endl;
    }

    std::cout << "=================================================" << std::endl;
    std::cout << "  ALL XBOX PLATFORM ADAPTATION TESTS PASSED!     " << std::endl;
    std::cout << "=================================================" << std::endl;
    return 0;
}
