#pragma once

#include "ipc_service.hpp"
#include "core/kernel/k_event.hpp"
#include <string>
#include <memory>

namespace nemu::core::kernel::ipc {

/// Performance Management Mode
enum class PerformanceMode : u32 {
    Handheld = 0,
    Docked = 1,
};

/// apm / apm:p - Application Performance Management service.
class ApmService final : public IIpcService {
public:
    explicit ApmService(std::string name = "apm");
    ~ApmService() override = default;

    enum Commands : u32 {
        OpenSession = 0,
        GetPerformanceMode = 1,
        IsCpuOverclockEnabled = 2,
    };

    u32 HandleRequest(const IpcContext& ctx, const IpcRequestReader& request,
                      IpcReplyWriter& reply, u32 x_id) override;

    void SetMode(PerformanceMode mode) noexcept { mode_ = mode; }
    [[nodiscard]] PerformanceMode GetMode() const noexcept { return mode_; }

private:
    PerformanceMode mode_{PerformanceMode::Docked};
};

/// Sub-session object returned by apm OpenSession
class ApmSessionService final : public IIpcService {
public:
    explicit ApmSessionService(std::string name = "apm:ISession");
    ~ApmSessionService() override = default;

    enum Commands : u32 {
        SetPerformanceConfiguration = 0,
        GetPerformanceConfiguration = 1,
        SetCpuOverclockEnabled = 2,
    };

    u32 HandleRequest(const IpcContext& ctx, const IpcRequestReader& request,
                      IpcReplyWriter& reply, u32 x_id) override;

private:
    u32 config_{0x00010001};
};

/// apm:sys - System Performance Management service
class ApmSysService final : public IIpcService {
public:
    explicit ApmSysService(std::string name = "apm:sys");
    ~ApmSysService() override = default;

    enum Commands : u32 {
        GetPerformanceEvent = 0,
        GetCurrentPerformanceConfiguration = 1,
        SetCpuBoostMode = 2,
        GetCurrentPerformanceOption = 3,
        BootMode = 4,
    };

    u32 HandleRequest(const IpcContext& ctx, const IpcRequestReader& request,
                      IpcReplyWriter& reply, u32 x_id) override;

private:
    std::shared_ptr<KEvent> perf_event_;
};

} // namespace nemu::core::kernel::ipc
