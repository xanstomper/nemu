#pragma once

#include "core/types.hpp"
#include "core/kernel/ipc/ipc_service.hpp"
#include "core/gpu/presentation/nvnflinger.hpp"
#include <memory>
#include <string>

namespace nemu::core::kernel::ipc {

class ViService final : public IIpcService {
public:
    explicit ViService(std::string name, std::shared_ptr<gpu::presentation::Nvnflinger> flinger);
    ~ViService() override = default;

    u32 HandleRequest(const IpcContext& ctx, const IpcRequestReader& request,
                      IpcReplyWriter& reply, u32 x_id) override;

    [[nodiscard]] std::shared_ptr<gpu::presentation::Nvnflinger> GetFlinger() const noexcept {
        return flinger_;
    }

private:
    u32 HandleGetDisplayService(const IpcContext& ctx, const IpcRequestReader& request, IpcReplyWriter& reply);

    std::shared_ptr<gpu::presentation::Nvnflinger> flinger_;
};

class ApplicationDisplayService final : public IIpcService {
public:
    explicit ApplicationDisplayService(std::shared_ptr<gpu::presentation::Nvnflinger> flinger);
    ~ApplicationDisplayService() override = default;

    u32 HandleRequest(const IpcContext& ctx, const IpcRequestReader& request,
                      IpcReplyWriter& reply, u32 x_id) override;

private:
    u32 HandleGetRelayService(const IpcContext& ctx, const IpcRequestReader& request, IpcReplyWriter& reply);
    u32 HandleOpenDisplay(const IpcContext& ctx, const IpcRequestReader& request, IpcReplyWriter& reply);
    u32 HandleCloseDisplay(const IpcContext& ctx, const IpcRequestReader& request, IpcReplyWriter& reply);
    u32 HandleCreateStrayLayer(const IpcContext& ctx, const IpcRequestReader& request, IpcReplyWriter& reply);
    u32 HandleDestroyStrayLayer(const IpcContext& ctx, const IpcRequestReader& request, IpcReplyWriter& reply);
    u32 HandleGetDisplayVsyncEvent(const IpcContext& ctx, const IpcRequestReader& request, IpcReplyWriter& reply);

    std::shared_ptr<gpu::presentation::Nvnflinger> flinger_;
};

class HosBinderDriverService final : public IIpcService {
public:
    explicit HosBinderDriverService(std::shared_ptr<gpu::presentation::Nvnflinger> flinger);
    ~HosBinderDriverService() override = default;

    u32 HandleRequest(const IpcContext& ctx, const IpcRequestReader& request,
                      IpcReplyWriter& reply, u32 x_id) override;

private:
    u32 HandleTransactParcel(const IpcContext& ctx, const IpcRequestReader& request, IpcReplyWriter& reply);
    u32 HandleAdjustRefcount(const IpcContext& ctx, const IpcRequestReader& request, IpcReplyWriter& reply);
    u32 HandleGetNativeHandle(const IpcContext& ctx, const IpcRequestReader& request, IpcReplyWriter& reply);

    std::shared_ptr<gpu::presentation::Nvnflinger> flinger_;
};

} // namespace nemu::core::kernel::ipc
