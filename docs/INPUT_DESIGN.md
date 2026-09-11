# Nemu Input & Controller Subsystem Architecture

## 1. Scope & Objectives

The input subsystem bridges physical host controllers (Xbox Wireless Controllers on Series S/X; standard gamepads/keyboards on PC) into the Nintendo Switch Human Interface Device (HID) state format.

Objectives:
* Zero perceptible input latency (< 4 ms sampling).
* Full mapping of all Switch digital and analog controls.
* Configurable analog deadzones and stick response curves.
* Multi-controller support (up to 8 players).

---

## 2. Switch HID Memory Layout & Ring Buffers

Switch titles read controller inputs directly from a 4 MiB kernel shared memory region (`KSharedMemory`) mapped read-only into the process address space.

### 2.1 Shared Memory Structure
The shared memory contains ring buffers for each controller style:
* **DebugPad:** Development debug controller.
* **TouchScreen:** Multi-touch capacitive screen state (up to 16 touch points).
* **Mouse / Keyboard:** External peripherals.
* **Npad:** Primary game controller states:
  * Handheld (Integrated Joy-Con pair)
  * Joy-Con Dual (Detached Joy-Cons acting as one)
  * Joy-Con Left / Joy-Con Right (Single horizontal Joy-Con)
  * Pro Controller (Standard Nintendo Switch Pro Controller)

### 2.2 Ring Buffer Entry Format
```cpp
struct NpadCommonState {
    int64_t sampling_number;
    uint64_t buttons;
    int32_t analog_stick_l_x;
    int32_t analog_stick_l_y;
    int32_t analog_stick_r_x;
    int32_t analog_stick_r_y;
    uint32_t attributes;
};
```

---

## 3. Button & Axis Mapping Table

| Nintendo Switch Input | Xbox Controller Equivalent |
| :--- | :--- |
| **A Button (Right)** | **B Button** (Xbox Standard Face Right) or Remapped to **A** |
| **B Button (Bottom)**| **A Button** (Xbox Standard Face Bottom) or Remapped to **B** |
| **X Button (Top)** | **Y Button** |
| **Y Button (Left)** | **X Button** |
| **D-Pad Up / Down / Left / Right** | **D-Pad Up / Down / Left / Right** |
| **L / R (Bumpers)** | **Left Bumper (LB) / Right Bumper (RB)** |
| **ZL / ZR (Triggers)** | **Left Trigger (LT) / Right Trigger (RT)** |
| **Left Stick Click (L3)** | **Left Thumbstick Click (LSB)** |
| **Right Stick Click (R3)** | **Right Thumbstick Click (RSB)** |
| **Plus (+)** | **Menu / Start Button** |
| **Minus (-)** | **View / Back Button** |
| **Home Button** | Guided through Xbox Guide button or UI overlay shortcut |
| **Capture Button** | Configurable combo (e.g. `LB + RB + View`) |

---

## 4. Analog Deadzone & Normalization Algorithm

Raw Xbox controller sticks provide signed 16-bit integers (`-32768` to `+32767`). The Switch expects coordinates in the range `[-32767, +32767]`.

To eliminate joystick drift without sacrificing responsiveness, Nemu employs a radial deadzone filter:

```cpp
struct StickCoord { float x; float y; };

StickCoord ApplyRadialDeadzone(float raw_x, float raw_y, float inner_deadzone, float outer_deadzone) {
    float magnitude = std::sqrt(raw_x * raw_x + raw_y * raw_y);
    if (magnitude < inner_deadzone) {
        return { 0.0f, 0.0f };
    }
    float normalized_mag = std::min(1.0f, (magnitude - inner_deadzone) / (outer_deadzone - inner_deadzone));
    float scale = normalized_mag / magnitude;
    return { raw_x * scale, raw_y * scale };
}
```
