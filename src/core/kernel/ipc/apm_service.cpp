#include "apm_service.hpp"
#include "platform/logger.hpp"
#include "core/kernel/k_handle_table.hpp"

namespace nemu::core::kernel::ipc {

ApmService::ApmService(std::string name)
    : IIpcService(std::move(name)) {}

u32 ApmService::HandleRequest(const IpcContext& ctx, const IpcRequestReader& request,
                              IpcReplyWriter& reply, u32 x_id) {
    (void)request;
    switch (x_id) {
        case OpenSession: {
            Handle session_handle = 0;
            if (ctx.handle_table) {
                auto session = std::make_shared<KClientSession>();
                session->SetService(std::make_shared<ApmSessionService>());
                session_handle = ctx.handle_table->CreateHandle(session);
            }
            reply.Begin(static_cast<u32>(IpcCommandType::Request), 16);
            reply.Payload<u32>(0, 0); // Result Success
            reply.Payload<u32>(4, session_handle);
            return static_cast<u32>(IpcResult::Success);
        }

        case GetPerformanceMode: {
            reply.Begin(static_cast<u32>(IpcCommandType::Request), 8);
            reply.Payload<u32>(0, 0); // Result Success
            reply.Payload<u32>(4, static_cast<u32>(mode_));
            return static_cast<u32>(IpcResult::Success);
        }

        case IsCpuOverclockEnabled: {
            reply.Begin(static_cast<u32>(IpcCommandType::Request), 8);
            reply.Payload<u32>(0, 0); // Result Success
            reply.Payload<u8>(4, 0);  // Overclock false
            return static_cast<u32>(IpcResult::Success);
        }

        default:
            NEMU_LOG_DEBUG("apm", "{}: Handled command 0x{:X}", GetName(), x_id);
            reply.Begin(static_cast<u32>(IpcCommandType::Request), 16);
            reply.Payload<u32>(0, 0);
            return static_cast<u32>(IpcResult::Success);
    }
}

ApmSessionService::ApmSessionService(std::string name)
    : IIpcService(std::move(name)) {}

u32 ApmSessionService::HandleRequest(const IpcContext& ctx, const IpcRequestReader& request,
                                     IpcReplyWriter& reply, u32 x_id) {
    (void)ctx;
    switch (x_id) {
        case SetPerformanceConfiguration: {
            config_ = request.Payload<u32>(4);
            reply.Begin(static_cast<u32>(IpcCommandType::Request), 8);
            reply.Payload<u32>(0, 0);
            return static_cast<u32>(IpcResult::Success);
        }

        case GetPerformanceConfiguration: {
            reply.Begin(static_cast<u32>(IpcCommandType::Request), 8);
            reply.Payload<u32>(0, 0);
            reply.Payload<u32>(4, config_);
            return static_cast<u32>(IpcResult::Success);
        }

        case SetCpuOverclockEnabled: {
            reply.Begin(static_cast<u32>(IpcCommandType::Request), 8);
            reply.Payload<u32>(0, 0);
            return static_cast<u32>(IpcResult::Success);
        }

        default:
            reply.Begin(static_cast<u32>(IpcCommandType::Request), 16);
            reply.Payload<u32>(0, 0);
            return static_cast<u32>(IpcResult::Success);
    }
}

ApmSysService::ApmSysService(std::string name)
    : IIpcService(std::move(name)),
      perf_event_(std::make_shared<KEvent>(/*auto_clear=*/false)) {}

u32 ApmSysService::HandleRequest(const IpcContext& ctx, const IpcRequestReader& request,
                                 IpcReplyWriter& reply, u32 x_id) {
    (void)request;
    switch (x_id) {
        case GetPerformanceEvent: {
            Handle event_handle = 0;
            if (ctx.handle_table) {
                event_handle = ctx.handle_table->CreateHandle(perf_event_);
            }
            reply.Begin(static_cast<u32>(IpcCommandType::Request), 16);
            reply.Payload<u32>(0, 0);
            reply.Payload<u32>(4, event_handle);
            return static_cast<u32>(IpcResult::Success);
        }

        case GetCurrentPerformanceConfiguration: {
            reply.Begin(static_cast<u32>(IpcCommandType::Request), 8);
            reply.Payload<u32>(0, 0);
            reply.Payload<u32>(4, 0x00010001);
            return static_cast<u32>(IpcResult::Success);
        }

        case SetCpuBoostMode: {
            reply.Begin(static_cast<u32>(IpcCommandType::Request), 8);
            reply.Payload<u32>(0, 0);
            return static_cast<u32>(IpcResult::Success);
        }

        default:
            reply.Begin(static_cast<u32>(IpcCommandType::Request), 16);
            reply.Payload<u32>(0, 0);
            return static_cast<u32>(IpcResult::Success);
    }
}

} // namespace nemu::core::kernel::ipc
