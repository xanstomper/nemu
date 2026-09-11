#include "sm_service.hpp"
#include "core/kernel/k_handle_table.hpp"
#include "core/kernel/k_process.hpp"
#include "core/memory/virtual_memory.hpp"
#include "service_registry.hpp"
#include "platform/logger.hpp"
#include <string>

namespace nemu::core::kernel::ipc {

SmService::SmService()
    : IIpcService("sm:") {}

u32 SmService::HandleRequest(const IpcContext& ctx, const IpcRequestReader& request,
                             IpcReplyWriter& reply, u32 x_id) {
    switch (x_id) {
        case Initialize:
            return HandleInitialize(ctx, reply);
        case GetServiceHandle:
            return HandleGetServiceHandle(ctx, request, reply);
        case RegisterService:
            return HandleRegisterService(ctx, request, reply);
        case UnregisterService:
            return HandleUnregisterService(ctx, request, reply);
        case GetServiceHandleDeprecated:
            return HandleGetServiceHandle(ctx, request, reply);
        default:
            NEMU_LOG_WARN("sm:", "Unhandled sm: command id 0x{:X}", x_id);
            return static_cast<u32>(IpcResult::Unimplemented);
    }
}

u32 SmService::HandleInitialize(const IpcContext& ctx, IpcReplyWriter& reply) {
    (void)ctx;
    reply.Begin(static_cast<u32>(IpcCommandType::Request), 0);
    return static_cast<u32>(IpcResult::Success);
}

/// Guest places the 8-byte (space/null zero-padded) service name at the start
/// of the IPC payload. Read it and trim padding.
static std::string ReadTrimmedServiceName(const IpcRequestReader& request) {
    auto raw = request.ReadString(8, 0);
    size_t len = raw.size();
    while (len > 0 && (raw[len - 1] == '\0' || raw[len - 1] == ' ')) {
        --len;
    }
    return std::string(raw.substr(0, len));
}

u32 SmService::HandleGetServiceHandle(const IpcContext& ctx, const IpcRequestReader& request,
                                      IpcReplyWriter& reply) {
    if (!ctx.registry || !ctx.handle_table) {
        NEMU_LOG_WARN("sm:", "GetServiceHandle called without registry/handle table context");
        return static_cast<u32>(IpcResult::InvalidBuffer);
    }

    const std::string name = ReadTrimmedServiceName(request);
    if (name.empty()) {
        return static_cast<u32>(IpcResult::InvalidRequest);
    }

    auto port = ctx.registry->CreatePort(name);
    if (!port || !*port) {
        NEMU_LOG_WARN("sm:", "GetServiceHandle: unknown service '{}'", name);
        return static_cast<u32>(IpcResult::NotFound);
    }

    const kernel::Handle handle = ctx.handle_table->CreateHandle(*port);
    if (handle == kernel::InvalidHandle) {
        return static_cast<u32>(IpcResult::OutOfMemory);
    }

    // Reply: payload carries the new handle (u32) so the guest can read it.
    reply.Begin(static_cast<u32>(IpcCommandType::Request), sizeof(u32));
    reply.Payload<u32>(0, handle);

    NEMU_LOG_DEBUG("sm:", "GetServiceHandle('{}') -> handle {}", name, handle);
    return static_cast<u32>(IpcResult::Success);
}

u32 SmService::HandleRegisterService(const IpcContext& ctx, const IpcRequestReader& request,
                                     IpcReplyWriter& reply) {
    (void)ctx;
    (void)request;
    (void)reply;
    // Guest-side dynamic service registration is not part of this HLE engine;
    // services are statically registered at boot.
    return static_cast<u32>(IpcResult::Unimplemented);
}

u32 SmService::HandleUnregisterService(const IpcContext& ctx, const IpcRequestReader& request,
                                       IpcReplyWriter& reply) {
    (void)ctx;
    (void)request;
    (void)reply;
    return static_cast<u32>(IpcResult::Unimplemented);
}

} // namespace nemu::core::kernel::ipc