#include "pctl_service.hpp"
#include "platform/logger.hpp"
#include "core/kernel/k_handle_table.hpp"

namespace nemu::core::kernel::ipc {

PctlService::PctlService(std::string name)
    : IIpcService(std::move(name)) {}

u32 PctlService::HandleRequest(const IpcContext& ctx, const IpcRequestReader& request,
                               IpcReplyWriter& reply, u32 x_id) {
    (void)request;
    switch (x_id) {
        case CreateService:
        case CreateServiceWithoutInitialize: {
            Handle sub_handle = 0;
            if (ctx.handle_table) {
                auto session = std::make_shared<KClientSession>();
                session->SetService(std::make_shared<ParentalControlSubService>());
                sub_handle = ctx.handle_table->CreateHandle(session);
            }
            reply.Begin(static_cast<u32>(IpcCommandType::Request), 16);
            reply.Payload<u32>(0, 0); // Result Success
            reply.Payload<u32>(4, sub_handle);
            return static_cast<u32>(IpcResult::Success);
        }

        default:
            NEMU_LOG_DEBUG("pctl", "{}: Handled command 0x{:X}", GetName(), x_id);
            reply.Begin(static_cast<u32>(IpcCommandType::Request), 16);
            reply.Payload<u32>(0, 0);
            return static_cast<u32>(IpcResult::Success);
    }
}

ParentalControlSubService::ParentalControlSubService(std::string name)
    : IIpcService(std::move(name)) {}

u32 ParentalControlSubService::HandleRequest(const IpcContext& ctx, const IpcRequestReader& request,
                                            IpcReplyWriter& reply, u32 x_id) {
    (void)ctx;
    (void)request;
    switch (x_id) {
        case Initialize:
        case CheckFreeCommunicationPermission:
        case ConfirmLaunchApplicationPermission:
        case ConfirmResumeApplicationPermission:
        case ConfirmSnsPostPermission:
        case ConfirmSystemSettingsPermission: {
            reply.Begin(static_cast<u32>(IpcCommandType::Request), 8);
            reply.Payload<u32>(0, 0); // Result Success
            return static_cast<u32>(IpcResult::Success);
        }

        case IsRestrictionTemporaryUnlocked:
        case IsRestrictedSystemSettingsEntered:
        case IsRestrictionEnabled: {
            reply.Begin(static_cast<u32>(IpcCommandType::Request), 8);
            reply.Payload<u32>(0, 0); // Result Success
            reply.Payload<u8>(4, 0);  // False
            return static_cast<u32>(IpcResult::Success);
        }

        case IsFreeCommunicationAvailable: {
            reply.Begin(static_cast<u32>(IpcCommandType::Request), 8);
            reply.Payload<u32>(0, 0); // Result Success
            reply.Payload<u8>(4, 1);  // True
            return static_cast<u32>(IpcResult::Success);
        }

        case GetSafetyLevel:
        case GetCurrentSettings: {
            reply.Begin(static_cast<u32>(IpcCommandType::Request), 8);
            reply.Payload<u32>(0, 0); // Result Success
            reply.Payload<u32>(4, 0); // None
            return static_cast<u32>(IpcResult::Success);
        }

        default:
            reply.Begin(static_cast<u32>(IpcCommandType::Request), 16);
            reply.Payload<u32>(0, 0);
            return static_cast<u32>(IpcResult::Success);
    }
}

} // namespace nemu::core::kernel::ipc
