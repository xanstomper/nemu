#include "lm_service.hpp"
#include "platform/logger.hpp"
#include "core/kernel/k_handle_table.hpp"

namespace nemu::core::kernel::ipc {

LmService::LmService(std::string name) : IIpcService(std::move(name)) {}

u32 LmService::HandleRequest(const IpcContext& ctx, const IpcRequestReader& request,
                             IpcReplyWriter& reply, u32 cmd_id) {
    (void)request;

    switch (cmd_id) {
        case Initialize: {
            NEMU_LOG_DEBUG("LM", "Initialize() -> returning ILogger session");
            Handle sub_handle = 0;
            if (ctx.handle_table) {
                auto session = std::make_shared<KClientSession>();
                session->SetService(std::make_shared<LoggerSubService>());
                sub_handle = ctx.handle_table->CreateHandle(session);
            }
            reply.Begin(static_cast<u32>(IpcCommandType::Request), 16);
            reply.Payload<u32>(0, 0); // Result Success
            reply.Payload<u32>(4, sub_handle);
            return static_cast<u32>(IpcResult::Success);
        }
        default:
            NEMU_LOG_WARN("LM", "Unhandled cmd 0x{:X}", cmd_id);
            reply.Begin(static_cast<u32>(IpcCommandType::Request), 8);
            reply.Payload<u32>(0, 0);
            return static_cast<u32>(IpcResult::Success);
    }
}

LoggerSubService::LoggerSubService(std::string name) : IIpcService(std::move(name)) {}

u32 LoggerSubService::HandleRequest(const IpcContext& ctx, const IpcRequestReader& request,
                                    IpcReplyWriter& reply, u32 cmd_id) {
    (void)ctx;
    (void)request;

    switch (cmd_id) {
        case Log: {
            NEMU_LOG_DEBUG("LM", "Log() message packet received from guest process");
            reply.Begin(static_cast<u32>(IpcCommandType::Request), 8);
            reply.Payload<u32>(0, 0);
            return static_cast<u32>(IpcResult::Success);
        }
        case SetDestination: {
            NEMU_LOG_DEBUG("LM", "SetDestination() destination updated");
            reply.Begin(static_cast<u32>(IpcCommandType::Request), 8);
            reply.Payload<u32>(0, 0);
            return static_cast<u32>(IpcResult::Success);
        }
        default:
            NEMU_LOG_DEBUG("LM", "ILogger: Unhandled cmd 0x{:X}", cmd_id);
            reply.Begin(static_cast<u32>(IpcCommandType::Request), 8);
            reply.Payload<u32>(0, 0);
            return static_cast<u32>(IpcResult::Success);
    }
}

} // namespace nemu::core::kernel::ipc
