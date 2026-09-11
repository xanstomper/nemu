#pragma once

#include "core/types.hpp"
#include "core/kernel/ipc/ipc_service.hpp"
#include "core/gpu/nvhost/nvdevice.hpp"
#include <memory>
#include <string>

namespace nemu::core::kernel::ipc {

class NvDrvService final : public IIpcService {
public:
    explicit NvDrvService(std::string name, std::shared_ptr<gpu::nvhost::NvDeviceManager> device_manager);
    ~NvDrvService() override = default;

    u32 HandleRequest(const IpcContext& ctx, const IpcRequestReader& request,
                      IpcReplyWriter& reply, u32 x_id) override;

    [[nodiscard]] std::shared_ptr<gpu::nvhost::NvDeviceManager> GetDeviceManager() const noexcept {
        return device_manager_;
    }

private:
    u32 HandleOpen(const IpcContext& ctx, const IpcRequestReader& request, IpcReplyWriter& reply);
    u32 HandleIoctl(const IpcContext& ctx, const IpcRequestReader& request, IpcReplyWriter& reply);
    u32 HandleClose(const IpcContext& ctx, const IpcRequestReader& request, IpcReplyWriter& reply);
    u32 HandleInitialize(const IpcContext& ctx, const IpcRequestReader& request, IpcReplyWriter& reply);
    u32 HandleQueryEvent(const IpcContext& ctx, const IpcRequestReader& request, IpcReplyWriter& reply);
    u32 HandleSetClientPID(const IpcContext& ctx, const IpcRequestReader& request, IpcReplyWriter& reply);
    u32 HandleIoctl2(const IpcContext& ctx, const IpcRequestReader& request, IpcReplyWriter& reply);
    u32 HandleIoctl3(const IpcContext& ctx, const IpcRequestReader& request, IpcReplyWriter& reply);

    std::shared_ptr<gpu::nvhost::NvDeviceManager> device_manager_;
};

} // namespace nemu::core::kernel::ipc
