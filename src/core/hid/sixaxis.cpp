#include "sixaxis.hpp"
#include <cmath>
#include <algorithm>

namespace nemu::core::hid {

SixAxisManager::SixAxisManager() {
    for (size_t i = 0; i < MAX_SENSORS; ++i) {
        ResetToNeutral(i);
    }
}

void SixAxisManager::ResetToNeutral(size_t index) {
    if (index >= MAX_SENSORS) return;
    std::lock_guard lock(sensor_mutex_);
    sensors_[index] = SixAxisSensorState{
        .accel_x = 0.0f,
        .accel_y = 0.0f,
        .accel_z = 1.0f, // 1G standard gravity
        .gyro_x = 0.0f,
        .gyro_y = 0.0f,
        .gyro_z = 0.0f,
        .orientation_x = 0.0f,
        .orientation_y = 0.0f,
        .orientation_z = 0.0f,
        .orientation_w = 1.0f
    };
}

void SixAxisManager::UpdateSensor(size_t index, const SixAxisSensorState& state) {
    if (index >= MAX_SENSORS) return;
    std::lock_guard lock(sensor_mutex_);
    sensors_[index] = state;
}

void SixAxisManager::EmulateFromStick(size_t index, s32 stick_x, s32 stick_y, float delta_time_sec) {
    (void)delta_time_sec;
    if (index >= MAX_SENSORS) return;

    // Convert -32768..32767 stick to angular velocity (up to +-180 deg/sec)
    const float norm_x = std::clamp(static_cast<float>(stick_x) / 32767.0f, -1.0f, 1.0f);
    const float norm_y = std::clamp(static_cast<float>(stick_y) / 32767.0f, -1.0f, 1.0f);

    constexpr float MAX_GYRO_DEG_PER_SEC = 180.0f;
    const float gyro_z = norm_x * MAX_GYRO_DEG_PER_SEC; // Yaw
    const float gyro_x = -norm_y * MAX_GYRO_DEG_PER_SEC; // Pitch

    std::lock_guard lock(sensor_mutex_);
    auto& s = sensors_[index];
    s.gyro_x = gyro_x;
    s.gyro_y = 0.0f;
    s.gyro_z = gyro_z;

    // Tilt accelerometer based on stick deflection
    s.accel_x = norm_x * 0.5f;
    s.accel_y = norm_y * 0.5f;
    s.accel_z = std::sqrt(std::max(0.0f, 1.0f - (s.accel_x * s.accel_x + s.accel_y * s.accel_y)));
}

SixAxisSensorState SixAxisManager::GetSensorState(size_t index) const {
    if (index >= MAX_SENSORS) return {};
    std::lock_guard lock(sensor_mutex_);
    return sensors_[index];
}

} // namespace nemu::core::hid
