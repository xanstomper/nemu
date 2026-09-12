#include "nvdrv_service.hpp"
#include "core/kernel/k_handle_table.hpp"
#include "core/kernel/k_event.hpp"
#include "core/memory/virtual_memory.hpp"
#include "platform/logger.hpp"
#include <vector>

namespace nemu::core::kernel::ipc {

NvDrvService::NvDrvService(std::string name, std::shared_ptr<gpu::nvhost::NvDeviceManager> device_manager)
    : IIpcService(std::move(name))
    , device_manager_(std::move(device_manager)) {}

u32 NvDrvService::HandleRequest(const IpcContext& ctx, const IpcRequestReader& request,
                                IpcReplyWriter& reply, u32 x_id) {
    switch (x_id) {
        case 0: // Open
            return HandleOpen(ctx, request, reply);
        case 1: // Ioctl
            return HandleIoctl(ctx, request, reply);
        case 2: // Close
            return HandleClose(ctx, request, reply);
        case 3: // Initialize
            return HandleInitialize(ctx, request, reply);
        case 4: // QueryEvent
            return HandleQueryEvent(ctx, request, reply);
        case 8: // SetClientPID
            return HandleSetClientPID(ctx, request, reply);
        case 11: // Ioctl2
            return HandleIoctl2(ctx, request, reply);
        case 13: // Ioctl3
            return HandleIoctl3(ctx, request, reply);
        default:
            NEMU_LOG_WARN("NvDrvService", "Unhandled nvdrv cmd 0x{:X}", x_id);
            reply.Begin(static_cast<u32>(IpcCommandType::Request), 0);
            return static_cast<u32>(IpcResult::Success);
    }
}

u32 NvDrvService::HandleOpen(const IpcContext& ctx, const IpcRequestReader& request, IpcReplyWriter& reply) {
    (void)ctx;
    // Path string is sent inline or at start of payload
    auto path_view = request.ReadString(64, 0);
    size_t len = path_view.size();
    while (len > 0 && (path_view[len - 1] == '\0' || path_view[len - 1] == ' ')) {
        --len;
    }
    const std::string path(path_view.substr(0, len));

    s32 fd = -1;
    if (device_manager_) {
        fd = device_manager_->Open(path);
    }

    reply.Begin(static_cast<u32>(IpcCommandType::Request), 8);
    reply.Payload<u32>(0, static_cast<u32>(fd));
    reply.Payload<u32>(4, 0); // ResultCode
    return static_cast<u32>(IpcResult::Success);
}

u32 NvDrvService::HandleIoctl(const IpcContext& ctx, const IpcRequestReader& request, IpcReplyWriter& reply) {
    const s32 fd  = static_cast<s32>(request.Payload<u32>(0));
    const u32 cmd = request.Payload<u32>(4);

    std::vector<u8> in_buf;
    vaddr_t out_addr = 0;
    size_t out_size = 0;

    for (const auto& desc : request.GetBufferDescriptors()) {
        if (desc.type == IpcBufferType::A_Send || desc.type == IpcBufferType::X_Pointer) {
            if (ctx.memory && desc.address != 0 && desc.size > 0) {
                in_buf.resize(desc.size);
                ctx.memory->ReadBlock(desc.address, in_buf.data(), desc.size);
            }
        } else if (desc.type == IpcBufferType::B_Receive || desc.type == IpcBufferType::C_Receive) {
            out_addr = desc.address;
            out_size = desc.size;
        } else if (desc.type == IpcBufferType::W_Exchange) {
            if (ctx.memory && desc.address != 0 && desc.size > 0) {
                in_buf.resize(desc.size);
                ctx.memory->ReadBlock(desc.address, in_buf.data(), desc.size);
                out_addr = desc.address;
                out_size = desc.size;
            }
        }
    }

    if (in_buf.empty()) {
        in_buf.resize(64, 0);
        for (size_t i = 0; i < in_buf.size(); ++i) {
            in_buf[i] = request.Read<u8>(static_cast<size_t>(IpcField::Payload) + 8 + i);
        }
    }

    const size_t allocated_out = (out_size > 0) ? out_size : 64;
    std::vector<u8> out_buf(allocated_out, 0);

    u32 nv_res = 0;
    if (device_manager_) {
        nv_res = device_manager_->Ioctl(fd, cmd, in_buf, out_buf);
    }

    if (ctx.memory && out_addr != 0 && out_size > 0) {
        const size_t bytes_to_write = std::min(out_size, out_buf.size());
        ctx.memory->WriteBlock(out_addr, out_buf.data(), bytes_to_write);
    }

    const size_t inline_bytes = std::min<size_t>(out_buf.size(), 64);
    reply.Begin(static_cast<u32>(IpcCommandType::Request), 8 + inline_bytes);
    reply.Payload<u32>(0, nv_res);
    reply.Payload<u32>(4, 0);
    for (size_t i = 0; i < inline_bytes; ++i) {
        reply.Write<u8>(static_cast<size_t>(IpcField::Payload) + 8 + i, out_buf[i]);
    }
    return static_cast<u32>(IpcResult::Success);
}

u32 NvDrvService::HandleClose(const IpcContext& ctx, const IpcRequestReader& request, IpcReplyWriter& reply) {
    (void)ctx;
    const s32 fd = static_cast<s32>(request.Payload<u32>(0));
    if (device_manager_) {
        device_manager_->Close(fd);
    }
    reply.Begin(static_cast<u32>(IpcCommandType::Request), 4);
    reply.Payload<u32>(0, 0);
    return static_cast<u32>(IpcResult::Success);
}

u32 NvDrvService::HandleInitialize(const IpcContext& ctx, const IpcRequestReader& request, IpcReplyWriter& reply) {
    (void)ctx; (void)request;
    reply.Begin(static_cast<u32>(IpcCommandType::Request), 4);
    reply.Payload<u32>(0, 0);
    return static_cast<u32>(IpcResult::Success);
}

u32 NvDrvService::HandleQueryEvent(const IpcContext& ctx, const IpcRequestReader& request, IpcReplyWriter& reply) {
    (void)request;
    kernel::Handle event_handle = kernel::InvalidHandle;
    if (ctx.handle_table) {
        auto ev = std::make_shared<kernel::KEvent>(false);
        ev->Signal();
        event_handle = ctx.handle_table->CreateHandle(ev);
    }

    reply.Begin(static_cast<u32>(IpcCommandType::Request), 8);
    reply.Payload<u32>(0, 0);
    reply.Payload<u32>(4, static_cast<u32>(event_handle));
    return static_cast<u32>(IpcResult::Success);
}

u32 NvDrvService::HandleSetClientPID(const IpcContext& ctx, const IpcRequestReader& request, IpcReplyWriter& reply) {
    (void)ctx; (void)request;
    reply.Begin(static_cast<u32>(IpcCommandType::Request), 4);
    reply.Payload<u32>(0, 0);
    return static_cast<u32>(IpcResult::Success);
}

u32 NvDrvService::HandleIoctl2(const IpcContext& ctx, const IpcRequestReader& request, IpcReplyWriter& reply) {
    return HandleIoctl(ctx, request, reply);
}

u32 NvDrvService::HandleIoctl3(const IpcContext& ctx, const IpcRequestReader& request, IpcReplyWriter& reply) {
    return HandleIoctl(ctx, request, reply);
}

} // namespace nemu::core::kernel::ipc
