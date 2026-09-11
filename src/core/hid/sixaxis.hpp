#pragma once

#include "hid_types.hpp"
#include <array>
#include <mutex>

namespace nemu::core::hid {

class SixAxisManager {
public:
    static constexpr size_t MAX_SENSORS = 8;

    SixAxisManager();
    ~SixAxisManager() = default;

    /// Update physical IMU sensor state
    void UpdateSensor(size_t index, const SixAxisSensorState& state);

    /// Emulate 6-axis gyro/accel motion using analog stick coordinates
    void EmulateFromStick(size_t index, s32 stick_x, s32 stick_y, float delta_time_sec = 1.0f / 60.0f);

    /// Get current sensor state
    [[nodiscard]] SixAxisSensorState GetSensorState(size_t index) const;

    /// Reset to neutral flat orientation (1G down Z-axis)
    void ResetToNeutral(size_t index);

private:
    mutable std::mutex sensor_mutex_;
    std::array<SixAxisSensorState, MAX_SENSORS> sensors_{};
};

} // namespace nemu::core::hid
