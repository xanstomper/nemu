#include "ipc_dispatcher.hpp"
#include "ipc_service.hpp"
#include "ipc_types.hpp"
#include "service_registry.hpp"
#include "core/kernel/k_process.hpp"
#include "core/kernel/k_thread.hpp"
#include "core/memory/virtual_memory.hpp"
#include "platform/logger.hpp"
#include <cstring>

namespace nemu::core::kernel::ipc {

u32 DispatchSyncRequest(KProcess& process, KThread& thread, KClientSession& session,
                        ServiceRegistry& registry) {
    auto& vmem = process.GetVirtualMemory();

    const vaddr_t buf_addr = thread.GetTlsAddress() + IpcBufferOffsetTls;
    if (!vmem.IsValidAddress(buf_addr, IpcBufferSize)) {
        NEMU_LOG_ERROR("IPC", "IPC buffer outside mapped guest memory (TLS 0x{:016X})", buf_addr);
        return static_cast<u32>(IpcResult::InvalidBuffer);
    }

    // Read the guest command buffer into a host-side copy.
    u8 guest_req[IpcBufferSize]{};
    if (!vmem.ReadBlock(buf_addr, guest_req, sizeof(guest_req))) {
        return static_cast<u32>(IpcResult::InvalidBuffer);
    }

    IpcRequestReader request(guest_req);
    IpcReply generated;
    u32 result = 0;

    // The service handler draws from the request reader and writes its reply
    // through a writer bound to the mirror buffer.
    IpcReplyWriter writer(generated.buffer);

    const auto cmd_type = static_cast<IpcCommandType>(request.GetType());
    const u32 x_id = request.GetXId();

    // Populate the HLE context from the calling process and registry.
    IpcContext ctx;
    ctx.handle_table = &process.GetHandleTable();
    ctx.memory = &process.GetVirtualMemory();
    ctx.registry = &registry;

    const auto& service = session.GetService();
    if (!service) {
        NEMU_LOG_WARN("IPC", "Session has no bound HLE service");
        result = static_cast<u32>(IpcResult::InvalidRequest);
    } else {
        switch (cmd_type) {
            case IpcCommandType::Request:
                result = service->HandleRequest(ctx, request, writer, x_id);
                break;
            case IpcCommandType::Control:
                result = service->HandleControl(ctx, request, writer, x_id);
                break;
            case IpcCommandType::Close:
                result = static_cast<u32>(IpcResult::Success);
                break;
            default:
                NEMU_LOG_WARN("IPC", "'{}' unexpected IPC command type 0x{:X}", service->GetName(),
                              request.GetType());
                result = static_cast<u32>(IpcResult::InvalidRequest);
                break;
        }
    }

    // The reply overlays the same buffer the guest wrote its request into.
    // `generated.buffer` already carries the header (type + data size) that the
    // handler's writer stamped at the start of the request.
    if (!vmem.WriteBlock(buf_addr, generated.buffer, sizeof(generated.buffer))) {
        return static_cast<u32>(IpcResult::InvalidBuffer);
    }

    NEMU_LOG_DEBUG("IPC", "'{}' x_id=0x{:X} -> result 0x{:X}", service ? service->GetName() : "<none>",
                   x_id, result);
    return result;
}

} // namespace nemu::core::kernel::ipc