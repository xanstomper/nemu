#include "ldr_ro_service.hpp"
#include "platform/logger.hpp"

namespace nemu::core::kernel::ipc {

LdrRoService::LdrRoService(std::string name) : IIpcService(std::move(name)) {}

u32 LdrRoService::HandleRequest(const IpcContext& ctx, const IpcRequestReader& request,
                               IpcReplyWriter& reply, u32 cmd_id) {
    (void)ctx;
    (void)request;

    switch (cmd_id) {
        case Initialize:
            NEMU_LOG_DEBUG("LdrRo", "ldr:ro Initialize() called");
            reply.Begin(static_cast<u32>(IpcCommandType::Request), 8);
            reply.Payload<u32>(0, 0);
            return static_cast<u32>(IpcResult::Success);
        case LoadNro: {
            NEMU_LOG_INFO("LdrRo", "ldr:ro LoadNro() called - mapping base address");
            reply.Begin(static_cast<u32>(IpcCommandType::Request), 16);
            reply.Payload<u32>(0, 0); // Success
            reply.Payload<u64>(4, 0x0060000000ULL); // Synthetic loaded base address
            return static_cast<u32>(IpcResult::Success);
        }
        case UnloadNro:
        case LoadNrr:
        case UnloadNrr:
            reply.Begin(static_cast<u32>(IpcCommandType::Request), 8);
            reply.Payload<u32>(0, 0);
            return static_cast<u32>(IpcResult::Success);
        default:
            NEMU_LOG_WARN("LdrRo", "Unhandled cmd 0x{:X}", cmd_id);
            reply.Begin(static_cast<u32>(IpcCommandType::Request), 8);
            reply.Payload<u32>(0, 0);
            return static_cast<u32>(IpcResult::Success);
    }
}

} // namespace nemu::core::kernel::ipc
