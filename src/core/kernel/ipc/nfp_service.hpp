#pragma once

#include "ipc_service.hpp"
#include <string>
#include <memory>

namespace nemu::core::kernel::ipc {

/// nfp:user, nfc:user - Near Field Communication / Amiibo Service.
/// Used by The Legend of Zelda: Breath of the Wild, Super Smash Bros. Ultimate,
/// Mario Kart 8 Deluxe, etc.
class NfpService final : public IIpcService {
public:
    explicit NfpService(std::string name = "nfp:user");
    ~NfpService() override = default;

    enum Commands : u32 {
        CreateUserInterface = 0,
    };

    u32 HandleRequest(const IpcContext& ctx, const IpcRequestReader& request,
                      IpcReplyWriter& reply, u32 x_id) override;
};

/// IUser subservice returned by NfpService::CreateUserInterface
class NfpUserSubService final : public IIpcService {
public:
    explicit NfpUserSubService(std::string name = "nfp:IUser");
    ~NfpUserSubService() override = default;

    enum Commands : u32 {
        Initialize = 0,
        Finalize = 1,
        ListDevices = 2,
        StartDetection = 3,
        StopDetection = 4,
        Mount = 5,
        Unmount = 6,
        GetTagInfo = 17,
        GetRegisterInfo = 18,
        GetCommonInfo = 19,
        GetModelInfo = 20,
    };

    u32 HandleRequest(const IpcContext& ctx, const IpcRequestReader& request,
                      IpcReplyWriter& reply, u32 x_id) override;
};

} // namespace nemu::core::kernel::ipc
