#include "sm_service.hpp"
#include "core/kernel/k_handle_table.hpp"
#include "core/kernel/k_process.hpp"
#include "core/memory/virtual_memory.hpp"
#include "service_registry.hpp"
#include "platform/logger.hpp"
#include <string>
#include <unordered_set>

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

class GenericStubService final : public IIpcService {
public:
    explicit GenericStubService(std::string name) : IIpcService(std::move(name)) {}
    ~GenericStubService() override = default;

    u32 HandleRequest(const IpcContext& ctx, const IpcRequestReader& request,
                      IpcReplyWriter& reply, u32 x_id) override {
        (void)ctx;
        (void)request;
        NEMU_LOG_DEBUG("IPC", "GenericStubService('{}'): Handled command 0x{:X}", GetName(), x_id);
        reply.Begin(static_cast<u32>(IpcCommandType::Request), 16);
        reply.Payload<u32>(0, 0); // Result Success
        reply.Payload<u32>(4, 0);
        reply.Payload<u64>(8, 0);
        return static_cast<u32>(IpcResult::Success);
    }
};

static bool IsKnownStubService(std::string_view name) {
    static const std::unordered_set<std::string_view> stubs = {
        "aoc:u", "pctl", "pctl:a", "pctl:s", "bcat:u", "bcat:a", "bcat:m",
        "prepo:u", "prepo:a", "caps:a", "caps:c", "caps:u", "caps:su",
        "friend:u", "friend:v", "lbl", "apm", "apm:p", "apm:sys",
        "arp:r", "spsm", "bsdcfg", "ssl", "news:u", "nfc:u", "nfc:user",
        "ir:u", "ovln:rcv", "set:cal", "audio"
    };
    return stubs.find(name) != stubs.end();
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
    if ((!port || !*port) && IsKnownStubService(name)) {
        NEMU_LOG_INFO("sm:", "GetServiceHandle: creating dynamic stub service for '{}'", name);
        ctx.registry->Register(std::make_shared<GenericStubService>(name));
        port = ctx.registry->CreatePort(name);
    }

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