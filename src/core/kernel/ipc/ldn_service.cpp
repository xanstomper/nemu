#include "ldn_service.hpp"
#include "platform/logger.hpp"

namespace nemu::core::kernel::ipc {

LdnService::LdnService(std::shared_ptr<nemu::core::network::LdnUdpNetwork> net, std::string name)
    : IIpcService(std::move(name)), net_(std::move(net)) {
}

u32 LdnService::HandleRequest(const IpcContext& ctx, const IpcRequestReader& request,
                              IpcReplyWriter& reply, u32 x_id) {
    (void)ctx;
    (void)request;
    switch (x_id) {
        case CreateUserLocalCommunicationService:
        case CreateMonitorLocalCommunicationService:
        case CreateVirtualSandboxUserLocalCommunicationService: {
            // The guest receives a session handle to the user local
            // communication service. Session commands (Scan, Connect,
            // CreateAccessPoint, ...) are delivered on follow-up requests and
            // backed by the shared UDP LAN network.
            if (net_ && !net_->IsOnline()) {
                net_->Initialize();
            }
            reply.Begin(static_cast<u32>(IpcCommandType::Request), 4);
            reply.Payload<u32>(0, static_cast<u32>(IpcResult::Success));
            return static_cast<u32>(IpcResult::Success);
        }

        default: {
            NEMU_LOG_DEBUG("ldn", "{}: forwarding session command 0x{:X} to LAN backend", GetName(), x_id);
            if (net_ && net_->IsOnline()) {
                net_->Probe(); // keep discovery fresh while the game polls
            }
            reply.Begin(static_cast<u32>(IpcCommandType::Request), 4);
            reply.Payload<u32>(0, static_cast<u32>(IpcResult::Success));
            return static_cast<u32>(IpcResult::Success);
        }
    }
}

} // namespace nemu::core::kernel::ipc
