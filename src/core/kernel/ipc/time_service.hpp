#pragma once

#include "ipc_service.hpp"

namespace nemu::core::kernel::ipc {

/// Time Clock interface (subservice of time:u). Mirrors libnx
/// TimeServiceObject / SystemClock APIs. Backed by the host system clock.
class TimeClockService final : public IIpcService {
public:
    explicit TimeClockService(bool steady);
    ~TimeClockService() override = default;

    enum : u32 {
        GetCurrentTime = 0x0,
        SetCurrentTime = 0x1,
        GetSystemClockContext = 0x2,
        SetSystemClockContext = 0x3,
    };

    u32 HandleRequest(const IpcContext& ctx, const IpcRequestReader& request,
                      IpcReplyWriter& reply, u32 x_id) override;

    [[nodiscard]] bool IsSteady() const noexcept { return steady_; }

private:
    bool steady_{false};
};

/// time:u service. Root interface exposing the standard clocks as subservices.
class TimeService final : public IIpcService {
public:
    TimeService();
    ~TimeService() override = default;

    enum : u32 {
        GetStandardUserSystemClock = 0x0,
        GetStandardNetworkSystemClock = 0x1,
        GetStandardSteadyClock = 0x2,
        GetStandardLocalSystemClock = 0x3,
        IsStandardNetworkSystemClockAccuracySufficient = 0x4,
    };

    u32 HandleRequest(const IpcContext& ctx, const IpcRequestReader& request,
                      IpcReplyWriter& reply, u32 x_id) override;

private:
    u32 SpawnClock(const IpcContext& ctx, IpcReplyWriter& reply, bool steady);
};

} // namespace nemu::core::kernel::ipc