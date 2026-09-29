#include "mii_service.hpp"
#include "platform/logger.hpp"
#include <cstring>

namespace nemu::core::kernel::ipc {

MiiService::MiiService(std::string name) : IIpcService(std::move(name)) {}

u32 MiiService::HandleRequest(const IpcContext& ctx, const IpcRequestReader& request,
                             IpcReplyWriter& reply, u32 cmd_id) {
    (void)ctx;
    (void)request;

    switch (cmd_id) {
        case IsFullDatabase: {
            NEMU_LOG_DEBUG("Mii", "IsFullDatabase() -> false");
            reply.Begin(static_cast<u32>(IpcCommandType::Request), 16);
            reply.Payload<u32>(0, 0); // Success
            reply.Payload<u8>(4, 0);  // IsFull = false
            return static_cast<u32>(IpcResult::Success);
        }
        case GetCount: {
            NEMU_LOG_DEBUG("Mii", "GetCount() -> 1 default Mii");
            reply.Begin(static_cast<u32>(IpcCommandType::Request), 16);
            reply.Payload<u32>(0, 0); // Success
            reply.Payload<u32>(4, 1); // Count = 1
            return static_cast<u32>(IpcResult::Success);
        }
        case Get:
        case Get1:
        case Get2:
        case GetDefault:
        case BuildRandom: {
            NEMU_LOG_DEBUG("Mii", "Get/GetDefault Mii character record (cmd 0x{:X})", cmd_id);
            reply.Begin(static_cast<u32>(IpcCommandType::Request), 16);
            reply.Payload<u32>(0, 0); // Success
            return static_cast<u32>(IpcResult::Success);
        }
        default:
            NEMU_LOG_WARN("Mii", "Unhandled cmd 0x{:X}", cmd_id);
            reply.Begin(static_cast<u32>(IpcCommandType::Request), 8);
            reply.Payload<u32>(0, 0);
            return static_cast<u32>(IpcResult::Success);
    }
}

MiiDatabaseSubService::MiiDatabaseSubService(std::string name) : IIpcService(std::move(name)) {}

u32 MiiDatabaseSubService::HandleRequest(const IpcContext& ctx, const IpcRequestReader& request,
                                         IpcReplyWriter& reply, u32 cmd_id) {
    (void)ctx;
    (void)request;

    switch (cmd_id) {
        case IsFullDatabase:
            reply.Begin(static_cast<u32>(IpcCommandType::Request), 16);
            reply.Payload<u32>(0, 0);
            reply.Payload<u8>(4, 0);
            return static_cast<u32>(IpcResult::Success);
        case GetCount:
            reply.Begin(static_cast<u32>(IpcCommandType::Request), 16);
            reply.Payload<u32>(0, 0);
            reply.Payload<u32>(4, 1);
            return static_cast<u32>(IpcResult::Success);
        default:
            reply.Begin(static_cast<u32>(IpcCommandType::Request), 8);
            reply.Payload<u32>(0, 0);
            return static_cast<u32>(IpcResult::Success);
    }
}

} // namespace nemu::core::kernel::ipc
