#include "spl_service.hpp"
#include "platform/logger.hpp"
#include <random>

namespace nemu::core::kernel::ipc {

SplService::SplService(std::string name) : IIpcService(std::move(name)) {}

u32 SplService::HandleRequest(const IpcContext& ctx, const IpcRequestReader& request,
                             IpcReplyWriter& reply, u32 cmd_id) {
    (void)ctx;
    (void)request;

    switch (cmd_id) {
        case GetRandomBytes: {
            NEMU_LOG_DEBUG("SPL", "GetRandomBytes() returning 16 bytes of entropy");
            std::random_device rd;
            std::mt19937_64 gen(rd());
            std::uniform_int_distribution<u64> dis;
            u64 r1 = dis(gen);
            u64 r2 = dis(gen);
            reply.Begin(static_cast<u32>(IpcCommandType::Request), 24);
            reply.Payload<u32>(0, 0); // Result Success
            reply.Payload<u64>(8, r1);
            reply.Payload<u64>(16, r2);
            return static_cast<u32>(IpcResult::Success);
        }
        case GenerateAesKek:
        case GenerateAesKey:
        case GenerateKey: {
            NEMU_LOG_DEBUG("SPL", "Key generation cmd 0x{:X}", cmd_id);
            reply.Begin(static_cast<u32>(IpcCommandType::Request), 24);
            reply.Payload<u32>(0, 0);
            reply.Payload<u64>(8, 0x0123456789ABCDEFULL);
            reply.Payload<u64>(16, 0xFEDCBA9876543210ULL);
            return static_cast<u32>(IpcResult::Success);
        }
        case ComputeCmac: {
            NEMU_LOG_DEBUG("SPL", "ComputeCmac() stub");
            reply.Begin(static_cast<u32>(IpcCommandType::Request), 24);
            reply.Payload<u32>(0, 0);
            reply.Payload<u64>(8, 0xAA55AA55AA55AA55ULL);
            reply.Payload<u64>(16, 0x55AA55AA55AA55AAULL);
            return static_cast<u32>(IpcResult::Success);
        }
        default:
            NEMU_LOG_WARN("SPL", "Unhandled cmd 0x{:X}", cmd_id);
            reply.Begin(static_cast<u32>(IpcCommandType::Request), 8);
            reply.Payload<u32>(0, 0);
            return static_cast<u32>(IpcResult::Success);
    }
}

} // namespace nemu::core::kernel::ipc
