#include "friend_service.hpp"
#include "platform/logger.hpp"
#include "core/kernel/k_handle_table.hpp"

namespace nemu::core::kernel::ipc {

FriendService::FriendService(std::string name)
    : IIpcService(std::move(name)) {}

u32 FriendService::HandleRequest(const IpcContext& ctx, const IpcRequestReader& request,
                                 IpcReplyWriter& reply, u32 x_id) {
    (void)request;
    switch (x_id) {
        case CreateFriendService: {
            Handle sub_handle = 0;
            if (ctx.handle_table) {
                auto session = std::make_shared<KClientSession>();
                session->SetService(std::make_shared<FriendSubService>());
                sub_handle = ctx.handle_table->CreateHandle(session);
            }
            reply.Begin(static_cast<u32>(IpcCommandType::Request), 16);
            reply.Payload<u32>(0, 0); // Result Success
            reply.Payload<u32>(4, sub_handle);
            return static_cast<u32>(IpcResult::Success);
        }

        case CreateNotificationService: {
            reply.Begin(static_cast<u32>(IpcCommandType::Request), 8);
            reply.Payload<u32>(0, 0);
            return static_cast<u32>(IpcResult::Success);
        }

        default:
            NEMU_LOG_DEBUG("friend", "{}: Handled command 0x{:X}", GetName(), x_id);
            reply.Begin(static_cast<u32>(IpcCommandType::Request), 16);
            reply.Payload<u32>(0, 0);
            return static_cast<u32>(IpcResult::Success);
    }
}

FriendSubService::FriendSubService(std::string name)
    : IIpcService(std::move(name)),
      completion_event_(std::make_shared<KEvent>(/*auto_clear=*/false)) {}

u32 FriendSubService::HandleRequest(const IpcContext& ctx, const IpcRequestReader& request,
                                   IpcReplyWriter& reply, u32 x_id) {
    (void)request;
    switch (x_id) {
        case GetCompletionEvent: {
            Handle event_handle = 0;
            if (ctx.handle_table) {
                event_handle = ctx.handle_table->CreateHandle(completion_event_);
            }
            reply.Begin(static_cast<u32>(IpcCommandType::Request), 16);
            reply.Payload<u32>(0, 0);
            reply.Payload<u32>(4, event_handle);
            return static_cast<u32>(IpcResult::Success);
        }

        case Cancel:
        case DeclareCloseOnlinePlaySession:
        case UpdateUserPresence: {
            reply.Begin(static_cast<u32>(IpcCommandType::Request), 8);
            reply.Payload<u32>(0, 0); // Result Success
            return static_cast<u32>(IpcResult::Success);
        }

        case GetFriendList: {
            // Count = 0
            reply.Begin(static_cast<u32>(IpcCommandType::Request), 8);
            reply.Payload<u32>(0, 0);
            reply.Payload<u32>(4, 0);
            return static_cast<u32>(IpcResult::Success);
        }

        case CheckFriendListAvailability: {
            reply.Begin(static_cast<u32>(IpcCommandType::Request), 8);
            reply.Payload<u32>(0, 0);
            reply.Payload<u8>(4, 1); // Available = true
            return static_cast<u32>(IpcResult::Success);
        }

        case GetFriendCount:
        case GetNewlyFriendCount: {
            reply.Begin(static_cast<u32>(IpcCommandType::Request), 8);
            reply.Payload<u32>(0, 0);
            reply.Payload<u32>(4, 0); // 0 friends
            return static_cast<u32>(IpcResult::Success);
        }

        default:
            reply.Begin(static_cast<u32>(IpcCommandType::Request), 16);
            reply.Payload<u32>(0, 0);
            return static_cast<u32>(IpcResult::Success);
    }
}

} // namespace nemu::core::kernel::ipc
