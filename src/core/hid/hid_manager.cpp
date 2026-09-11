#include "hid_manager.hpp"

namespace nemu::core::hid {

HidManager::HidManager() = default;

void HidManager::UpdateController(size_t player_index, const XboxGamepadState& state) {
    if (player_index >= MAX_PLAYERS) return;

    std::lock_guard lock(hid_mutex_);
    const s64 next_sample = ++sampling_numbers_[player_index];

    NpadCommonState common = ControllerMapper::MapXboxToNpad(
        state, next_sample, layout_, inner_deadzone_, outer_deadzone_);

    auto& rb = controllers_[player_index];
    const s64 head = (rb.head + 1) % 17;
    rb.entries[static_cast<size_t>(head)] = common;
    rb.head = head;
    rb.sampling_number = next_sample;
    if (rb.count < 17) {
        rb.count++;
    }
}

NpadCommonState HidManager::GetCurrentState(size_t player_index) const {
    if (player_index >= MAX_PLAYERS) return {};

    std::lock_guard lock(hid_mutex_);
    const auto& rb = controllers_[player_index];
    if (rb.count == 0) return {};
    return rb.entries[static_cast<size_t>(rb.head)];
}

NpadRingBuffer HidManager::GetRingBuffer(size_t player_index) const {
    if (player_index >= MAX_PLAYERS) return {};

    std::lock_guard lock(hid_mutex_);
    return controllers_[player_index];
}

} // namespace nemu::core::hid
