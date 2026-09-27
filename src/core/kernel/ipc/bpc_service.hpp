#pragma once

#include "ipc_service.hpp"
#include "core/kernel/k_event.hpp"
#include <string>
#include <memory>

namespace nemu::core::kernel::ipc {

/// Board Power Control Service (bpc, bpc:r, bpc:c, bpc:b, bpc:w, bpc:ams)
/// Ported from Nintendo Switch Horizon OS / Eden emulator specifications.
class BpcService final : public IIpcService {
public:
    explicit BpcService(std::string name = "bpc");
    ~BpcService() override = default;

    enum Commands : u32 {
        ShutdownSystem = 0,
        RebootSystem = 1,
        GetWakeupReason = 2,
        GetShutdownReason = 3,
        GetAcOk = 4,
        GetBoardPowerControlEvent = 5,
        GetSleepButtonState = 6,
        GetPowerEvent = 7,
        CreateWakeupTimer = 8,
        CancelWakeupTimer = 9,
        EnableWakeupTimerOnDevice = 10,
        CreateWakeupTimerEx = 11,
        GetLastEnabledWakeupTimerType = 12,
        CleanAllWakeupTimers = 13,
        GetPowerButton = 14,
        SetEnableWakeupTimer = 15,

        // bpc:r
        GetRtcTime = 0,
        SetRtcTime = 1,
        GetRtcResetDetected = 2,
        ClearRtcResetDetected = 3,
        SetUpRtcResetOnShutdown = 4,

        // bpc:ams
        RebootToFatalError = 65000,
        SetRebootPayload = 65001,
    };

    u32 HandleRequest(const IpcContext& ctx, const IpcRequestReader& request,
                      IpcReplyWriter& reply, u32 x_id) override;

    [[nodiscard]] bool IsShutdownRequested() const noexcept { return shutdown_requested_; }
    [[nodiscard]] bool IsRebootRequested() const noexcept { return reboot_requested_; }

private:
    std::shared_ptr<KEvent> power_event_;
    bool shutdown_requested_{false};
    bool reboot_requested_{false};
};

} // namespace nemu::core::kernel::ipc
