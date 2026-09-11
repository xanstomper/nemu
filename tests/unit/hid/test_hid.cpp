#include "core/hid/hid_types.hpp"
#include "core/hid/deadzone.hpp"
#include "core/hid/controller_mapping.hpp"
#include "core/hid/hid_manager.hpp"
#include <iostream>
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
using namespace nemu::core::hid;

int main() {
    std::cout << "[Test: Input / HID Subsystem & Gamepad Mapping]" << std::endl;

    // 1. Test DeadzoneFilter
    {
        // Zero input -> (0, 0)
        auto s0 = DeadzoneFilter::ApplyRadialDeadzone(0, 0);
        NEMU_TEST_ASSERT(s0.x == 0 && s0.y == 0, "Zero input deadzone");

        // Small drift (magnitude ~0.10, below 0.15 threshold) -> (0, 0)
        auto s1 = DeadzoneFilter::ApplyRadialDeadzone(2000, 2000); // 2000/32767 = 0.061
        NEMU_TEST_ASSERT(s1.x == 0 && s1.y == 0, "Small drift must be filtered to zero");

        // Significant input (magnitude ~0.50) -> non-zero, smoothly rescaled
        auto s2 = DeadzoneFilter::ApplyRadialDeadzone(16384, 0);
        NEMU_TEST_ASSERT(s2.x > 0 && s2.y == 0, "Active stick X must be positive");
        NEMU_TEST_ASSERT(s2.x < 16384, "Rescaled stick X should be smoothly transitioned");

        // Extreme input near max -> clamped cleanly to max range
        auto s3 = DeadzoneFilter::ApplyRadialDeadzone(32767, 0);
        NEMU_TEST_ASSERT(s3.x == 32767, "Full deflection should reach 32767");

        std::cout << "  - DeadzoneFilter radial calculations: PASSED" << std::endl;
    }

    // 2. Test ControllerMapper (Nintendo Layout vs Xbox Mirrored)
    {
        XboxGamepadState xbox{};
        xbox.b = true; // In Nintendo layout, Xbox B is Switch A!
        xbox.lb = true;
        xbox.trigger_r = 0.8f; // Triggers ZR
        xbox.start = true;    // Triggers Plus
        xbox.dpad_up = true;

        auto npad_nintendo = ControllerMapper::MapXboxToNpad(xbox, 1, FaceButtonLayout::NintendoStandard);
        NEMU_TEST_ASSERT((npad_nintendo.buttons & NpadButton::A) != 0, "Xbox B mapped to Switch A");
        NEMU_TEST_ASSERT((npad_nintendo.buttons & NpadButton::B) == 0, "Switch B not pressed");
        NEMU_TEST_ASSERT((npad_nintendo.buttons & NpadButton::L) != 0, "LB mapped to L");
        NEMU_TEST_ASSERT((npad_nintendo.buttons & NpadButton::ZR) != 0, "RT mapped to ZR");
        NEMU_TEST_ASSERT((npad_nintendo.buttons & NpadButton::Plus) != 0, "Start mapped to Plus");
        NEMU_TEST_ASSERT((npad_nintendo.buttons & NpadButton::DUp) != 0, "DPad Up mapped");

        auto npad_mirrored = ControllerMapper::MapXboxToNpad(xbox, 1, FaceButtonLayout::XboxMirrored);
        NEMU_TEST_ASSERT((npad_mirrored.buttons & NpadButton::B) != 0, "Xbox B mirrored to Switch B");
        NEMU_TEST_ASSERT((npad_mirrored.buttons & NpadButton::A) == 0, "Switch A not pressed");

        std::cout << "  - ControllerMapper layout mapping: PASSED" << std::endl;
    }

    // 3. Test HidManager Ring Buffer & Multi-Player State
    {
        HidManager mgr;
        XboxGamepadState p1{};
        p1.a = true;

        // Update 25 times to verify ring buffer wraps around 17 slots
        for (int i = 0; i < 25; ++i) {
            mgr.UpdateController(0, p1);
        }

        auto cur_p1 = mgr.GetCurrentState(0);
        NEMU_TEST_ASSERT(cur_p1.sampling_number == 25, "Sampling number should be 25");
        NEMU_TEST_ASSERT((cur_p1.buttons & NpadButton::B) != 0, "Button state present");

        auto rb = mgr.GetRingBuffer(0);
        NEMU_TEST_ASSERT(rb.count == 17, "Ring buffer should be saturated at 17 entries");
        NEMU_TEST_ASSERT(rb.sampling_number == 25, "Ring buffer sampling number 25");

        // Check player 2 is independent
        auto cur_p2 = mgr.GetCurrentState(1);
        NEMU_TEST_ASSERT(cur_p2.sampling_number == 0, "Player 2 is clean");

        std::cout << "  - HidManager multi-player & ring buffer saturation: PASSED" << std::endl;
    }

    std::cout << "[Test: Input / HID Subsystem & Gamepad Mapping PASSED]" << std::endl;
    return 0;
}
