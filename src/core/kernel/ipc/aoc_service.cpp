#include "aoc_service.hpp"
#include "platform/logger.hpp"
#include "core/kernel/k_handle_table.hpp"

namespace nemu::core::kernel::ipc {

AocService::AocService(std::string name)
    : IIpcService(std::move(name)),
      change_event_(std::make_shared<KEvent>(/*auto_clear=*/false)) {}

u32 AocService::HandleRequest(const IpcContext& ctx, const IpcRequestReader& request,
                              IpcReplyWriter& reply, u32 x_id) {
    (void)request;
    switch (x_id) {
        case CountAddOnContentByApplicationId:
        case CountAddOnContent: {
            reply.Begin(static_cast<u32>(IpcCommandType::Request), 8);
            reply.Payload<u32>(0, 0); // Result Success
            reply.Payload<u32>(4, dlc_count_);
            return static_cast<u32>(IpcResult::Success);
        }

        case ListAddOnContentByApplicationId:
        case ListAddOnContent: {
            // Count = 0
            reply.Begin(static_cast<u32>(IpcCommandType::Request), 8);
            reply.Payload<u32>(0, 0); // Result Success
            reply.Payload<u32>(4, 0); // Count = 0
            return static_cast<u32>(IpcResult::Success);
        }

        case GetAddOnContentBaseIdByApplicationId:
        case GetAddOnContentBaseId: {
            reply.Begin(static_cast<u32>(IpcCommandType::Request), 16);
            reply.Payload<u32>(0, 0); // Result Success
            reply.Payload<u64>(8, 0x0100000000010000ULL);
            return static_cast<u32>(IpcResult::Success);
        }

        case PrepareAddOnContentByApplicationId:
        case PrepareAddOnContent: {
            reply.Begin(static_cast<u32>(IpcCommandType::Request), 8);
            reply.Payload<u32>(0, 0); // Result Success
            return static_cast<u32>(IpcResult::Success);
        }

        case GetAddOnContentListChangedEvent:
        case GetAddOnContentListChangedEventWithProcessId: {
            Handle event_handle = 0;
            if (ctx.handle_table) {
                event_handle = ctx.handle_table->CreateHandle(change_event_);
            }
            reply.Begin(static_cast<u32>(IpcCommandType::Request), 16);
            reply.Payload<u32>(0, 0); // Result Success
            reply.Payload<u32>(4, event_handle);
            return static_cast<u32>(IpcResult::Success);
        }

        case NotifyMountAddOnContent:
        case NotifyUnmountAddOnContent:
        case CheckAddOnContentMountStatus: {
            reply.Begin(static_cast<u32>(IpcCommandType::Request), 8);
            reply.Payload<u32>(0, 0); // Result Success
            return static_cast<u32>(IpcResult::Success);
        }

        default:
            NEMU_LOG_DEBUG("aoc", "AocService: Handled command 0x{:X}", x_id);
            reply.Begin(static_cast<u32>(IpcCommandType::Request), 16);
            reply.Payload<u32>(0, 0);
            return static_cast<u32>(IpcResult::Success);
    }
}

} // namespace nemu::core::kernel::ipc
