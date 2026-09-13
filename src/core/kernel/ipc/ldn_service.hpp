#pragma once

#include "ipc_service.hpp"
#include "core/network/ldn_network.hpp"
#include <memory>

namespace nemu::core::kernel::ipc {

/// ldn:u / ldn:m - Nintendo Switch local-wireless network service.
/// Backed by the real UDP LAN backend so multiple Nemu instances (or
/// compatible clients like Ryujinx-LDN) can discover each other and play
/// local-wireless titles over the LAN.
class LdnService final : public IIpcService {
public:
    explicit LdnService(std::shared_ptr<nemu::core::network::LdnUdpNetwork> net,
                        std::string name = "ldn:u");
    ~LdnService() override = default;

    enum Commands : u32 {
        CreateUserLocalCommunicationService = 0,
        CreateMonitorLocalCommunicationService = 1, // ldn:m only
        CreateVirtualSandboxUserLocalCommunicationService = 1000,
    };

    u32 HandleRequest(const IpcContext& ctx, const IpcRequestReader& request,
                      IpcReplyWriter& reply, u32 x_id) override;

private:
    std::shared_ptr<nemu::core::network::LdnUdpNetwork> net_;
};

} // namespace nemu::core::kernel::ipc
