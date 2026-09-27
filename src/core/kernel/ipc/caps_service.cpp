#include "caps_service.hpp"
#include "platform/logger.hpp"
#include "core/kernel/k_handle_table.hpp"

namespace nemu::core::kernel::ipc {

CapsService::CapsService(std::string name)
    : IIpcService(std::move(name)) {}

u32 CapsService::HandleRequest(const IpcContext& ctx, const IpcRequestReader& request,
                               IpcReplyWriter& reply, u32 x_id) {
    switch (x_id) {
        case SetShimLibraryVersion: {
            shim_library_version_ = request.Payload<u64>(0);
            NEMU_LOG_DEBUG("caps", "caps::SetShimLibraryVersion: version={}", shim_library_version_);
            reply.Begin(static_cast<u32>(IpcCommandType::Request), 8);
            reply.Payload<u32>(0, 0); // Result Success
            return static_cast<u32>(IpcResult::Success);
        }

        case GetAlbumFileList0AafeAruidDeprecated:
        case GetAlbumFileList3AaeAruid:
        case GetAlbumFileList1AafeAruidDeprecated:
        case GetAlbumFileList2AafeUidAruidDeprecated:
        case GetAlbumFileList4AaeUidAruid:
        case GetAllAlbumFileList3AaeAruid: {
            // Out<u64> out_entries_count = 0 (empty album on clean boot)
            reply.Begin(static_cast<u32>(IpcCommandType::Request), 16);
            reply.Payload<u32>(0, 0); // Result Success
            reply.Payload<u64>(8, 0); // Count = 0
            return static_cast<u32>(IpcResult::Success);
        }

        case DeleteAlbumFileByAruid:
        case DeleteAlbumFileByAruidForDebug: {
            reply.Begin(static_cast<u32>(IpcCommandType::Request), 8);
            reply.Payload<u32>(0, 0); // Result Success
            return static_cast<u32>(IpcResult::Success);
        }

        case GetAlbumFileSizeByAruid: {
            reply.Begin(static_cast<u32>(IpcCommandType::Request), 16);
            reply.Payload<u32>(0, 0); // Result Success
            reply.Payload<u64>(8, 0); // Size = 0
            return static_cast<u32>(IpcResult::Success);
        }

        case PrecheckToCreateContentsByAruid: {
            reply.Begin(static_cast<u32>(IpcCommandType::Request), 8);
            reply.Payload<u32>(0, 0); // Result Success
            return static_cast<u32>(IpcResult::Success);
        }

        case CaptureScreenShot:
        case CaptureScreenShotWithUserData: {
            NEMU_LOG_DEBUG("caps", "caps::CaptureScreenShot (command {})", x_id);
            reply.Begin(static_cast<u32>(IpcCommandType::Request), 8);
            reply.Payload<u32>(0, 0); // Result Success
            return static_cast<u32>(IpcResult::Success);
        }

        case OpenAccessorSessionForApplication: {
            Handle subsession_handle = 0;
            if (ctx.handle_table) {
                auto session = std::make_shared<KClientSession>();
                session->SetService(std::make_shared<CapsAccessorSessionService>());
                subsession_handle = ctx.handle_table->CreateHandle(session);
            }
            reply.Begin(static_cast<u32>(IpcCommandType::Request), 16);
            reply.Payload<u32>(0, 0); // Result Success
            reply.Payload<u32>(4, subsession_handle);
            return static_cast<u32>(IpcResult::Success);
        }

        default:
            NEMU_LOG_DEBUG("caps", "{}: Handled command 0x{:X}", GetName(), x_id);
            reply.Begin(static_cast<u32>(IpcCommandType::Request), 16);
            reply.Payload<u32>(0, 0);
            return static_cast<u32>(IpcResult::Success);
    }
}

CapsAccessorSessionService::CapsAccessorSessionService(std::string name)
    : IIpcService(std::move(name)) {}

u32 CapsAccessorSessionService::HandleRequest(const IpcContext& ctx, const IpcRequestReader& request,
                                             IpcReplyWriter& reply, u32 x_id) {
    (void)ctx;
    (void)request;
    NEMU_LOG_DEBUG("caps", "CapsAccessorSession: Handled command 0x{:X}", x_id);
    reply.Begin(static_cast<u32>(IpcCommandType::Request), 16);
    reply.Payload<u32>(0, 0);
    return static_cast<u32>(IpcResult::Success);
}

} // namespace nemu::core::kernel::ipc
