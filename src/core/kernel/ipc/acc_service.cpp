#include "acc_service.hpp"
#include "core/kernel/k_handle_table.hpp"
#include "core/kernel/ipc/ipc_service.hpp"
#include "platform/logger.hpp"

namespace nemu::core::kernel::ipc {

// ---------------------------------------------------------------------------
// ProfileService
// ---------------------------------------------------------------------------

ProfileService::ProfileService(UserId user_id, std::string nickname)
    : IIpcService("acc:IProfile"), user_id_(user_id), nickname_(std::move(nickname)) {}

u32 ProfileService::HandleRequest(
    const IpcContext& ctx,
    const IpcRequestReader& request,
    IpcReplyWriter& reply,
    u32 x_id
) {
    (void)ctx;
    (void)request;

    switch (x_id) {
        case Get:
        case GetBase: {
            reply.Begin(0, 48);
            reply.Payload<u32>(0, 0); // Result Success
            reply.Payload<u64>(4, user_id_.low);
            reply.Payload<u64>(12, user_id_.high);
            (void)reply.WriteString(20, nickname_);
            return static_cast<u32>(IpcResult::Success);
        }

        default:
            reply.Begin(0, 4);
            reply.Payload<u32>(0, 0);
            return static_cast<u32>(IpcResult::Success);
    }
}

// ---------------------------------------------------------------------------
// AccountService
// ---------------------------------------------------------------------------

AccountService::AccountService() : IIpcService("acc:u0") {}

u32 AccountService::HandleRequest(
    const IpcContext& ctx,
    const IpcRequestReader& request,
    IpcReplyWriter& reply,
    u32 x_id
) {
    (void)request;

    switch (x_id) {
        case GetUserCount: {
            reply.Begin(0, 8);
            reply.Payload<u32>(0, 0);
            reply.Payload<u32>(4, 1); // 1 active user
            return static_cast<u32>(IpcResult::Success);
        }

        case GetUserExistence: {
            reply.Begin(0, 8);
            reply.Payload<u32>(0, 0);
            reply.Payload<u32>(4, 1); // User exists
            return static_cast<u32>(IpcResult::Success);
        }

        case ListOpenUsers:
        case GetLastOpenedUser: {
            reply.Begin(0, 24);
            reply.Payload<u32>(0, 0);
            reply.Payload<u64>(4, default_user_.low);
            reply.Payload<u64>(12, default_user_.high);
            return static_cast<u32>(IpcResult::Success);
        }

        case GetProfile: {
            auto profile_svc = std::make_shared<ProfileService>(default_user_);
            auto session = std::make_shared<KClientSession>();
            session->SetService(profile_svc);

            Handle session_handle = 0;
            if (ctx.handle_table) {
                session_handle = ctx.handle_table->CreateHandle(session);
            }

            reply.Begin(0, 8);
            reply.Payload<u32>(0, 0);
            reply.Payload<u32>(4, session_handle);
            return static_cast<u32>(IpcResult::Success);
        }

        case InitializeApplicationInfo: {
            reply.Begin(0, 4);
            reply.Payload<u32>(0, 0);
            return static_cast<u32>(IpcResult::Success);
        }

        default:
            NEMU_LOG_WARN("acc:u0", "acc:u0: Unhandled command 0x{:X}", x_id);
            reply.Begin(0, 4);
            reply.Payload<u32>(0, static_cast<u32>(IpcResult::Unimplemented));
            return static_cast<u32>(IpcResult::Unimplemented);
    }
}

} // namespace nemu::core::kernel::ipc
