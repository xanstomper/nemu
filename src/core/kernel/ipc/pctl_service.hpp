#pragma once

#include "ipc_service.hpp"
#include <string>
#include <memory>

namespace nemu::core::kernel::ipc {

/// pctl, pctl:a, pctl:s, pctl:r - Parental Control Service Factory.
class PctlService final : public IIpcService {
public:
    explicit PctlService(std::string name = "pctl");
    ~PctlService() override = default;

    enum Commands : u32 {
        CreateService = 0,
        CreateServiceWithoutInitialize = 1,
    };

    u32 HandleRequest(const IpcContext& ctx, const IpcRequestReader& request,
                      IpcReplyWriter& reply, u32 x_id) override;
};

/// IParentalControlService subsession returned by CreateService
class ParentalControlSubService final : public IIpcService {
public:
    explicit ParentalControlSubService(std::string name = "pctl:IParentalControlService");
    ~ParentalControlSubService() override = default;

    enum Commands : u32 {
        Initialize = 1,
        CheckFreeCommunicationPermission = 1001,
        ConfirmLaunchApplicationPermission = 1002,
        ConfirmResumeApplicationPermission = 1003,
        ConfirmSnsPostPermission = 1004,
        ConfirmSystemSettingsPermission = 1005,
        IsRestrictionTemporaryUnlocked = 1006,
        IsRestrictedSystemSettingsEntered = 1010,
        IsFreeCommunicationAvailable = 1018,
        IsRestrictionEnabled = 1031,
        GetSafetyLevel = 1032,
        GetCurrentSettings = 1035,
    };

    u32 HandleRequest(const IpcContext& ctx, const IpcRequestReader& request,
                      IpcReplyWriter& reply, u32 x_id) override;
};

} // namespace nemu::core::kernel::ipc
