#include "mii_service.hpp"
#include "platform/logger.hpp"
#include "core/memory/virtual_memory.hpp"
#include <cstring>

namespace nemu::core::kernel::ipc {

MiiService::MiiService(std::string name) : IIpcService(std::move(name)) {}

u32 MiiService::HandleRequest(const IpcContext& ctx, const IpcRequestReader& request,
                             IpcReplyWriter& reply, u32 cmd_id) {
    (void)ctx;
    (void)request;

    switch (cmd_id) {
        case IsFullDatabase: {
            NEMU_LOG_DEBUG("Mii", "IsFullDatabase() -> false");
            reply.Begin(static_cast<u32>(IpcCommandType::Request), 16);
            reply.Payload<u32>(0, 0); // Success
            reply.Payload<u8>(4, 0);  // IsFull = false
            return static_cast<u32>(IpcResult::Success);
        }
        case GetCount: {
            NEMU_LOG_DEBUG("Mii", "GetCount() -> 1 default Mii");
            reply.Begin(static_cast<u32>(IpcCommandType::Request), 16);
            reply.Payload<u32>(0, 0); // Success
            reply.Payload<u32>(4, 1); // Count = 1
            return static_cast<u32>(IpcResult::Success);
        }
        case Get:
        case Get1:
        case Get2:
        case GetDefault:
        case BuildRandom: {
            NEMU_LOG_DEBUG("Mii", "Get/GetDefault Mii character record (cmd 0x{:X})", cmd_id);
            MiiCharInfo mii{};
            // If random, vary color and hair
            if (cmd_id == BuildRandom) {
                mii.favorite_color = 2; // Yellow
                mii.hair_type = 1;
            }

            // Write to client buffer descriptor if present
            if (ctx.memory) {
                for (const auto& desc : request.GetBufferDescriptors()) {
                    if ((desc.type == IpcBufferType::B_Receive || desc.type == IpcBufferType::C_Receive) &&
                        desc.address != 0 && desc.size >= sizeof(MiiCharInfo)) {
                        ctx.memory->WriteBlock(desc.address, &mii, sizeof(MiiCharInfo));
                        break;
                    }
                }
            }

            // Also write into reply payload for titles expecting inline record
            reply.Begin(static_cast<u32>(IpcCommandType::Request), 16 + sizeof(MiiCharInfo));
            reply.Payload<u32>(0, 0); // Success
            const u8* raw_mii = reinterpret_cast<const u8*>(&mii);
            for (size_t i = 0; i < sizeof(MiiCharInfo); ++i) {
                reply.Write<u8>(static_cast<size_t>(IpcField::Payload) + 8 + i, raw_mii[i]);
            }
            return static_cast<u32>(IpcResult::Success);
        }
        default:
            NEMU_LOG_WARN("Mii", "Unhandled cmd 0x{:X}", cmd_id);
            reply.Begin(static_cast<u32>(IpcCommandType::Request), 8);
            reply.Payload<u32>(0, 0);
            return static_cast<u32>(IpcResult::Success);
    }
}

MiiDatabaseSubService::MiiDatabaseSubService(std::string name) : IIpcService(std::move(name)) {}

u32 MiiDatabaseSubService::HandleRequest(const IpcContext& ctx, const IpcRequestReader& request,
                                         IpcReplyWriter& reply, u32 cmd_id) {
    (void)ctx;
    (void)request;

    switch (cmd_id) {
        case IsFullDatabase:
            reply.Begin(static_cast<u32>(IpcCommandType::Request), 16);
            reply.Payload<u32>(0, 0);
            reply.Payload<u8>(4, 0);
            return static_cast<u32>(IpcResult::Success);
        case GetCount:
            reply.Begin(static_cast<u32>(IpcCommandType::Request), 16);
            reply.Payload<u32>(0, 0);
            reply.Payload<u32>(4, 1);
            return static_cast<u32>(IpcResult::Success);
        case Get:
        case Get1:
        case Get2:
        case GetDefault: {
            MiiCharInfo mii{};
            if (ctx.memory) {
                for (const auto& desc : request.GetBufferDescriptors()) {
                    if ((desc.type == IpcBufferType::B_Receive || desc.type == IpcBufferType::C_Receive) &&
                        desc.address != 0 && desc.size >= sizeof(MiiCharInfo)) {
                        ctx.memory->WriteBlock(desc.address, &mii, sizeof(MiiCharInfo));
                        break;
                    }
                }
            }
            reply.Begin(static_cast<u32>(IpcCommandType::Request), 16 + sizeof(MiiCharInfo));
            reply.Payload<u32>(0, 0);
            const u8* raw_mii = reinterpret_cast<const u8*>(&mii);
            for (size_t i = 0; i < sizeof(MiiCharInfo); ++i) {
                reply.Write<u8>(static_cast<size_t>(IpcField::Payload) + 8 + i, raw_mii[i]);
            }
            return static_cast<u32>(IpcResult::Success);
        }
        default:
            reply.Begin(static_cast<u32>(IpcCommandType::Request), 8);
            reply.Payload<u32>(0, 0);
            return static_cast<u32>(IpcResult::Success);
    }
}

} // namespace nemu::core::kernel::ipc
