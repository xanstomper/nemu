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
        GetCurrentTimePoint = 0x1,  // steady clock: 0x18-byte SteadyClockTimePoint
        SetCurrentTime = 0x2,
        GetSystemClockContext = 0x3,
        SetSystemClockContext = 0x4,
    };

    u32 HandleRequest(const IpcContext& ctx, const IpcRequestReader& request,
                      IpcReplyWriter& reply, u32 x_id) override;

    [[nodiscard]] bool IsSteady() const noexcept { return steady_; }

private:
    bool steady_{false};
};

/// TimeZone interface (subservice of time:u / time:s).
class TimeZoneService final : public IIpcService {
public:
    TimeZoneService();
    ~TimeZoneService() override = default;

    enum : u32 {
        GetDeviceLocationName = 0x0,
        SetDeviceLocationName = 0x1,
        GetTotalLocationNameCount = 0x2,
        LoadLocationNameList = 0x3,
        LoadTimeZoneRule = 0x4,
        ToCalendarTime = 0x64,               // 100
        ToCalendarTimeWithMyRule = 0x65,     // 101
        ToPosixTime = 0xC8,                  // 200
        ToPosixTimeWithMyRule = 0xC9,        // 201
    };

    u32 HandleRequest(const IpcContext& ctx, const IpcRequestReader& request,
                      IpcReplyWriter& reply, u32 x_id) override;
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
        GetTimeZoneService = 0x3,
        GetStandardLocalSystemClock = 0x4,
        IsStandardNetworkSystemClockAccuracySufficient = 0x5,
        CalculateMonotonicSystemClockToBaseTimePoint = 0x64, // 100
        GetSharedMemoryNativeHandle = 0xC8,                 // 200
    };

    u32 HandleRequest(const IpcContext& ctx, const IpcRequestReader& request,
                      IpcReplyWriter& reply, u32 x_id) override;

private:
    u32 SpawnClock(const IpcContext& ctx, IpcReplyWriter& reply, bool steady);
    u32 SpawnTimeZone(const IpcContext& ctx, IpcReplyWriter& reply);
};

} // namespace nemu::core::kernel::ipc