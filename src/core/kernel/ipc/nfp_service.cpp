#include "nfp_service.hpp"
#include "platform/logger.hpp"
#include "core/kernel/k_handle_table.hpp"

namespace nemu::core::kernel::ipc {

NfpService::NfpService(std::string name) : IIpcService(std::move(name)) {}

u32 NfpService::HandleRequest(const IpcContext& ctx, const IpcRequestReader& request,
                             IpcReplyWriter& reply, u32 cmd_id) {
    (void)request;

    switch (cmd_id) {
        case CreateUserInterface: {
            NEMU_LOG_DEBUG("NFP", "CreateUserInterface() -> returning IUser session");
            Handle sub_handle = 0;
            if (ctx.handle_table) {
                auto session = std::make_shared<KClientSession>();
                session->SetService(std::make_shared<NfpUserSubService>());
                sub_handle = ctx.handle_table->CreateHandle(session);
            }
            reply.Begin(static_cast<u32>(IpcCommandType::Request), 16);
            reply.Payload<u32>(0, 0); // Result Success
            reply.Payload<u32>(4, sub_handle);
            return static_cast<u32>(IpcResult::Success);
        }
        default:
            NEMU_LOG_WARN("NFP", "Unhandled cmd 0x{:X}", cmd_id);
            reply.Begin(static_cast<u32>(IpcCommandType::Request), 8);
            reply.Payload<u32>(0, 0);
            return static_cast<u32>(IpcResult::Success);
    }
}

NfpUserSubService::NfpUserSubService(std::string name) : IIpcService(std::move(name)) {}

u32 NfpUserSubService::HandleRequest(const IpcContext& ctx, const IpcRequestReader& request,
                                     IpcReplyWriter& reply, u32 cmd_id) {
    (void)ctx;
    (void)request;

    switch (cmd_id) {
        case Initialize:
        case Finalize:
        case StartDetection:
        case StopDetection:
        case Mount:
        case Unmount:
            reply.Begin(static_cast<u32>(IpcCommandType::Request), 8);
            reply.Payload<u32>(0, 0);
            return static_cast<u32>(IpcResult::Success);
        case ListDevices: {
            NEMU_LOG_DEBUG("NFP", "IUser::ListDevices() -> 1 device active");
            reply.Begin(static_cast<u32>(IpcCommandType::Request), 16);
            reply.Payload<u32>(0, 0);       // Success
            reply.Payload<u32>(4, 1);       // 1 device count
            reply.Payload<u64>(8, 0x10001); // device handle
            return static_cast<u32>(IpcResult::Success);
        }
        case GetTagInfo:
        case GetCommonInfo:
        case GetModelInfo:
        case GetRegisterInfo: {
            reply.Begin(static_cast<u32>(IpcCommandType::Request), 16);
            reply.Payload<u32>(0, 0);
            return static_cast<u32>(IpcResult::Success);
        }
        default:
            NEMU_LOG_DEBUG("NFP", "IUser::Unhandled cmd 0x{:X}", cmd_id);
            reply.Begin(static_cast<u32>(IpcCommandType::Request), 8);
            reply.Payload<u32>(0, 0);
            return static_cast<u32>(IpcResult::Success);
    }
}

} // namespace nemu::core::kernel::ipc
