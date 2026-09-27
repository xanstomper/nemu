#include "prepo_service.hpp"
#include "platform/logger.hpp"

namespace nemu::core::kernel::ipc {

PrepoService::PrepoService(std::string name)
    : IIpcService(std::move(name)) {}

u32 PrepoService::HandleRequest(const IpcContext& ctx, const IpcRequestReader& request,
                               IpcReplyWriter& reply, u32 x_id) {
    (void)ctx;
    (void)request;
    switch (x_id) {
        case SaveReportOld:
        case SaveReportWithUserOld:
        case SaveReportOld2:
        case SaveReportWithUserOld2:
        case SaveReport:
        case SaveReportWithUser:
        case SaveSystemReport: {
            report_count_++;
            reply.Begin(static_cast<u32>(IpcCommandType::Request), 8);
            reply.Payload<u32>(0, 0); // Result Success
            return static_cast<u32>(IpcResult::Success);
        }

        case RequestImmediateTransmission: {
            reply.Begin(static_cast<u32>(IpcCommandType::Request), 8);
            reply.Payload<u32>(0, 0); // Result Success
            return static_cast<u32>(IpcResult::Success);
        }

        case GetTransmissionStatus: {
            reply.Begin(static_cast<u32>(IpcCommandType::Request), 8);
            reply.Payload<u32>(0, 0); // Result Success
            reply.Payload<u32>(4, 0); // Status = Ready
            return static_cast<u32>(IpcResult::Success);
        }

        case GetSystemSessionId: {
            reply.Begin(static_cast<u32>(IpcCommandType::Request), 16);
            reply.Payload<u32>(0, 0); // Result Success
            reply.Payload<u64>(8, 0x505245504F5F5345ULL); // "PREPO_SE"
            return static_cast<u32>(IpcResult::Success);
        }

        default:
            NEMU_LOG_DEBUG("prepo", "{}: Handled command 0x{:X}", GetName(), x_id);
            reply.Begin(static_cast<u32>(IpcCommandType::Request), 16);
            reply.Payload<u32>(0, 0);
            return static_cast<u32>(IpcResult::Success);
    }
}

} // namespace nemu::core::kernel::ipc
