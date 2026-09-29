#pragma once

#include "ipc_service.hpp"
#include <string>
#include <memory>

namespace nemu::core::kernel::ipc {

/// lm, lm:m - Log Manager Service.
/// Used by Nintendo SDK nn::diag to route debug and crash telemetry.
class LmService final : public IIpcService {
public:
    explicit LmService(std::string name = "lm");
    ~LmService() override = default;

    enum Commands : u32 {
        Initialize = 0,
    };

    u32 HandleRequest(const IpcContext& ctx, const IpcRequestReader& request,
                      IpcReplyWriter& reply, u32 x_id) override;
};

/// ILogger subservice returned by LmService::Initialize
class LoggerSubService final : public IIpcService {
public:
    explicit LoggerSubService(std::string name = "lm:ILogger");
    ~LoggerSubService() override = default;

    enum Commands : u32 {
        Log = 0,
        SetDestination = 1,
    };

    u32 HandleRequest(const IpcContext& ctx, const IpcRequestReader& request,
                      IpcReplyWriter& reply, u32 x_id) override;
};

} // namespace nemu::core::kernel::ipc
