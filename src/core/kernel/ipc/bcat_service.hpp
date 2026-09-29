#pragma once

#include "ipc_service.hpp"
#include <string>
#include <memory>

namespace nemu::core::kernel::ipc {

/// bcat:u, bcat:a, bcat:m - Background Content Asynchronous Transfer (Boxcat).
/// Used by Animal Crossing: New Horizons, Super Mario Odyssey, Splatoon 2, etc.
class BcatService final : public IIpcService {
public:
    explicit BcatService(std::string name = "bcat:u");
    ~BcatService() override = default;

    enum Commands : u32 {
        CreateDeliveryCacheStorageService = 0,
        CreateDeliveryCacheProgressService = 1,
    };

    u32 HandleRequest(const IpcContext& ctx, const IpcRequestReader& request,
                      IpcReplyWriter& reply, u32 x_id) override;
};

/// IDeliveryCacheStorageService subservice returned by BcatService
class DeliveryCacheStorageSubService final : public IIpcService {
public:
    explicit DeliveryCacheStorageSubService(std::string name = "bcat:IDeliveryCacheStorageService");
    ~DeliveryCacheStorageSubService() override = default;

    enum Commands : u32 {
        CreateDeliveryCacheDirectoryService = 0,
        CreateDeliveryCacheFileService = 1,
        EnumerateDeliveryCacheDirectory = 10,
    };

    u32 HandleRequest(const IpcContext& ctx, const IpcRequestReader& request,
                      IpcReplyWriter& reply, u32 x_id) override;
};

} // namespace nemu::core::kernel::ipc
