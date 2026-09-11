#include "time_service.hpp"
#include "core/kernel/k_handle_table.hpp"
#include "core/kernel/k_thread.hpp"
#include "platform/logger.hpp"
#include <chrono>

namespace nemu::core::kernel::ipc {

namespace {
    using namespace std::chrono;

    /// Return the current time in nanoseconds since the POSIX epoch.
    u64 NowUnixNs() {
        return static_cast<u64>(duration_cast<nanoseconds>(system_clock::now().time_since_epoch()).count());
    }

    /// Return a steady-clock count in nanoseconds.
    u64 SteadyNs() {
        return static_cast<u64>(duration_cast<nanoseconds>(steady_clock::now().time_since_epoch()).count());
    }
} // namespace

TimeClockService::TimeClockService(bool steady)
    : IIpcService(steady ? "time:u:steady" : "time:u:system"), steady_(steady) {}

u32 TimeClockService::HandleRequest(const IpcContext& ctx, const IpcRequestReader& request,
                                    IpcReplyWriter& reply, u32 x_id) {
    (void)ctx;
    switch (x_id) {
        case GetCurrentTime: {
            const u64 now = steady_ ? SteadyNs() : NowUnixNs();
            reply.Begin(static_cast<u32>(IpcCommandType::Request), sizeof(u64));
            reply.Payload<u64>(0, now);
            return static_cast<u32>(IpcResult::Success);
        }
        case SetCurrentTime: {
            const u64 t = request.Payload<u64>(0);
            NEMU_LOG_DEBUG("time", "SetCurrentTime({} ns) recorded (HLE no-op)", t);
            reply.Begin(static_cast<u32>(IpcCommandType::Request), 0);
            return static_cast<u32>(IpcResult::Success);
        }
        case GetSystemClockContext: {
            // SystemClockContext is 0x28 bytes: epoch offset (u64) + steady offset (u64)
            // + ... We zero-fill the fixed struct in the reply.
            reply.Begin(static_cast<u32>(IpcCommandType::Request), 0x28);
            const u64 now = steady_ ? SteadyNs() : NowUnixNs();
            reply.Payload<u64>(0, now); // system clock
            reply.Payload<u64>(8, now); // steady clock
            return static_cast<u32>(IpcResult::Success);
        }
        case SetSystemClockContext: {
            reply.Begin(static_cast<u32>(IpcCommandType::Request), 0);
            return static_cast<u32>(IpcResult::Success);
        }
        default:
            NEMU_LOG_WARN("time", "Unhandled time:u clock command id 0x{:X}", x_id);
            return static_cast<u32>(IpcResult::Unimplemented);
    }
}

TimeService::TimeService()
    : IIpcService("time:u") {}

u32 TimeService::HandleRequest(const IpcContext& ctx, const IpcRequestReader& request,
                               IpcReplyWriter& reply, u32 x_id) {
    (void)request;
    switch (x_id) {
        case GetStandardUserSystemClock:
        case GetStandardNetworkSystemClock:
        case GetStandardLocalSystemClock: {
            // User/network/local are all wall-clock based in this HLE model.
            return SpawnClock(ctx, reply, /*steady=*/false);
        }
        case GetStandardSteadyClock:
            return SpawnClock(ctx, reply, /*steady=*/true);
        case IsStandardNetworkSystemClockAccuracySufficient: {
            reply.Begin(static_cast<u32>(IpcCommandType::Request), sizeof(u8));
            reply.Payload<u8>(0, 1); // sufficient
            return static_cast<u32>(IpcResult::Success);
        }
        default:
            NEMU_LOG_WARN("time", "Unhandled time:u command id 0x{:X}", x_id);
            return static_cast<u32>(IpcResult::Unimplemented);
    }
}

u32 TimeService::SpawnClock(const IpcContext& ctx, IpcReplyWriter& reply, bool steady) {
    if (!ctx.handle_table) {
        return static_cast<u32>(IpcResult::InvalidBuffer);
    }
    auto clock = std::make_shared<TimeClockService>(steady);
    auto session = std::make_shared<KClientSession>();
    session->SetService(std::move(clock));

    const kernel::Handle handle = ctx.handle_table->CreateHandle(session);
    if (handle == kernel::InvalidHandle) {
        return static_cast<u32>(IpcResult::OutOfMemory);
    }

    reply.Begin(static_cast<u32>(IpcCommandType::Request), sizeof(u32));
    reply.Payload<u32>(0, handle);
    return static_cast<u32>(IpcResult::Success);
}

} // namespace nemu::core::kernel::ipc