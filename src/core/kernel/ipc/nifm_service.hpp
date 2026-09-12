#pragma once

#include "ipc_service.hpp"
#include "core/kernel/k_event.hpp"
#include <string>
#include <memory>

namespace nemu::core::kernel::ipc {

class RequestService final : public IIpcService {
public:
    RequestService();
    ~RequestService() override = default;

    enum Commands : u32 {
        GetRequestState = 0,
        GetResult = 1,
        GetSystemEventReadableHandle = 2,
        Cancel = 3,
        Submit = 4,
        SetRequirement = 11,
        SetRequirementPreset = 12,
    };

    u32 HandleRequest(const IpcContext& ctx, const IpcRequestReader& request,
                       IpcReplyWriter& reply, u32 x_id) override;

private:
    std::shared_ptr<KEvent> completion_event_;
};

class GeneralService final : public IIpcService {
public:
    GeneralService();
    ~GeneralService() override = default;

    enum Commands : u32 {
        GetClientId = 1,
        CreateScanRequest = 2,
        CreateRequest = 4,
        GetCurrentNetworkProfile = 5,
        IsAnyInternetRequestAccepted = 12,
    };

    u32 HandleRequest(const IpcContext& ctx, const IpcRequestReader& request,
                       IpcReplyWriter& reply, u32 x_id) override;
};

class NifmService final : public IIpcService {
public:
    explicit NifmService(std::string name = "nifm:u");
    ~NifmService() override = default;

    enum Commands : u32 {
        CreateGeneralServiceOld = 4,
        CreateGeneralService = 5,
    };

    u32 HandleRequest(const IpcContext& ctx, const IpcRequestReader& request,
                       IpcReplyWriter& reply, u32 x_id) override;
};

class BsdService final : public IIpcService {
public:
    explicit BsdService(std::string name = "bsd:u");
    ~BsdService() override = default;

    enum Commands : u32 {
        RegisterClient = 0,
        StartMonitoring = 1,
        Socket = 2,
        Close = 11,
    };

    u32 HandleRequest(const IpcContext& ctx, const IpcRequestReader& request,
                       IpcReplyWriter& reply, u32 x_id) override;
};

} // namespace nemu::core::kernel::ipc
