#include "nifm_service.hpp"
#include "core/kernel/k_handle_table.hpp"
#include "platform/logger.hpp"

namespace nemu::core::kernel::ipc {

// ---------------------------------------------------------------------------
// RequestService (nifm:IRequest)
// ---------------------------------------------------------------------------

RequestService::RequestService()
    : IIpcService("nifm:IRequest") {
    completion_event_ = std::make_shared<KEvent>(true); // signaled
    completion_event_->Signal();
}

u32 RequestService::HandleRequest(const IpcContext& ctx, const IpcRequestReader& request,
                                  IpcReplyWriter& reply, u32 x_id) {
    (void)request;
    switch (x_id) {
        case GetRequestState: {
            reply.Begin(static_cast<u32>(IpcCommandType::Request), 8);
            reply.Payload<u32>(0, static_cast<u32>(IpcResult::Success));
            reply.Payload<u32>(4, 1); // 1 = Complete
            return static_cast<u32>(IpcResult::Success);
        }

        case GetResult: {
            reply.Begin(static_cast<u32>(IpcCommandType::Request), 4);
            reply.Payload<u32>(0, static_cast<u32>(IpcResult::Success));
            return static_cast<u32>(IpcResult::Success);
        }

        case GetSystemEventReadableHandle: {
            Handle event_handle = 0;
            if (ctx.handle_table && completion_event_) {
                event_handle = ctx.handle_table->CreateHandle(completion_event_);
            }
            reply.Begin(static_cast<u32>(IpcCommandType::Request), 8);
            reply.Payload<u32>(0, static_cast<u32>(IpcResult::Success));
            reply.Payload<u32>(4, event_handle);
            return static_cast<u32>(IpcResult::Success);
        }

        case Cancel:
        case Submit:
        case SetRequirement:
        case SetRequirementPreset: {
            reply.Begin(static_cast<u32>(IpcCommandType::Request), 4);
            reply.Payload<u32>(0, static_cast<u32>(IpcResult::Success));
            return static_cast<u32>(IpcResult::Success);
        }

        default:
            NEMU_LOG_DEBUG("nifm", "IRequest: stubbing command 0x{:X}", x_id);
            reply.Begin(static_cast<u32>(IpcCommandType::Request), 4);
            reply.Payload<u32>(0, static_cast<u32>(IpcResult::Success));
            return static_cast<u32>(IpcResult::Success);
    }
}

// ---------------------------------------------------------------------------
// GeneralService (nifm:IGeneralService)
// ---------------------------------------------------------------------------

GeneralService::GeneralService()
    : IIpcService("nifm:IGeneralService") {}

u32 GeneralService::HandleRequest(const IpcContext& ctx, const IpcRequestReader& request,
                                  IpcReplyWriter& reply, u32 x_id) {
    (void)request;
    switch (x_id) {
        case GetClientId: {
            reply.Begin(static_cast<u32>(IpcCommandType::Request), 20);
            reply.Payload<u32>(0, static_cast<u32>(IpcResult::Success));
            // 16-byte dummy client ID
            reply.Payload<u64>(4, 0x12345678ULL);
            reply.Payload<u64>(12, 0x9ABCDEF0ULL);
            return static_cast<u32>(IpcResult::Success);
        }

        case CreateScanRequest:
        case CreateRequest: {
            auto req = std::make_shared<RequestService>();
            auto session = std::make_shared<KClientSession>();
            session->SetService(req);

            Handle session_handle = 0;
            if (ctx.handle_table) {
                session_handle = ctx.handle_table->CreateHandle(session);
            }

            reply.Begin(static_cast<u32>(IpcCommandType::Request), 8);
            reply.Payload<u32>(0, static_cast<u32>(IpcResult::Success));
            reply.Payload<u32>(4, session_handle);
            return static_cast<u32>(IpcResult::Success);
        }

        case GetCurrentNetworkProfile: {
            reply.Begin(static_cast<u32>(IpcCommandType::Request), 4);
            reply.Payload<u32>(0, static_cast<u32>(IpcResult::Success));
            return static_cast<u32>(IpcResult::Success);
        }

        case IsAnyInternetRequestAccepted: {
            reply.Begin(static_cast<u32>(IpcCommandType::Request), 8);
            reply.Payload<u32>(0, static_cast<u32>(IpcResult::Success));
            reply.Payload<u32>(4, 0); // 0 = False (offline mode)
            return static_cast<u32>(IpcResult::Success);
        }

        default:
            NEMU_LOG_DEBUG("nifm", "IGeneralService: stubbing command 0x{:X}", x_id);
            reply.Begin(static_cast<u32>(IpcCommandType::Request), 4);
            reply.Payload<u32>(0, static_cast<u32>(IpcResult::Success));
            return static_cast<u32>(IpcResult::Success);
    }
}

// ---------------------------------------------------------------------------
// NifmService (nifm:u)
// ---------------------------------------------------------------------------

NifmService::NifmService(std::string name)
    : IIpcService(std::move(name)) {}

u32 NifmService::HandleRequest(const IpcContext& ctx, const IpcRequestReader& request,
                               IpcReplyWriter& reply, u32 x_id) {
    (void)request;
    switch (x_id) {
        case CreateGeneralServiceOld:
        case CreateGeneralService: {
            auto gen = std::make_shared<GeneralService>();
            auto session = std::make_shared<KClientSession>();
            session->SetService(gen);

            Handle session_handle = 0;
            if (ctx.handle_table) {
                session_handle = ctx.handle_table->CreateHandle(session);
            }

            reply.Begin(static_cast<u32>(IpcCommandType::Request), 8);
            reply.Payload<u32>(0, static_cast<u32>(IpcResult::Success));
            reply.Payload<u32>(4, session_handle);
            return static_cast<u32>(IpcResult::Success);
        }

        default:
            NEMU_LOG_WARN("nifm", "{}: Unhandled command 0x{:X}", GetName(), x_id);
            reply.Begin(static_cast<u32>(IpcCommandType::Request), 4);
            reply.Payload<u32>(0, static_cast<u32>(IpcResult::Unimplemented));
            return static_cast<u32>(IpcResult::Unimplemented);
    }
}

// ---------------------------------------------------------------------------
// BsdService (bsd:u / bsd:s)
// ---------------------------------------------------------------------------

BsdService::BsdService(std::string name)
    : IIpcService(std::move(name)) {}

u32 BsdService::HandleRequest(const IpcContext& ctx, const IpcRequestReader& request,
                              IpcReplyWriter& reply, u32 x_id) {
    (void)ctx;
    (void)request;
    switch (x_id) {
        case RegisterClient:
        case StartMonitoring:
        case Socket:
        case Close: {
            reply.Begin(static_cast<u32>(IpcCommandType::Request), 4);
            reply.Payload<u32>(0, static_cast<u32>(IpcResult::Success));
            return static_cast<u32>(IpcResult::Success);
        }
        default:
            reply.Begin(static_cast<u32>(IpcCommandType::Request), 4);
            reply.Payload<u32>(0, static_cast<u32>(IpcResult::Success));
            return static_cast<u32>(IpcResult::Success);
    }
}

} // namespace nemu::core::kernel::ipc
