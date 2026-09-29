#pragma once

#include "ipc_service.hpp"
#include <string>
#include <memory>

namespace nemu::core::kernel::ipc {

/// mii:u, mii:e - Nintendo Switch Mii Database Service.
/// Used by Mario Kart 8 Deluxe, Super Smash Bros Ultimate, Switch Sports, etc.
class MiiService final : public IIpcService {
public:
    explicit MiiService(std::string name = "mii:u");
    ~MiiService() override = default;

    enum Commands : u32 {
        IsFullDatabase = 0,
        GetCount = 1,
        Get = 2,
        Get1 = 3,
        Get2 = 4,
        GetDefault = 5,
        BuildRandom = 6,
    };

    u32 HandleRequest(const IpcContext& ctx, const IpcRequestReader& request,
                      IpcReplyWriter& reply, u32 x_id) override;
};

/// IDatabaseService subservice returned by MiiService
class MiiDatabaseSubService final : public IIpcService {
public:
    explicit MiiDatabaseSubService(std::string name = "mii:IDatabaseService");
    ~MiiDatabaseSubService() override = default;

    enum Commands : u32 {
        IsFullDatabase = 0,
        GetCount = 1,
        Get = 2,
        Get1 = 3,
        Get2 = 4,
        GetDefault = 5,
    };

    u32 HandleRequest(const IpcContext& ctx, const IpcRequestReader& request,
                      IpcReplyWriter& reply, u32 x_id) override;
};

} // namespace nemu::core::kernel::ipc
