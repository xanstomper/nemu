#include "bcat_service.hpp"
#include "platform/logger.hpp"
#include "core/kernel/k_handle_table.hpp"

namespace nemu::core::kernel::ipc {

BcatService::BcatService(std::string name) : IIpcService(std::move(name)) {}

u32 BcatService::HandleRequest(const IpcContext& ctx, const IpcRequestReader& request,
                             IpcReplyWriter& reply, u32 cmd_id) {
    (void)request;

    switch (cmd_id) {
        case CreateDeliveryCacheStorageService: {
            NEMU_LOG_DEBUG("BCAT", "CreateDeliveryCacheStorageService() -> returning storage session");
            Handle sub_handle = 0;
            if (ctx.handle_table) {
                auto session = std::make_shared<KClientSession>();
                session->SetService(std::make_shared<DeliveryCacheStorageSubService>());
                sub_handle = ctx.handle_table->CreateHandle(session);
            }
            reply.Begin(static_cast<u32>(IpcCommandType::Request), 16);
            reply.Payload<u32>(0, 0); // Success
            reply.Payload<u32>(4, sub_handle);
            return static_cast<u32>(IpcResult::Success);
        }
        case CreateDeliveryCacheProgressService: {
            NEMU_LOG_DEBUG("BCAT", "CreateDeliveryCacheProgressService() stub");
            reply.Begin(static_cast<u32>(IpcCommandType::Request), 8);
            reply.Payload<u32>(0, 0);
            return static_cast<u32>(IpcResult::Success);
        }
        default:
            NEMU_LOG_WARN("BCAT", "Unhandled cmd 0x{:X}", cmd_id);
            reply.Begin(static_cast<u32>(IpcCommandType::Request), 8);
            reply.Payload<u32>(0, 0);
            return static_cast<u32>(IpcResult::Success);
    }
}

DeliveryCacheStorageSubService::DeliveryCacheStorageSubService(std::string name) : IIpcService(std::move(name)) {}

u32 DeliveryCacheStorageSubService::HandleRequest(const IpcContext& ctx, const IpcRequestReader& request,
                                                  IpcReplyWriter& reply, u32 cmd_id) {
    (void)ctx;
    (void)request;

    switch (cmd_id) {
        case CreateDeliveryCacheDirectoryService:
        case CreateDeliveryCacheFileService:
            NEMU_LOG_DEBUG("BCAT", "DeliveryCacheStorageSubService cmd 0x{:X}", cmd_id);
            reply.Begin(static_cast<u32>(IpcCommandType::Request), 8);
            reply.Payload<u32>(0, 0);
            return static_cast<u32>(IpcResult::Success);
        case EnumerateDeliveryCacheDirectory:
            reply.Begin(static_cast<u32>(IpcCommandType::Request), 16);
            reply.Payload<u32>(0, 0); // Success
            reply.Payload<u32>(4, 0); // 0 delivery entries
            return static_cast<u32>(IpcResult::Success);
        default:
            reply.Begin(static_cast<u32>(IpcCommandType::Request), 8);
            reply.Payload<u32>(0, 0);
            return static_cast<u32>(IpcResult::Success);
    }
}

} // namespace nemu::core::kernel::ipc
