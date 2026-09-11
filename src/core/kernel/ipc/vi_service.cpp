#include "vi_service.hpp"
#include "core/kernel/k_handle_table.hpp"
#include "core/kernel/k_event.hpp"
#include "platform/logger.hpp"

namespace nemu::core::kernel::ipc {

ViService::ViService(std::string name, std::shared_ptr<gpu::presentation::Nvnflinger> flinger)
    : IIpcService(std::move(name))
    , flinger_(std::move(flinger)) {}

u32 ViService::HandleRequest(const IpcContext& ctx, const IpcRequestReader& request,
                             IpcReplyWriter& reply, u32 x_id) {
    switch (x_id) {
        case 0:
        case 1:
        case 2: // GetDisplayService
            return HandleGetDisplayService(ctx, request, reply);
        default:
            NEMU_LOG_WARN("ViService", "Unhandled vi request 0x{:X}", x_id);
            reply.Begin(static_cast<u32>(IpcCommandType::Request), 0);
            return static_cast<u32>(IpcResult::Success);
    }
}

u32 ViService::HandleGetDisplayService(const IpcContext& ctx, const IpcRequestReader& request, IpcReplyWriter& reply) {
    (void)request;
    kernel::Handle session_handle = kernel::InvalidHandle;
    if (ctx.handle_table) {
        auto disp_srv = std::make_shared<ApplicationDisplayService>(flinger_);
        auto session = std::make_shared<KClientSession>();
        session->SetService(std::move(disp_srv));
        session_handle = ctx.handle_table->CreateHandle(session);
    }

    reply.Begin(static_cast<u32>(IpcCommandType::Request), 8);
    reply.Payload<u32>(0, 0); // Result
    reply.Payload<u32>(4, static_cast<u32>(session_handle));
    return static_cast<u32>(IpcResult::Success);
}

// ---------------------------------------------------------------------------
// ApplicationDisplayService
// ---------------------------------------------------------------------------

ApplicationDisplayService::ApplicationDisplayService(std::shared_ptr<gpu::presentation::Nvnflinger> flinger)
    : IIpcService("IApplicationDisplayService")
    , flinger_(std::move(flinger)) {}

u32 ApplicationDisplayService::HandleRequest(const IpcContext& ctx, const IpcRequestReader& request,
                                             IpcReplyWriter& reply, u32 x_id) {
    switch (x_id) {
        case 100: // GetRelayService
            return HandleGetRelayService(ctx, request, reply);
        case 101: // OpenDisplay
            return HandleOpenDisplay(ctx, request, reply);
        case 102: // CloseDisplay
            return HandleCloseDisplay(ctx, request, reply);
        case 2030: // CreateStrayLayer
            return HandleCreateStrayLayer(ctx, request, reply);
        case 2040: // DestroyStrayLayer
            return HandleDestroyStrayLayer(ctx, request, reply);
        case 5202: // GetDisplayVsyncEvent
            return HandleGetDisplayVsyncEvent(ctx, request, reply);
        default:
            NEMU_LOG_WARN("ApplicationDisplayService", "Unhandled cmd 0x{:X}", x_id);
            reply.Begin(static_cast<u32>(IpcCommandType::Request), 0);
            return static_cast<u32>(IpcResult::Success);
    }
}

u32 ApplicationDisplayService::HandleGetRelayService(const IpcContext& ctx, const IpcRequestReader& request, IpcReplyWriter& reply) {
    (void)request;
    kernel::Handle session_handle = kernel::InvalidHandle;
    if (ctx.handle_table) {
        auto binder_srv = std::make_shared<HosBinderDriverService>(flinger_);
        auto session = std::make_shared<KClientSession>();
        session->SetService(std::move(binder_srv));
        session_handle = ctx.handle_table->CreateHandle(session);
    }

    reply.Begin(static_cast<u32>(IpcCommandType::Request), 8);
    reply.Payload<u32>(0, 0);
    reply.Payload<u32>(4, static_cast<u32>(session_handle));
    return static_cast<u32>(IpcResult::Success);
}

u32 ApplicationDisplayService::HandleOpenDisplay(const IpcContext& ctx, const IpcRequestReader& request, IpcReplyWriter& reply) {
    (void)ctx;
    auto name_view = request.ReadString(64, 0);
    size_t len = name_view.size();
    while (len > 0 && (name_view[len - 1] == '\0' || name_view[len - 1] == ' ')) --len;
    std::string name(name_view.substr(0, len));
    if (name.empty()) name = "Default";

    u64 disp_id = 1;
    if (flinger_) {
        disp_id = flinger_->OpenDisplay(name);
    }

    reply.Begin(static_cast<u32>(IpcCommandType::Request), 12);
    reply.Payload<u32>(0, 0);
    reply.Payload<u64>(4, disp_id);
    return static_cast<u32>(IpcResult::Success);
}

u32 ApplicationDisplayService::HandleCloseDisplay(const IpcContext& ctx, const IpcRequestReader& request, IpcReplyWriter& reply) {
    (void)ctx;
    const u64 disp_id = request.Payload<u64>(0);
    if (flinger_) {
        flinger_->CloseDisplay(disp_id);
    }
    reply.Begin(static_cast<u32>(IpcCommandType::Request), 4);
    reply.Payload<u32>(0, 0);
    return static_cast<u32>(IpcResult::Success);
}

u32 ApplicationDisplayService::HandleCreateStrayLayer(const IpcContext& ctx, const IpcRequestReader& request, IpcReplyWriter& reply) {
    (void)ctx;
    const u64 disp_id = request.Payload<u64>(0);
    u64 layer_id = 1;
    if (flinger_) {
        layer_id = flinger_->CreateLayer(disp_id);
    }

    reply.Begin(static_cast<u32>(IpcCommandType::Request), 16);
    reply.Payload<u32>(0, 0);
    reply.Payload<u64>(4, layer_id);
    reply.Payload<u32>(12, static_cast<u32>(layer_id)); // binder producer id
    return static_cast<u32>(IpcResult::Success);
}

u32 ApplicationDisplayService::HandleDestroyStrayLayer(const IpcContext& ctx, const IpcRequestReader& request, IpcReplyWriter& reply) {
    (void)ctx;
    const u64 layer_id = request.Payload<u64>(0);
    if (flinger_) {
        flinger_->DestroyLayer(layer_id);
    }
    reply.Begin(static_cast<u32>(IpcCommandType::Request), 4);
    reply.Payload<u32>(0, 0);
    return static_cast<u32>(IpcResult::Success);
}

u32 ApplicationDisplayService::HandleGetDisplayVsyncEvent(const IpcContext& ctx, const IpcRequestReader& request, IpcReplyWriter& reply) {
    (void)request;
    kernel::Handle event_handle = kernel::InvalidHandle;
    if (ctx.handle_table) {
        auto ev = std::make_shared<kernel::KEvent>(true);
        ev->Signal();
        event_handle = ctx.handle_table->CreateHandle(ev);
    }

    reply.Begin(static_cast<u32>(IpcCommandType::Request), 8);
    reply.Payload<u32>(0, 0);
    reply.Payload<u32>(4, static_cast<u32>(event_handle));
    return static_cast<u32>(IpcResult::Success);
}

// ---------------------------------------------------------------------------
// HosBinderDriverService
// ---------------------------------------------------------------------------

HosBinderDriverService::HosBinderDriverService(std::shared_ptr<gpu::presentation::Nvnflinger> flinger)
    : IIpcService("IHOSBinderDriver")
    , flinger_(std::move(flinger)) {}

u32 HosBinderDriverService::HandleRequest(const IpcContext& ctx, const IpcRequestReader& request,
                                         IpcReplyWriter& reply, u32 x_id) {
    switch (x_id) {
        case 0: // TransactParcel
            return HandleTransactParcel(ctx, request, reply);
        case 1: // AdjustRefcount
            return HandleAdjustRefcount(ctx, request, reply);
        case 2: // GetNativeHandle
            return HandleGetNativeHandle(ctx, request, reply);
        default:
            NEMU_LOG_WARN("HosBinderDriverService", "Unhandled cmd 0x{:X}", x_id);
            reply.Begin(static_cast<u32>(IpcCommandType::Request), 0);
            return static_cast<u32>(IpcResult::Success);
    }
}

u32 HosBinderDriverService::HandleTransactParcel(const IpcContext& ctx, const IpcRequestReader& request, IpcReplyWriter& reply) {
    (void)ctx;
    const s32 binder_id = static_cast<s32>(request.Payload<u32>(0));
    const u32 code      = request.Payload<u32>(4);

    std::shared_ptr<gpu::presentation::BufferQueue> bq;
    if (flinger_) {
        bq = flinger_->GetBufferQueue(static_cast<u64>(binder_id));
    }

    reply.Begin(static_cast<u32>(IpcCommandType::Request), 32);
    reply.Payload<u32>(0, 0); // Result

    switch (code) {
        case 1: { // REQUEST_BUFFER
            reply.Payload<u32>(4, 0);
            break;
        }
        case 3: { // DEQUEUE_BUFFER
            s32 slot = 0;
            if (bq) {
                slot = bq->DequeueBuffer(1280, 720, gpu::PixelFormat::R8G8B8A8_UNORM);
            }
            reply.Payload<u32>(4, static_cast<u32>(slot));
            break;
        }
        case 4: { // QUEUE_BUFFER
            const s32 slot = static_cast<s32>(request.Payload<u32>(8));
            if (bq) {
                bq->QueueBuffer(slot, 0, 0);
            }
            reply.Payload<u32>(4, 0);
            break;
        }
        case 5: { // CANCEL_BUFFER
            const s32 slot = static_cast<s32>(request.Payload<u32>(8));
            if (bq) {
                bq->CancelBuffer(slot);
            }
            reply.Payload<u32>(4, 0);
            break;
        }
        case 6: { // QUERY
            reply.Payload<u32>(4, 1280); // Width
            reply.Payload<u32>(8, 720);  // Height
            break;
        }
        case 7: { // CONNECT
            if (bq) bq->Connect();
            reply.Payload<u32>(4, 0);
            break;
        }
        case 8: { // DISCONNECT
            if (bq) bq->Disconnect();
            reply.Payload<u32>(4, 0);
            break;
        }
        default:
            break;
    }

    return static_cast<u32>(IpcResult::Success);
}

u32 HosBinderDriverService::HandleAdjustRefcount(const IpcContext& ctx, const IpcRequestReader& request, IpcReplyWriter& reply) {
    (void)ctx; (void)request;
    reply.Begin(static_cast<u32>(IpcCommandType::Request), 4);
    reply.Payload<u32>(0, 0);
    return static_cast<u32>(IpcResult::Success);
}

u32 HosBinderDriverService::HandleGetNativeHandle(const IpcContext& ctx, const IpcRequestReader& request, IpcReplyWriter& reply) {
    (void)request;
    kernel::Handle ev_handle = kernel::InvalidHandle;
    if (ctx.handle_table) {
        auto ev = std::make_shared<kernel::KEvent>(true);
        ev->Signal();
        ev_handle = ctx.handle_table->CreateHandle(ev);
    }
    reply.Begin(static_cast<u32>(IpcCommandType::Request), 8);
    reply.Payload<u32>(0, 0);
    reply.Payload<u32>(4, static_cast<u32>(ev_handle));
    return static_cast<u32>(IpcResult::Success);
}

} // namespace nemu::core::kernel::ipc
