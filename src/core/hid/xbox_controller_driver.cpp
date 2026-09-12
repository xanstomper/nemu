#include "xbox_controller_driver.hpp"
#include "platform/logger.hpp"
#include <algorithm>

#ifdef _WIN32
#include <windows.h>
#include <xinput.h>

typedef DWORD (WINAPI *PFN_XInputGetState)(DWORD dwUserIndex, XINPUT_STATE* pState);
typedef DWORD (WINAPI *PFN_XInputSetState)(DWORD dwUserIndex, XINPUT_VIBRATION* pVibration);
#endif

namespace nemu::core::hid {

XboxControllerDriver::XboxControllerDriver() {
    InitializeXInput();
}

XboxControllerDriver::~XboxControllerDriver() {
#ifdef _WIN32
    if (xinput_module_) {
        FreeLibrary(reinterpret_cast<HMODULE>(xinput_module_));
        xinput_module_ = nullptr;
    }
#endif
}

bool XboxControllerDriver::InitializeXInput() {
#ifdef _WIN32
    const char* dlls[] = { "xinput1_4.dll", "xinput1_3.dll", "xinput9_1_0.dll" };
    for (const char* dll : dlls) {
        HMODULE mod = LoadLibraryA(dll);
        if (mod) {
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wcast-function-type"
#endif
            auto get_state = reinterpret_cast<PFN_XInputGetState>(GetProcAddress(mod, "XInputGetState"));
            auto set_state = reinterpret_cast<PFN_XInputSetState>(GetProcAddress(mod, "XInputSetState"));
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic pop
#endif
            if (get_state) {
                xinput_module_ = mod;
                fn_get_state_ = reinterpret_cast<void*>(get_state);
                fn_set_state_ = reinterpret_cast<void*>(set_state);
                xinput_available_ = true;
                NEMU_LOG_INFO("HID", "Xbox Wireless Controller driver initialized via {}", dll);
                return true;
            }
            FreeLibrary(mod);
        }
    }
    NEMU_LOG_WARN("HID", "XInput runtime not available on system");
#endif
    return false;
}

std::optional<XboxGamepadState> XboxControllerDriver::Poll(size_t player_index) {
    if (player_index >= MAX_XBOX_CONTROLLERS) {
        return std::nullopt;
    }

    if (has_injected_[player_index]) {
        return injected_state_[player_index];
    }

#ifdef _WIN32
    if (xinput_available_ && fn_get_state_) {
        auto get_state = reinterpret_cast<PFN_XInputGetState>(fn_get_state_);
        XINPUT_STATE xs{};
        DWORD res = get_state(static_cast<DWORD>(player_index), &xs);
        if (res == ERROR_SUCCESS) {
            connected_[player_index] = true;
            XboxGamepadState state{};
            state.a = (xs.Gamepad.wButtons & XINPUT_GAMEPAD_A) != 0;
            state.b = (xs.Gamepad.wButtons & XINPUT_GAMEPAD_B) != 0;
            state.x = (xs.Gamepad.wButtons & XINPUT_GAMEPAD_X) != 0;
            state.y = (xs.Gamepad.wButtons & XINPUT_GAMEPAD_Y) != 0;
            state.dpad_up = (xs.Gamepad.wButtons & XINPUT_GAMEPAD_DPAD_UP) != 0;
            state.dpad_down = (xs.Gamepad.wButtons & XINPUT_GAMEPAD_DPAD_DOWN) != 0;
            state.dpad_left = (xs.Gamepad.wButtons & XINPUT_GAMEPAD_DPAD_LEFT) != 0;
            state.dpad_right = (xs.Gamepad.wButtons & XINPUT_GAMEPAD_DPAD_RIGHT) != 0;
            state.start = (xs.Gamepad.wButtons & XINPUT_GAMEPAD_START) != 0;
            state.back = (xs.Gamepad.wButtons & XINPUT_GAMEPAD_BACK) != 0;
            state.lb = (xs.Gamepad.wButtons & XINPUT_GAMEPAD_LEFT_SHOULDER) != 0;
            state.rb = (xs.Gamepad.wButtons & XINPUT_GAMEPAD_RIGHT_SHOULDER) != 0;
            state.lsb = (xs.Gamepad.wButtons & XINPUT_GAMEPAD_LEFT_THUMB) != 0;
            state.rsb = (xs.Gamepad.wButtons & XINPUT_GAMEPAD_RIGHT_THUMB) != 0;
            state.trigger_l = static_cast<float>(xs.Gamepad.bLeftTrigger) / 255.0f;
            state.trigger_r = static_cast<float>(xs.Gamepad.bRightTrigger) / 255.0f;
            state.thumb_lx = xs.Gamepad.sThumbLX;
            state.thumb_ly = xs.Gamepad.sThumbLY;
            state.thumb_rx = xs.Gamepad.sThumbRX;
            state.thumb_ry = xs.Gamepad.sThumbRY;
            return state;
        } else {
            connected_[player_index] = false;
            return std::nullopt;
        }
    }
#endif

    return std::nullopt;
}

bool XboxControllerDriver::IsConnected(size_t player_index) const noexcept {
    if (player_index >= MAX_XBOX_CONTROLLERS) {
        return false;
    }
    return connected_[player_index];
}

bool XboxControllerDriver::SetVibration(size_t player_index, float low_freq_motor, float high_freq_motor) {
#ifdef _WIN32
    if (xinput_available_ && fn_set_state_ && player_index < MAX_XBOX_CONTROLLERS) {
        auto set_state = reinterpret_cast<PFN_XInputSetState>(fn_set_state_);
        XINPUT_VIBRATION vib{};
        vib.wLeftMotorSpeed = static_cast<WORD>(std::clamp(low_freq_motor, 0.0f, 1.0f) * 65535.0f);
        vib.wRightMotorSpeed = static_cast<WORD>(std::clamp(high_freq_motor, 0.0f, 1.0f) * 65535.0f);
        return set_state(static_cast<DWORD>(player_index), &vib) == ERROR_SUCCESS;
    }
#endif
    (void)player_index;
    (void)low_freq_motor;
    (void)high_freq_motor;
    return false;
}

void XboxControllerDriver::InjectState(size_t player_index, const XboxGamepadState& state) {
    if (player_index < MAX_XBOX_CONTROLLERS) {
        injected_state_[player_index] = state;
        has_injected_[player_index] = true;
        connected_[player_index] = true;
    }
}

void XboxControllerDriver::SetConnected(size_t player_index, bool connected) {
    if (player_index < MAX_XBOX_CONTROLLERS) {
        connected_[player_index] = connected;
    }
}

void XboxControllerDriver::ClearInjectedState(size_t player_index) {
    if (player_index < MAX_XBOX_CONTROLLERS) {
        has_injected_[player_index] = false;
        injected_state_[player_index] = {};
    }
}

} // namespace nemu::core::hid
